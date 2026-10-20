#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/run_loader.hpp"
#include "gpia/stats/summary.hpp"

namespace fs = std::filesystem;
namespace stats = gpia::stats;

namespace
{

const fs::path kTestData{GPIA_TESTDATA_DIR};

void expect_close(const std::optional<double>& actual, double expected, double tolerance,
                  const std::string& label)
{
    ASSERT_TRUE(actual.has_value()) << label;
    EXPECT_NEAR(*actual, expected, tolerance * std::max(1.0, std::abs(expected))) << label;
}

} // namespace

TEST(Stats, RunningStatsOnEmptyAndSingleValue)
{
    stats::RunningStats empty;
    EXPECT_FALSE(empty.mean().has_value());
    EXPECT_FALSE(empty.sample_stddev().has_value());

    stats::RunningStats one;
    one.add(7.5);
    EXPECT_EQ(one.mean(), 7.5);
    EXPECT_EQ(one.min(), 7.5);
    EXPECT_EQ(one.max(), 7.5);
    EXPECT_FALSE(one.sample_stddev().has_value()); // 一个样本算不出样本标准差
}

TEST(Stats, RunningStatsMatchesTextbookSample)
{
    stats::RunningStats s;
    for (const double v : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0})
    {
        s.add(v);
    }
    expect_close(s.mean(), 5.0, 1e-12, "mean");
    expect_close(s.sample_stddev(), std::sqrt(32.0 / 7.0), 1e-12, "stddev");
}

TEST(Stats, PercentileUsesLinearInterpolation)
{
    const std::vector<double> one{3.0};
    EXPECT_EQ(stats::percentile_sorted(one, 0.99), 3.0);

    const std::vector<double> four{1.0, 2.0, 3.0, 4.0};
    EXPECT_DOUBLE_EQ(stats::percentile_sorted(four, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(stats::percentile_sorted(four, 0.5), 2.5);
    EXPECT_DOUBLE_EQ(stats::percentile_sorted(four, 0.95), 3.85);
    EXPECT_DOUBLE_EQ(stats::percentile_sorted(four, 1.0), 4.0);
}

TEST(Stats, MedianHandlesOddAndEvenCounts)
{
    std::vector<double> odd{5.0, 1.0, 3.0};
    EXPECT_EQ(stats::median_inplace(odd), 3.0);
    std::vector<double> even{4.0, 1.0, 3.0, 2.0};
    EXPECT_EQ(stats::median_inplace(even), 2.5);
}

TEST(Stats, SummaryOfEmptyColumnHasNoStatistics)
{
    const stats::Summary s = stats::summarize({}, 4);
    EXPECT_EQ(s.count, 0u);
    EXPECT_EQ(s.missing, 4u);
    EXPECT_FALSE(s.mean.has_value());
    EXPECT_FALSE(s.p99.has_value());
}

TEST(Stats, SummaryMatchesSchemaExampleReport)
{
    const std::vector<double> frame_ms{16.683, 16.684, 24.646, 16.690, 16.686,
                                       31.204, 16.681, 16.688, 16.684};
    const stats::Summary s = stats::summarize(frame_ms);
    expect_close(s.mean, 19.182889, 1e-6, "mean");
    expect_close(s.stddev, 5.220439, 1e-6, "stddev");
    expect_close(s.p50, 16.686, 1e-9, "p50");
    expect_close(s.p95, 28.5808, 1e-9, "p95");
    expect_close(s.p99, 30.67936, 1e-9, "p99");
}

TEST(Stats, SummaryMatchesPythonGoldenValues)
{
    for (const auto& entry : fs::directory_iterator(kTestData))
    {
        const auto expected =
            nlohmann::json::parse(gpia::read_file_bytes(entry.path() / "expected.json").value());
        const auto& golden = expected["frame_time_ms"];
        if (golden.is_null())
        {
            continue;
        }
        gpia::Diagnostics d;
        const auto run = gpia::load_run(entry.path(), d);
        ASSERT_TRUE(run) << entry.path();

        std::vector<double> values;
        for (const auto& f : run->table.frames)
        {
            if (f.frame_index >= run->manifest.warmup_frames)
            {
                values.push_back(f.frame_time_ms);
            }
        }
        const stats::Summary s = stats::summarize(values);
        const std::string name = expected["case"].get<std::string>();
        EXPECT_EQ(s.count, golden["count"].get<std::size_t>()) << name;
        for (const auto& [key, value] :
             {std::pair{"mean", s.mean}, std::pair{"stddev", s.stddev}, std::pair{"min", s.min},
              std::pair{"max", s.max}, std::pair{"p50", s.p50}, std::pair{"p95", s.p95},
              std::pair{"p99", s.p99}})
        {
            expect_close(value, golden[key].get<double>(), 1e-9, name + "." + key);
        }
    }
}

TEST(Stats, MovingAverageUsesAvailablePrefix)
{
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0};
    const auto avg = stats::moving_average(values, 2);
    ASSERT_EQ(avg.size(), 4u);
    EXPECT_DOUBLE_EQ(avg[0], 1.0);
    EXPECT_DOUBLE_EQ(avg[1], 1.5);
    EXPECT_DOUBLE_EQ(avg[2], 2.5);
    EXPECT_DOUBLE_EQ(avg[3], 3.5);
}

TEST(Stats, HistogramCountsUnderflowAndOverflow)
{
    const std::vector<double> values{0.5, 1.5, 1.7, -1.0, 10.0};
    const auto h = stats::histogram(values, 0.0, 1.0, 3);
    EXPECT_EQ(h.counts, (std::vector<std::size_t>{1, 2, 0}));
    EXPECT_EQ(h.underflow, 1u);
    EXPECT_EQ(h.overflow, 1u);
}
