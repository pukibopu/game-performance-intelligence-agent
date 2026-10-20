#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "analyzers.hpp"

#include "gpia/detect/spike_detector.hpp"

namespace gpia::analysis
{

namespace
{

constexpr double kMiB = 1024.0 * 1024.0;
constexpr double kMarkerShare = 0.5;

FindingSeverity severity_for_bytes(double bytes) noexcept
{
    if (bytes >= 16.0 * kMiB)
    {
        return FindingSeverity::High;
    }
    if (bytes >= 4.0 * kMiB)
    {
        return FindingSeverity::Medium;
    }
    return FindingSeverity::Low;
}

void report_missing_column(EvidenceLedger& ledger, std::string_view column, std::string title,
                           std::string rule, std::string missing)
{
    const std::string id = ledger.add_evidence(
        EvidenceType::DataAvailability,
        std::format("{} has no values in the analyzed frames", column), std::nullopt,
        {{"column", as_text(std::string(column))}, {"available_frames", as_int(0)}});
    Finding fd;
    fd.kind = FindingKind::InsufficientData;
    fd.severity = FindingSeverity::Info;
    fd.level = EvidenceLevel::InsufficientEvidence;
    fd.title = std::move(title);
    fd.evidence_ids = {id};
    fd.rule = std::move(rule);
    fd.missing_evidence = {std::move(missing)};
    ledger.add_finding(std::move(fd));
}

void analyze_allocations(std::span<const FrameRecord> frames, const GcAllocConfig& config,
                         EvidenceLedger& ledger)
{
    std::vector<double> alloc;
    std::vector<std::uint64_t> alloc_frame;
    for (const FrameRecord& f : frames)
    {
        if (f.gc_alloc_bytes)
        {
            alloc.push_back(static_cast<double>(*f.gc_alloc_bytes));
            alloc_frame.push_back(f.frame_index);
        }
    }
    if (alloc.empty())
    {
        report_missing_column(
            ledger, "gc_alloc_bytes", "Per-frame managed allocation was not recorded",
            "gc_alloc.availability",
            "gc_alloc_bytes per frame, which Unity only exposes in Development builds");
        return;
    }

    struct AllocEvent
    {
        std::uint64_t frame = 0;
        double bytes = 0.0;
        double threshold = 0.0;
        std::string id;
    };
    std::vector<AllocEvent> events;
    for (const auto& s : detect::mad_spikes(alloc, config.k, config.min_bytes))
    {
        const std::uint64_t frame = alloc_frame[s.index];
        std::string id =
            ledger.add_evidence(EvidenceType::ThresholdCrossing,
                                "gc_alloc_bytes exceeded median + max(k x 1.4826 x MAD, min_bytes)",
                                FrameRange{.first = frame, .last = frame},
                                {{"metric", as_text("gc_alloc_bytes")},
                                 {"value", as_real(s.value)},
                                 {"baseline", as_real(s.baseline)},
                                 {"threshold", as_real(s.threshold)}});
        events.push_back(
            {.frame = frame, .bytes = s.value, .threshold = s.threshold, .id = std::move(id)});
    }

    for (const auto& [begin, end] :
         group_consecutive(events, [](const AllocEvent& e) { return e.frame; }))
    {
        double total = 0.0;
        double peak = 0.0;
        Finding fd;
        for (std::size_t i = begin; i <= end; ++i)
        {
            total += events[i].bytes;
            peak = std::max(peak, events[i].bytes);
            fd.evidence_ids.push_back(events[i].id);
        }
        fd.kind = FindingKind::GcAllocSpike;
        fd.severity = severity_for_bytes(peak);
        fd.level = EvidenceLevel::DeterministicFinding;
        fd.frame_range = FrameRange{.first = events[begin].frame, .last = events[end].frame};
        fd.title = begin == end ? std::format("Frame {} allocated {:.1f} MiB of managed memory",
                                              events[begin].frame, total / kMiB)
                                : std::format("Frames {}-{} allocated {:.1f} MiB of managed memory",
                                              events[begin].frame, events[end].frame, total / kMiB);
        fd.metrics = {{"gc_alloc_bytes", total},
                      {"peak_frame_bytes", peak},
                      {"threshold_bytes", events[begin].threshold}};
        fd.rule = "gc_alloc.mad";
        fd.rule_params = {{"k", as_real(config.k)}, {"min_bytes", as_real(config.min_bytes)}};
        ledger.add_finding(std::move(fd));
    }
}

} // namespace

void analyze_gc(std::span<const FrameRecord> frames, const GcAllocConfig& config,
                const std::vector<HitchEvent>& hitches, EvidenceLedger& ledger)
{
    analyze_allocations(frames, config, ledger);

    // 回收次数是累计值，相邻两个有值的帧之间增加即视为发生了回收
    std::unordered_map<std::uint64_t, std::string> collection_evidence;
    std::unordered_map<std::uint64_t, std::size_t> position;
    std::optional<std::int32_t> previous_count;
    std::uint64_t previous_frame = 0;
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const FrameRecord& f = frames[i];
        position[f.frame_index] = i;
        if (!f.gc_collection_count)
        {
            continue;
        }
        if (previous_count && *f.gc_collection_count > *previous_count)
        {
            const std::int64_t delta = *f.gc_collection_count - *previous_count;
            collection_evidence[f.frame_index] = ledger.add_evidence(
                EvidenceType::CounterDelta,
                std::format("gc_collection_count increased by {} between frames {} and {}", delta,
                            previous_frame, f.frame_index),
                FrameRange{.first = previous_frame, .last = f.frame_index},
                {{"metric", as_text("gc_collection_count")},
                 {"from", as_int(*previous_count)},
                 {"to", as_int(*f.gc_collection_count)},
                 {"delta", as_int(delta)}});
        }
        previous_count = f.gc_collection_count;
        previous_frame = f.frame_index;
    }

