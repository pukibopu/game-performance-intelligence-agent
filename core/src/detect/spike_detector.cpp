#include "gpia/detect/spike_detector.hpp"

#include <algorithm>
#include <cmath>

#include "gpia/stats/summary.hpp"

namespace gpia::detect
{

namespace
{

// 正态分布下 MAD 与标准差的换算系数
constexpr double kMadToSigma = 1.4826;

} // namespace

std::vector<Spike> fixed_threshold(std::span<const double> values, double threshold)
{
    std::vector<Spike> spikes;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] > threshold)
        {
            spikes.push_back(
                {.index = i, .value = values[i], .baseline = threshold, .threshold = threshold});
        }
    }
    return spikes;
}

std::vector<Spike> percentile_spikes(std::span<const double> values, double q, double k,
                                     double floor)
{
    if (values.empty())
    {
        return {};
    }
    std::vector<double> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    const double baseline = stats::percentile_sorted(sorted, q);
    const double threshold = std::max(floor, k * baseline);

    std::vector<Spike> spikes;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] > threshold)
        {
            spikes.push_back(
                {.index = i, .value = values[i], .baseline = baseline, .threshold = threshold});
        }
    }
    return spikes;
}

std::vector<Spike> rolling_median_spikes(std::span<const double> values, std::size_t window,
                                         double k, double floor)
{
    std::vector<Spike> spikes;
    const std::size_t half = window / 2;
    std::vector<double> buffer;
    buffer.reserve(window + 1);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        // 窗口以当前帧为中心，靠近两端时截断
        const std::size_t begin = i > half ? i - half : 0;
        const std::size_t end = std::min(values.size(), i + half + 1);
        buffer.assign(values.begin() + static_cast<std::ptrdiff_t>(begin),
                      values.begin() + static_cast<std::ptrdiff_t>(end));
        const double baseline = stats::median_inplace(buffer);
        const double threshold = std::max(floor, k * baseline);
        if (values[i] > threshold)
        {
            spikes.push_back(
                {.index = i, .value = values[i], .baseline = baseline, .threshold = threshold});
        }
    }
    return spikes;
}

std::vector<Spike> mad_spikes(std::span<const double> values, double k, double floor)
{
    if (values.empty())
    {
        return {};
    }
    std::vector<double> work(values.begin(), values.end());
    const double median = stats::median_inplace(work);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        work[i] = std::abs(values[i] - median);
    }
    const double mad = stats::median_inplace(work);
    const double threshold = median + std::max(k * kMadToSigma * mad, floor);

    std::vector<Spike> spikes;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] > threshold)
        {
            spikes.push_back(
                {.index = i, .value = values[i], .baseline = median, .threshold = threshold});
        }
    }
    return spikes;
}

} // namespace gpia::detect
