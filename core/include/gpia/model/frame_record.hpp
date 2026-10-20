#pragma once

#include <cstdint>
#include <optional>

namespace gpia
{

// frames.csv 的一行，字段名即列名，缺失值为空且绝不补零
struct FrameRecord
{
    // ---------- 必需字段 ----------
    std::uint64_t frame_index = 0;
    std::int64_t timestamp_ns = 0;
    double frame_time_ms = 0.0;

    // ---------- FrameTimingManager 帧时间，已按 4 帧延迟回填 ----------
    std::optional<double> cpu_main_thread_ms;
    std::optional<double> cpu_render_thread_ms;
    std::optional<double> cpu_present_wait_ms;
    std::optional<double> gpu_frame_time_ms;

    // ---------- 托管堆与 GC ----------
    std::optional<std::int64_t> gc_alloc_bytes; // 仅 Development 构建可用
    std::optional<std::int64_t> gc_used_bytes;
    std::optional<std::int64_t> gc_reserved_bytes;
    std::optional<std::int32_t> gc_collection_count; // 累计值，分析时取差值

    // ---------- 进程内存 ----------
    std::optional<std::int64_t> total_used_bytes;
    std::optional<std::int64_t> total_reserved_bytes;
    std::optional<std::int64_t> system_used_bytes;

    // ---------- 渲染计数 ----------
    std::optional<std::int32_t> draw_calls;
    std::optional<std::int32_t> setpass_calls;
    std::optional<std::int32_t> batches;
    std::optional<std::int64_t> triangles;
    std::optional<std::int64_t> vertices;

    // ---------- 场景注入标记 ----------
    std::uint32_t scenario_marker = 0; // 0 表示本帧未注入负载

    friend bool operator==(const FrameRecord&, const FrameRecord&) = default;
};

} // namespace gpia