    if (hitches.empty())
    {
        return;
    }
    const bool have_marker = std::ranges::any_of(
        frames, [](const FrameRecord& f) { return f.gc_collect_ms.has_value(); });
    if (!previous_count && !have_marker)
    {
        report_missing_column(ledger, "gc_collection_count",
                              "GC involvement in hitches cannot be assessed",
                              "gc.collection_availability", "gc_collection_count per frame");
        return;
    }

    std::optional<std::string> marker_note;
    for (const HitchEvent& h : hitches)
    {
        const std::size_t pos = position.at(h.frame_index);
        const FrameRecord& f = frames[pos];
        const auto collected = collection_evidence.find(h.frame_index);
        const bool has_collection = collected != collection_evidence.end();
        const double gc_ms = f.gc_collect_ms.value_or(0.0);
        if (!has_collection && gc_ms <= 0.0)
        {
            continue;
        }

        Finding fd;
        fd.kind = FindingKind::GcCollectionHitch;
        fd.severity = FindingSeverity::Medium;
        fd.frame_range = FrameRange{.first = h.frame_index, .last = h.frame_index};
        fd.metrics = {{"frame_time_ms", h.frame_time_ms}, {"baseline_ms", h.baseline_ms}};
        fd.evidence_ids = {h.evidence_id};
        if (has_collection)
        {
            fd.evidence_ids.push_back(collected->second);
        }
        fd.rule = "gc.collection_hitch_same_frame";

        // GC.Collect 实测耗时占到超额帧时间的一半以上，才把相关性升级为有证据的诊断
        const double excess_ms = std::max(0.0, h.frame_time_ms - h.baseline_ms);
        if (f.gc_collect_ms && gc_ms > 0.0 && excess_ms > 0.0 && gc_ms >= kMarkerShare * excess_ms)
        {
            fd.level = EvidenceLevel::EvidenceBackedDiagnosis;
            fd.title = std::format("Frame {} hitch is mostly a blocking GC.Collect of {:.1f} ms",
                                   h.frame_index, gc_ms);
            fd.metrics.emplace_back("gc_collect_ms", gc_ms);
            fd.metrics.emplace_back("gc_share_of_excess", gc_ms / excess_ms);
            fd.evidence_ids.push_back(ledger.add_evidence(
                EvidenceType::ThresholdCrossing,
                std::format("GC.Collect took {:.1f} ms of the {:.1f} ms above the rolling median",
                            gc_ms, excess_ms),
                FrameRange{.first = h.frame_index, .last = h.frame_index},
                {{"metric", as_text("gc_collect_ms")},
                 {"value", as_real(gc_ms)},
                 {"excess_ms", as_real(excess_ms)},
                 {"threshold", as_real(kMarkerShare * excess_ms)}}));
            fd.rule = "gc.collect_marker_share";
            fd.rule_params = {{"min_share_of_excess", as_real(kMarkerShare)}};
            ledger.add_finding(std::move(fd));
            continue;
        }

        // 同帧只说明相关，没有足够的 GC.Collect 耗时就只能停在假设
        fd.level = EvidenceLevel::Hypothesis;
        fd.title = std::format("Frame {} hitch coincides with a GC collection", h.frame_index);

        if (pos > 0 && f.gc_used_bytes && frames[pos - 1].gc_used_bytes &&
            *f.gc_used_bytes < *frames[pos - 1].gc_used_bytes)
        {
            const std::int64_t before = *frames[pos - 1].gc_used_bytes;
            const std::int64_t after = *f.gc_used_bytes;
            fd.evidence_ids.push_back(ledger.add_evidence(
                EvidenceType::CounterDelta,
                std::format("gc_used_bytes dropped between frames {} and {}",
                            frames[pos - 1].frame_index, h.frame_index),
                FrameRange{.first = frames[pos - 1].frame_index, .last = h.frame_index},
                {{"metric", as_text("gc_used_bytes")},
                 {"from", as_int(before)},
                 {"to", as_int(after)},
                 {"delta", as_int(after - before)}}));
            fd.metrics.emplace_back("freed_bytes", static_cast<double>(before - after));
        }
        if (f.gc_collect_ms)
        {
            fd.metrics.emplace_back("gc_collect_ms", gc_ms);
            fd.evidence_ids.push_back(ledger.add_evidence(
                EvidenceType::Statistic,
                std::format(
                    "GC.Collect took only {:.1f} ms of the {:.1f} ms above the rolling median",
                    gc_ms, excess_ms),
                FrameRange{.first = h.frame_index, .last = h.frame_index},
                {{"metric", as_text("gc_collect_ms")},
                 {"value", as_real(gc_ms)},
                 {"excess_ms", as_real(excess_ms)}}));
            fd.missing_evidence = {std::format(
                "what else ran during frame {}, for example a profiler capture", h.frame_index)};
        }
        else
        {
            if (!marker_note)
            {
                marker_note = ledger.add_evidence(
                    EvidenceType::DataAvailability,
                    "GC.Collect marker duration is not recorded in this run", std::nullopt,
                    {{"source", as_text("GC.Collect marker")},
                     {"status", as_text("not_recorded")}});
            }
            fd.evidence_ids.push_back(*marker_note);
            fd.missing_evidence = {
                std::format("GC.Collect marker duration for frame {}", h.frame_index)};
        }
        if (!f.cpu_main_thread_ms)
        {
            fd.evidence_ids.push_back(ledger.add_evidence(
                EvidenceType::DataAvailability,
                std::format("cpu_main_thread_ms is missing for frame {}", h.frame_index),
                FrameRange{.first = h.frame_index, .last = h.frame_index},
                {{"column", as_text("cpu_main_thread_ms")}}));
            fd.missing_evidence.push_back(
                std::format("cpu_main_thread_ms for frame {}", h.frame_index));
        }
        ledger.add_finding(std::move(fd));
    }
}

} // namespace gpia::analysis
