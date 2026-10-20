#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "gpia/diagnostics.hpp"
#include "gpia/result.hpp"

namespace gpia
{

struct HitchConfig
{
    std::size_t window = 61;
    double k = 2.0;
    std::optional<double> floor_ms; // 为空时取目标帧时间的 1.5 倍，帧率不限时为 0
};

struct CpuConfig
{
    std::size_t window = 61;
    double k = 2.0;
};

struct GcAllocConfig
{
    double k = 5.0;
    double min_bytes = 1024.0 * 1024.0;
};

struct MemoryConfig
{
    double min_slope_mib_per_s = 0.5;
    double min_r_squared = 0.8;
    double min_duration_s = 10.0;
    std::size_t min_collections = 3; // 用 GC 后的基线判断增长时至少需要的回收次数
};

struct AnalysisConfig
{
    std::size_t min_frames = 30;
    HitchConfig hitch;
    CpuConfig cpu;
    GcAllocConfig gc_alloc;
    MemoryConfig memory;
};

// 从 JSON 文本读取配置，只覆盖出现的键，未知键记警告，类型错误致命
[[nodiscard]] Result<AnalysisConfig> parse_analysis_config(std::string_view json_text,
                                                           Diagnostics& diagnostics);

// 序列化为规范化的 JSON 文本，写进报告的 analyzer.config
[[nodiscard]] std::string to_json_text(const AnalysisConfig& config);

} // namespace gpia
