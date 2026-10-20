#include "gpia/stats/summary.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace gpia::stats
{

void RunningStats::add(double value) noexcept
{
    ++count_;
    const double delta = value - mean_;
    mean_ += delta / static_cast<double>(count_);
    m2_ += delta * (value - mean_);
    min_ = count_ == 1 ? value : std::min(min_, value);
    max_ = count_ == 1 ? value : std::max(max_, value);
}

std::optional<double> RunningStats::mean() const noexcept
{
    return count_ > 0 ? std::optional<double>{mean_} : std::nullopt;
}

std::optional<double> RunningStats::sample_stddev() const noexcept
{
    if (count_ < 2)
    {
        return std::nullopt;
    }
    return std::sqrt(m2_ / static_cast<double>(count_ - 1));
}

std::optional<double> RunningStats::min() const noexcept
{
    return count_ > 0 ? std::optional<double>{min_} : std::nullopt;
}

std::optional<double> RunningStats::max() const noexcept
{
    return count_ > 0 ? std::optional<double>{max_} : std::nullopt;
}

double percentile_sorted(std::span<const double> sorted, double q) noexcept
{
    assert(!sorted.empty());
    const double h = static_cast<double>(sorted.size() - 1) * std::clamp(q, 0.0, 1.0);
    const auto lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (h - static_cast<double>(lo));
}

double median_inplace(std::span<double> values) noexcept
{
    assert(!values.empty());
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid),
                     values.end());
    const double upper = values[mid];
    if (values.size() % 2 == 1)
    {
        return upper;
    }
    // 偶数个时取两个中间值的平均，下中位数是左半部分的最大值
    const double lower =
        *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
    return (lower + upper) / 2.0;
}

Summary summarize(std::span<const double> values, std::size_t missing)
{
    Summary summary;
    summary.count = values.size();
    summary.missing = missing;
    if (values.empty())
    {
        return summary;
    }

    RunningStats running;
    for (const double v : values)
    {
        running.add(v);
    }
    summary.mean = running.mean();
    summary.stddev = running.sample_stddev();
    summary.min = running.min();
    summary.max = running.max();

    std::vector<double> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    summary.p50 = percentile_sorted(sorted, 0.50);
    summary.p95 = percentile_sorted(sorted, 0.95);
    summary.p99 = percentile_sorted(sorted, 0.99);
    return summary;
}

std::vector<double> moving_average(std::span<const double> values, std::size_t window)
{
    std::vector<double> result;
    result.reserve(values.size());
    const std::size_t w = std::max<std::size_t>(window, 1);
    double sum = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        sum += values[i];
        if (i >= w)
        {
            sum -= values[i - w];
        }
        result.push_back(sum / static_cast<double>(std::min(i + 1, w)));
    }
    return result;
}

Histogram histogram(std::span<const double> values, double origin, double bin_width,
                    std::size_t bins)
{
    Histogram h{.origin = origin, .bin_width = bin_width, .counts = std::vector<std::size_t>(bins)};
    for (const double v : values)
    {
        if (v < origin)
        {
            ++h.underflow;
            continue;
        }
        const double position = (v - origin) / bin_width;
        if (position >= static_cast<double>(bins))
        {
            ++h.overflow;
            continue;
        }
        ++h.counts[static_cast<std::size_t>(position)];
    }
    return h;
}

} // namespace gpia::stats
