#include "gpia/analysis/analyze.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "analyzers.hpp"
#include "evidence_ledger.hpp"

namespace gpia
{

namespace
{

// 编译期按成员类型决定如何取值，可选字段缺失时返回空
template <auto Member> std::optional<double> read_metric(const FrameRecord& frame)
{
    const auto& value = frame.*Member;
    using T = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::is_arithmetic_v<T>)
    {
        return static_cast<double>(value);
    }
    else
    {
        return value ? std::optional<double>{static_cast<double>(*value)} : std::nullopt;
    }
}

struct MetricColumn
{
    std::string_view name;
    std::optional<double> (*read)(const FrameRecord&);
};

// 计数器累计值、帧号和场景标记不是性能指标，所以不做汇总
constexpr std::array<MetricColumn, 18> kMetricColumns{{
    {"frame_time_ms", &read_metric<&FrameRecord::frame_time_ms>},
    {"cpu_main_thread_ms", &read_metric<&FrameRecord::cpu_main_thread_ms>},
    {"cpu_render_thread_ms", &read_metric<&FrameRecord::cpu_render_thread_ms>},
    {"cpu_present_wait_ms", &read_metric<&FrameRecord::cpu_present_wait_ms>},
    {"gpu_frame_time_ms", &read_metric<&FrameRecord::gpu_frame_time_ms>},
    {"gc_alloc_bytes", &read_metric<&FrameRecord::gc_alloc_bytes>},
    {"gc_used_bytes", &read_metric<&FrameRecord::gc_used_bytes>},
    {"gc_reserved_bytes", &read_metric<&FrameRecord::gc_reserved_bytes>},
    {"total_used_bytes", &read_metric<&FrameRecord::total_used_bytes>},
    {"total_reserved_bytes", &read_metric<&FrameRecord::total_reserved_bytes>},
    {"system_used_bytes", &read_metric<&FrameRecord::system_used_bytes>},
    {"draw_calls", &read_metric<&FrameRecord::draw_calls>},
    {"setpass_calls", &read_metric<&FrameRecord::setpass_calls>},
    {"batches", &read_metric<&FrameRecord::batches>},
    {"triangles", &read_metric<&FrameRecord::triangles>},
    {"vertices", &read_metric<&FrameRecord::vertices>},
    {"cpu_frame_time_ms", &read_metric<&FrameRecord::cpu_frame_time_ms>},
    {"gc_collect_ms", &read_metric<&FrameRecord::gc_collect_ms>},
}};

std::vector<ColumnSummary> summarize_columns(const std::vector<std::string>& header,
                                             std::span<const FrameRecord> frames)
{
    std::vector<ColumnSummary> result;
    for (const MetricColumn& column : kMetricColumns)
    {
        if (std::ranges::find(header, column.name) == header.end())
        {
            continue;
        }
        std::vector<double> values;
        values.reserve(frames.size());
        for (const FrameRecord& f : frames)
        {
            if (const auto v = column.read(f))
            {
                values.push_back(*v);
            }
        }
        result.push_back({.column = std::string(column.name),
                          .summary = stats::summarize(values, frames.size() - values.size())});
    }
    return result;
}

// 帧率不设上限时没有帧预算可参考，只依赖滚动中位数
double derive_floor_ms(std::int32_t target_frame_rate) noexcept
{
    return target_frame_rate > 0 ? 1.5 * 1000.0 / static_cast<double>(target_frame_rate) : 0.0;
}

} // namespace

Report analyze_run(const LoadedRun& run, const AnalysisConfig& config,
                   const Diagnostics& diagnostics)
{
    Report report;
    report.run_id = run.manifest.run_id;

    // 预热帧不参与分析，但仍计入输入统计
    std::vector<FrameRecord> analyzed;
    analyzed.reserve(run.table.frames.size());
    std::uint64_t warmup_skipped = 0;
    for (const FrameRecord& f : run.table.frames)
    {
        if (f.frame_index < run.manifest.warmup_frames)
        {
            ++warmup_skipped;
        }
        else
        {
            analyzed.push_back(f);
        }
    }
    report.input = InputInfo{.frames_sha256 = run.frames_sha256,
                             .frames_total = run.table.data_rows,
                             .frames_analyzed = analyzed.size(),
                             .warmup_frames_skipped = warmup_skipped,
                             .frame_index_gaps = run.table.frame_index_gaps,
                             .invalid_rows = run.table.data_rows - run.table.frames.size()};

    AnalysisConfig effective = config;
    if (!effective.hitch.floor_ms)
    {
        effective.hitch.floor_ms = derive_floor_ms(run.manifest.display.target_frame_rate);
    }
    if (!effective.cpu.floor_ms)
    {
        // 主线程尖峰至少要占到半个帧预算才值得报告
        effective.cpu.floor_ms = derive_floor_ms(run.manifest.display.target_frame_rate) / 3.0;
    }
    report.config_json = to_json_text(effective);
    report.summary = summarize_columns(run.table.header, analyzed);

    analysis::EvidenceLedger ledger{run.manifest.run_id};
    if (analyzed.size() < effective.min_frames)
    {
        const std::string id = ledger.add_evidence(
            EvidenceType::DataAvailability,
            std::format("only {} frames remain after warm-up, fewer than min_frames {}",
                        analyzed.size(), effective.min_frames),
            std::nullopt,
            {{"frames_analyzed", analysis::as_int(static_cast<std::int64_t>(analyzed.size()))},
             {"min_frames", analysis::as_int(static_cast<std::int64_t>(effective.min_frames))}});
        Finding fd;
        fd.kind = FindingKind::InsufficientData;
        fd.severity = FindingSeverity::Info;
        fd.level = EvidenceLevel::InsufficientEvidence;
        fd.title = "Too few frames to analyze";
        fd.evidence_ids = {id};
        fd.rule = "input.min_frames";
        fd.missing_evidence = {
            std::format("at least {} frames after warm-up", effective.min_frames)};
        ledger.add_finding(std::move(fd));
        report.outcome = Outcome::InsufficientData;
    }
    else
    {
        const auto hitches =
            analysis::analyze_hitches(analyzed, effective.hitch, *effective.hitch.floor_ms, ledger);
        analysis::analyze_cpu(analyzed, effective.cpu, ledger);
        analysis::analyze_gc(analyzed, effective.gc_alloc, hitches, ledger);
        analysis::analyze_memory(analyzed, effective.memory, ledger);
        const bool warned =
            diagnostics.count(Severity::Warning) + diagnostics.count(Severity::Error) > 0;
        report.outcome = warned ? Outcome::OkWithWarnings : Outcome::Ok;
    }

    report.evidence = ledger.take_evidence();
    report.findings = ledger.take_findings();
    return report;
}

} // namespace gpia
