#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace gpia::stats
{

// Welford 单遍算法，数值稳定且不需要保存全部样本
class RunningStats
{
  public:
    void add(double value) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::optional<double> mean() const noexcept;
    [[nodiscard]] std::optional<double> sample_stddev() const noexcept;
    [[nodiscard]] std::optional<double> min() const noexcept;
    [[nodiscard]] std::optional<double> max() const noexcept;

  private:
    std::size_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
};

// 输入必须已排序且非空，q 取 0 到 1，结果与 numpy 默认的线性插值一致
[[nodiscard]] double percentile_sorted(std::span<const double> sorted, double q) noexcept;

// 会打乱输入顺序，平均 O(n)，输入必须非空
[[nodiscard]] double median_inplace(std::span<double> values) noexcept;

struct Summary
{
    std::size_t count = 0;
    std::size_t missing = 0;
    std::optional<double> mean;
    std::optional<double> stddev; // 样本标准差，少于 2 个值时为空
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> p50;
    std::optional<double> p95;
    std::optional<double> p99;
};

// 汇总一列数值，空列只给出计数而不编造统计量
[[nodiscard]] Summary summarize(std::span<const double> values, std::size_t missing = 0);

// 以当前帧结尾的窗口均值，开头不足一个窗口时用已有的帧
[[nodiscard]] std::vector<double> moving_average(std::span<const double> values,
                                                 std::size_t window);

struct Histogram
{
    double origin = 0.0;
    double bin_width = 1.0;
    std::vector<std::size_t> counts;
    std::size_t underflow = 0;
    std::size_t overflow = 0;
};

// 固定宽度的直方图，落在范围外的值分别计入下溢和上溢
[[nodiscard]] Histogram histogram(std::span<const double> values, double origin, double bin_width,
                                  std::size_t bins);

} // namespace gpia::stats
