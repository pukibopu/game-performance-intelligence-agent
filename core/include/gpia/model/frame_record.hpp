#pragma once

#include <cstdint>
#include <optional>

namespace gpia
{

struct FrameRecord
{
    // ---------- required ----------
    std::uint64_t frame_index = 0;
    std::int64_t timestamp_ns = 0;
    double frame_time_ms = 0.0;

    // ---------- FrameTimingManager ----------
    std::optional<double> cpu_main_thread_ms;
    std::optional<double> cpu_render_thread_ms;
    std::optional<double> cpu_present_wait_ms;
    std::optional<double> gpu_frame_time_ms;

    // ---------- heap / GC ----------
    std::optional<std::int64_t> gc_alloc_bytes; // Development builds only
    std::optional<std::int64_t> gc_used_bytes;
    std::optional<std::int64_t> gc_reserved_bytes;
    std::optional<std::int32_t> gc_collection_count; // cumulative; analyze the delta

    // ---------- process memory ----------
    std::optional<std::int64_t> total_used_bytes;
    std::optional<std::int64_t> total_reserved_bytes;
    std::optional<std::int64_t> system_used_bytes;

    // ---------- rendering counters ----------
    std::optional<std::int32_t> draw_calls;
    std::optional<std::int32_t> setpass_calls;
    std::optional<std::int32_t> batches;
    std::optional<std::int64_t> triangles;
    std::optional<std::int64_t> vertices;

    // ---------- ground truth ----------
    std::uint32_t scenario_marker = 0; // 0 = no injected workload

    friend bool operator==(const FrameRecord&, const FrameRecord&) = default;
};

} // namespace gpia
