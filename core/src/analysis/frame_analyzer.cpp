#include <format>
#include <optional>

#include "analyzers.hpp"

#include "gpia/detect/spike_detector.hpp"

namespace gpia::analysis
{

namespace
{

FindingSeverity severity_for_ratio(double ratio) noexcept
{
    if (ratio >= 4.0)
    {
        return FindingSeverity::High;
    }
    if (ratio >= 2.5)
    {
        return FindingSeverity::Medium;
    }
    return FindingSeverity::Low;
}

std::string frames_label(std::uint64_t first, std::uint64_t last)
{
    return first == last ? std::format("Frame {}", first)
                         : std::format("Frames {}-{}", first, last);
}

double ratio_of(double value, double baseline) noexcept
{
    return baseline > 0.0 ? value / baseline : 0.0;
}

// 只有份额足够大时才在标题里点名占用最多的一方，避免过度解读
std::string busy_detail(const FrameRecord& frame, double frame_ms)
{
    if (frame.cpu_main_thread_ms && *frame.cpu_main_thread_ms / frame_ms >= 0.75)
    {
        return std::format(", main thread busy for {:.0f}% of it",
                           *frame.cpu_main_thread_ms / frame_ms * 100.0);
    }
    if (frame.gpu_frame_time_ms && *frame.gpu_frame_time_ms / frame_ms >= 0.75)
    {
        return std::format(", GPU busy for {:.0f}% of it",
                           *frame.gpu_frame_time_ms / frame_ms * 100.0);
    }
    return {};
}

} // namespace

std::vector<HitchEvent> analyze_hitches(std::span<const FrameRecord> frames,
                                        const HitchConfig& config, double floor_ms,
                                        EvidenceLedger& ledger)
{
    std::vector<double> frame_ms;
    frame_ms.reserve(frames.size());
    for (const FrameRecord& f : frames)
    {
        frame_ms.push_back(f.frame_time_ms);
    }

    std::vector<HitchEvent> hitches;
    std::vector<std::size_t> positions;
    for (const auto& s : detect::rolling_median_spikes(frame_ms, config.window, config.k, floor_ms))
    {
        const FrameRecord& f = frames[s.index];
        std::string id =
            ledger.add_evidence(EvidenceType::ThresholdCrossing,
                                "frame_time_ms exceeded max(floor_ms, k x rolling median)",
                                FrameRange{.first = f.frame_index, .last = f.frame_index},
                                {{"metric", as_text("frame_time_ms")},
                                 {"value", as_real(s.value)},
                                 {"baseline", as_real(s.baseline)},
                                 {"threshold", as_real(s.threshold)}});
        hitches.push_back({.frame_index = f.frame_index,
                           .frame_time_ms = s.value,
                           .baseline_ms = s.baseline,
                           .evidence_id = std::move(id)});
        positions.push_back(s.index);
    }

    const auto frame_of = [](const HitchEvent& h) { return h.frame_index; };
    for (const auto& [begin, end] : group_consecutive(hitches, frame_of))
    {
        // 每组取相对基线最严重的一帧作为代表
        std::size_t worst = begin;
        for (std::size_t i = begin; i <= end; ++i)
        {
            if (ratio_of(hitches[i].frame_time_ms, hitches[i].baseline_ms) >
                ratio_of(hitches[worst].frame_time_ms, hitches[worst].baseline_ms))
            {
                worst = i;
            }
        }
        const HitchEvent& w = hitches[worst];
        const FrameRecord& wf = frames[positions[worst]];
        const double ratio = ratio_of(w.frame_time_ms, w.baseline_ms);

        Finding fd;
        fd.kind = FindingKind::FrameHitch;
        fd.severity = severity_for_ratio(ratio);
        fd.level = EvidenceLevel::DeterministicFinding;
        fd.frame_range =
            FrameRange{.first = hitches[begin].frame_index, .last = hitches[end].frame_index};
        fd.title = std::format("{} took {:.2f}x the rolling median frame time{}",
                               frames_label(fd.frame_range->first, fd.frame_range->last), ratio,
                               busy_detail(wf, w.frame_time_ms));
        fd.metrics = {{"frame_time_ms", w.frame_time_ms},
                      {"baseline_ms", w.baseline_ms},
                      {"ratio_to_baseline", ratio}};
        if (wf.cpu_main_thread_ms)
        {
            fd.metrics.emplace_back("cpu_main_thread_ms", *wf.cpu_main_thread_ms);
            fd.metrics.emplace_back("main_thread_share", *wf.cpu_main_thread_ms / w.frame_time_ms);
        }
        if (wf.gpu_frame_time_ms)
        {
            fd.metrics.emplace_back("gpu_frame_time_ms", *wf.gpu_frame_time_ms);
        }
        for (std::size_t i = begin; i <= end; ++i)
        {
            fd.evidence_ids.push_back(hitches[i].evidence_id);
        }
        fd.rule = "hitch.rolling_median";
        fd.rule_params = {{"window", as_int(static_cast<std::int64_t>(config.window))},
                          {"k", as_real(config.k)},
                          {"floor_ms", as_real(floor_ms)}};
        ledger.add_finding(std::move(fd));
    }
    return hitches;
}

void analyze_cpu(std::span<const FrameRecord> frames, const CpuConfig& config,
                 EvidenceLedger& ledger)
{
    std::vector<double> values;
    std::vector<std::uint64_t> frame_of;
    for (const FrameRecord& f : frames)
    {
        if (f.cpu_main_thread_ms)
        {
            values.push_back(*f.cpu_main_thread_ms);
            frame_of.push_back(f.frame_index);
        }
    }
    if (values.empty())
    {
        const std::string id = ledger.add_evidence(
            EvidenceType::DataAvailability,
            "cpu_main_thread_ms has no values in the analyzed frames", std::nullopt,
            {{"column", as_text("cpu_main_thread_ms")}, {"available_frames", as_int(0)}});
        Finding fd;
        fd.kind = FindingKind::InsufficientData;
        fd.severity = FindingSeverity::Info;
        fd.level = EvidenceLevel::InsufficientEvidence;
        fd.title =
            "Main-thread CPU time was not recorded, so hitches cannot be split into CPU or GPU";
        fd.evidence_ids = {id};
        fd.rule = "cpu.availability";
        fd.missing_evidence = {"cpu_main_thread_ms per frame from FrameTimingManager"};
        ledger.add_finding(std::move(fd));
        return;
    }

    struct CpuEvent
    {
        std::uint64_t frame = 0;
        double value = 0.0;
        double baseline = 0.0;
        std::string id;
    };
    std::vector<CpuEvent> events;
    for (const auto& s : detect::rolling_median_spikes(values, config.window, config.k,
                                                       config.floor_ms.value_or(0.0)))
    {
        const std::uint64_t frame = frame_of[s.index];
        std::string id = ledger.add_evidence(EvidenceType::ThresholdCrossing,
                                             "cpu_main_thread_ms exceeded k x rolling median",
                                             FrameRange{.first = frame, .last = frame},
                                             {{"metric", as_text("cpu_main_thread_ms")},
                                              {"value", as_real(s.value)},
                                              {"baseline", as_real(s.baseline)},
                                              {"threshold", as_real(s.threshold)}});
        events.push_back(
            {.frame = frame, .value = s.value, .baseline = s.baseline, .id = std::move(id)});
    }

    for (const auto& [begin, end] :
         group_consecutive(events, [](const CpuEvent& e) { return e.frame; }))
    {
        std::size_t worst = begin;
        for (std::size_t i = begin; i <= end; ++i)
        {
            if (events[i].value > events[worst].value)
            {
                worst = i;
            }
        }
        const CpuEvent& w = events[worst];
        const double ratio = ratio_of(w.value, w.baseline);
        Finding fd;
        fd.kind = FindingKind::CpuSpike;
        fd.severity = severity_for_ratio(ratio);
        fd.level = EvidenceLevel::DeterministicFinding;
        fd.frame_range = FrameRange{.first = events[begin].frame, .last = events[end].frame};
        fd.title = std::format("{} main-thread CPU time was {:.2f}x its rolling median",
                               frames_label(fd.frame_range->first, fd.frame_range->last), ratio);
        fd.metrics = {{"cpu_main_thread_ms", w.value},
                      {"baseline_ms", w.baseline},
                      {"ratio_to_baseline", ratio}};
        for (std::size_t i = begin; i <= end; ++i)
        {
            fd.evidence_ids.push_back(events[i].id);
        }
        fd.rule = "cpu.rolling_median";
        fd.rule_params = {{"window", as_int(static_cast<std::int64_t>(config.window))},
                          {"k", as_real(config.k)},
                          {"floor_ms", as_real(config.floor_ms.value_or(0.0))}};
        ledger.add_finding(std::move(fd));
    }
}

} // namespace gpia::analysis
