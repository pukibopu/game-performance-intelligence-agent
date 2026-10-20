#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "analyzers.hpp"

namespace gpia::analysis
{

namespace
{

constexpr double kMiB = 1024.0 * 1024.0;

struct LinearFit
{
    double slope = 0.0;
    double r_squared = 0.0;
};

// 最小二乘直线拟合，先减均值再累加以保持数值稳定
std::optional<LinearFit> fit_line(const std::vector<double>& x, const std::vector<double>& y)
{
    const std::size_t n = x.size();
    if (n < 2)
    {
        return std::nullopt;
    }
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }
    if (sxx <= 0.0)
    {
        return std::nullopt;
    }
    const double r_squared = syy > 0.0 ? (sxy * sxy) / (sxx * syy) : 0.0;
    return LinearFit{.slope = sxy / sxx, .r_squared = r_squared};
}

FindingSeverity severity_for_slope(double mib_per_s) noexcept
{
    if (mib_per_s >= 5.0)
    {
        return FindingSeverity::High;
    }
    if (mib_per_s >= 1.0)
    {
        return FindingSeverity::Medium;
    }
    return FindingSeverity::Low;
}

void add_growth_finding(EvidenceLedger& ledger, std::string_view metric, std::string rule,
                        Fields params, const std::string& evidence_id, double mib_per_s,
                        double duration_s, double growth_bytes, std::optional<double> r_squared,
                        FrameRange range)
{
    Finding fd;
    fd.kind = FindingKind::MemoryGrowth;
    fd.severity = severity_for_slope(mib_per_s);
    fd.level = EvidenceLevel::Hypothesis;
    fd.frame_range = range;
    fd.title = std::format("{} grew steadily at {:.2f} MiB/s over {:.1f} s", metric, mib_per_s,
                           duration_s);
    fd.metrics = {
        {"slope_mib_per_s", mib_per_s}, {"duration_s", duration_s}, {"growth_bytes", growth_bytes}};
    if (r_squared)
    {
        fd.metrics.emplace_back("r_squared", *r_squared);
    }
    fd.evidence_ids = {evidence_id};
    fd.rule = std::move(rule);
    fd.rule_params = std::move(params);
    // 持续增长也可能是缓存或资源流式加载，只有快照对比才能确认泄漏
    fd.missing_evidence = {"memory snapshot comparison showing which objects are retained"};
    ledger.add_finding(std::move(fd));
}

} // namespace

void analyze_memory(std::span<const FrameRecord> frames, const MemoryConfig& config,
                    EvidenceLedger& ledger)
{
    std::vector<std::size_t> collections;
    std::optional<std::int32_t> previous;
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        if (!frames[i].gc_collection_count)
        {
            continue;
        }
        if (previous && *frames[i].gc_collection_count > *previous)
        {
            collections.push_back(i);
        }
        previous = frames[i].gc_collection_count;
    }

    const std::pair<std::string_view, std::optional<std::int64_t> FrameRecord::*> series[] = {
        {"gc_used_bytes", &FrameRecord::gc_used_bytes},
        {"total_used_bytes", &FrameRecord::total_used_bytes},
    };
    for (const auto& [metric, member] : series)
    {
        std::vector<double> t_s;
        std::vector<double> bytes;
        std::uint64_t first_frame = 0;
        std::uint64_t last_frame = 0;
        for (const FrameRecord& f : frames)
        {
            if (const auto& value = f.*member)
            {
                if (t_s.empty())
                {
                    first_frame = f.frame_index;
                }
                last_frame = f.frame_index;
                t_s.push_back(static_cast<double>(f.timestamp_ns) / 1e9);
                bytes.push_back(static_cast<double>(*value));
            }
        }
        if (t_s.size() < 2)
        {
            continue;
        }
        const double duration = t_s.back() - t_s.front();
        const FrameRange range{.first = first_frame, .last = last_frame};

        if (collections.empty())
        {
            // 没有回收时托管堆只涨不跌是正常的，所以同时要求斜率、拟合度和时长
            const auto fit = fit_line(t_s, bytes);
            if (!fit)
            {
                continue;
            }
            const double mib_per_s = fit->slope / kMiB;
            if (duration < config.min_duration_s || mib_per_s < config.min_slope_mib_per_s ||
                fit->r_squared < config.min_r_squared)
            {
                continue;
            }
            const std::string id =
                ledger.add_evidence(EvidenceType::Statistic,
                                    std::format("least-squares fit of {} over time", metric), range,
                                    {{"metric", as_text(std::string(metric))},
                                     {"slope_bytes_per_s", as_real(fit->slope)},
                                     {"r_squared", as_real(fit->r_squared)},
                                     {"duration_s", as_real(duration)},
                                     {"start_bytes", as_real(bytes.front())},
                                     {"end_bytes", as_real(bytes.back())},
                                     {"samples", as_int(static_cast<std::int64_t>(bytes.size()))}});
            add_growth_finding(ledger, metric, "memory.linear_growth",
                               {{"min_slope_mib_per_s", as_real(config.min_slope_mib_per_s)},
                                {"min_r_squared", as_real(config.min_r_squared)},
                                {"min_duration_s", as_real(config.min_duration_s)}},
                               id, mib_per_s, duration, bytes.back() - bytes.front(),
                               fit->r_squared, range);
            continue;
        }

        // 有回收时只看托管堆每次回收后的基线，锯齿本身不算增长
        if (metric != "gc_used_bytes" || collections.size() < config.min_collections)
        {
            continue;
        }
        std::vector<double> levels;
        std::vector<double> level_t;
        for (const std::size_t i : collections)
        {
            if (const auto& value = frames[i].*member)
            {
                levels.push_back(static_cast<double>(*value));
                level_t.push_back(static_cast<double>(frames[i].timestamp_ns) / 1e9);
            }
        }
        if (levels.size() < config.min_collections)
        {
            continue;
        }
        bool rising = true;
        for (std::size_t i = 1; i < levels.size(); ++i)
        {
            rising = rising && levels[i] > levels[i - 1];
        }
        const double span_s = level_t.back() - level_t.front();
        const double mib_per_s =
            span_s > 0.0 ? (levels.back() - levels.front()) / span_s / kMiB : 0.0;
        if (!rising || span_s < config.min_duration_s || mib_per_s < config.min_slope_mib_per_s)
        {
            continue;
        }
        const std::string id =
            ledger.add_evidence(EvidenceType::Statistic,
                                "gc_used_bytes right after each GC collection kept rising", range,
                                {{"metric", as_text("gc_used_bytes")},
                                 {"collections", as_int(static_cast<std::int64_t>(levels.size()))},
                                 {"first_post_gc_bytes", as_real(levels.front())},
                                 {"last_post_gc_bytes", as_real(levels.back())},
                                 {"duration_s", as_real(span_s)}});
        add_growth_finding(
            ledger, metric, "memory.post_gc_baseline",
            {{"min_collections", as_int(static_cast<std::int64_t>(config.min_collections))},
             {"min_slope_mib_per_s", as_real(config.min_slope_mib_per_s)}},
            id, mib_per_s, span_s, levels.back() - levels.front(), std::nullopt, range);
    }
}

} // namespace gpia::analysis
