#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace gpia::detect
{

// 一个被判定为尖峰的样本，index 指向输入序列中的位置
struct Spike
{
    std::size_t index = 0;
    double value = 0.0;
    double baseline = 0.0;
    double threshold = 0.0;
};

// 超过固定阈值即为尖峰，O(n)，阈值依赖帧预算
[[nodiscard]] std::vector<Spike> fixed_threshold(std::span<const double> values, double threshold);

// 超过 max(floor, k 倍的 q 分位数) 即为尖峰，O(n log n)
[[nodiscard]] std::vector<Spike> percentile_spikes(std::span<const double> values, double q,
                                                   double k, double floor);

// 超过 max(floor, k 倍的居中滚动中位数) 即为尖峰，朴素实现为 O(n·w)
[[nodiscard]] std::vector<Spike> rolling_median_spikes(std::span<const double> values,
                                                       std::size_t window, double k, double floor);

// 超过 median + max(k·1.4826·MAD, floor) 即为尖峰，MAD 为 0 时由 floor 兜底
[[nodiscard]] std::vector<Spike> mad_spikes(std::span<const double> values, double k, double floor);

} // namespace gpia::detect
