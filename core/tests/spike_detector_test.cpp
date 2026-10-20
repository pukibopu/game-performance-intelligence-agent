#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/detect/spike_detector.hpp"
#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/run_loader.hpp"

namespace fs = std::filesystem;
namespace detect = gpia::detect;

namespace
{

const fs::path kTestData{GPIA_TESTDATA_DIR};
constexpr double kMiB = 1024.0 * 1024.0;

std::vector<std::size_t> indices(const std::vector<detect::Spike>& spikes)
{
    std::vector<std::size_t> result;
    for (const auto& s : spikes)
    {
        result.push_back(s.index);
    }
    return result;
}

struct Fixture
{
    std::vector<std::uint64_t> frame_index;
    std::vector<double> frame_time_ms;
    std::vector<double> gc_alloc_bytes; // 只收集有值的帧
    std::vector<std::uint64_t> gc_alloc_frame;
    nlohmann::json expected;
};

// 取预热之后的帧，与生成器计算 ground truth 的口径一致
Fixture load_fixture(const std::string& name)
{
    Fixture f;
    gpia::Diagnostics d;
    const auto run = gpia::load_run(kTestData / name, d).value();
    for (const auto& frame : run.table.frames)
    {
        if (frame.frame_index < run.manifest.warmup_frames)
        {
            continue;
        }
        f.frame_index.push_back(frame.frame_index);
        f.frame_time_ms.push_back(frame.frame_time_ms);
        if (frame.gc_alloc_bytes)
        {
            f.gc_alloc_bytes.push_back(static_cast<double>(*frame.gc_alloc_bytes));
            f.gc_alloc_frame.push_back(frame.frame_index);
        }
    }
    f.expected =
        nlohmann::json::parse(gpia::read_file_bytes(kTestData / name / "expected.json").value());
    return f;
}

std::vector<std::uint64_t> to_frames(const std::vector<detect::Spike>& spikes,
                                     const std::vector<std::uint64_t>& frame_of)
{
    std::vector<std::uint64_t> frames;
    for (const auto& s : spikes)
    {
        frames.push_back(frame_of[s.index]);
    }
    return frames;
}

} // namespace

TEST(SpikeDetector, EmptyInputGivesNoSpikes)
{
    EXPECT_TRUE(detect::fixed_threshold({}, 1.0).empty());
    EXPECT_TRUE(detect::percentile_spikes({}, 0.5, 2.0, 0.0).empty());
    EXPECT_TRUE(detect::rolling_median_spikes({}, 61, 2.0, 0.0).empty());
    EXPECT_TRUE(detect::mad_spikes({}, 5.0, 0.0).empty());
}

TEST(SpikeDetector, FixedThresholdFlagsEveryFrameAfterLevelShift)
{
    std::vector<double> values(100, 10.0);
    values.resize(200, 18.0);
    EXPECT_EQ(detect::fixed_threshold(values, 15.0).size(), 100u);
}

TEST(SpikeDetector, RollingMedianIgnoresLevelShift)
{
    std::vector<double> values(100, 10.0);
    values.resize(200, 18.0);
    EXPECT_TRUE(detect::rolling_median_spikes(values, 61, 2.0, 0.0).empty());
}

TEST(SpikeDetector, RollingMedianFindsIsolatedSpikes)
{
    std::vector<double> values(200, 16.0);
    values[30] = 50.0;
    values[90] = 50.0;
    values[150] = 50.0;
    const auto spikes = detect::rolling_median_spikes(values, 61, 2.0, 0.0);
    EXPECT_EQ(indices(spikes), (std::vector<std::size_t>{30, 90, 150}));
    EXPECT_DOUBLE_EQ(spikes[0].baseline, 16.0);
    EXPECT_DOUBLE_EQ(spikes[0].threshold, 32.0);
}

TEST(SpikeDetector, RollingMedianRespectsAbsoluteFloor)
{
    std::vector<double> values(50, 1.0);
    values[10] = 5.0;
    EXPECT_TRUE(detect::rolling_median_spikes(values, 11, 2.0, 25.0).empty());
}

TEST(SpikeDetector, MadFallsBackToFloorWhenMadIsZero)
{
    std::vector<double> values(100, 0.0);
    values[10] = 8.0 * kMiB;
    values[40] = 8.0 * kMiB;
    values[41] = 4096.0;
    const auto spikes = detect::mad_spikes(values, 5.0, 1.0 * kMiB);
    EXPECT_EQ(indices(spikes), (std::vector<std::size_t>{10, 40}));
    EXPECT_DOUBLE_EQ(spikes[0].threshold, 1.0 * kMiB);
}

TEST(SpikeDetector, PercentileThresholdScalesTheBaseline)
{
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i)
    {
        values.push_back(i);
    }
    const auto spikes = detect::percentile_spikes(values, 0.5, 1.5, 0.0);
    ASSERT_EQ(spikes.size(), 25u);
    EXPECT_EQ(spikes.front().index, 75u);
}

TEST(SpikeDetector, RecoversInjectedHitchesExactly)
{
    for (const std::string name :
         {"normal", "cpu_spikes", "gc_spikes", "missing_columns", "memory_growth"})
    {
        const Fixture f = load_fixture(name);
        const auto spikes = detect::rolling_median_spikes(f.frame_time_ms, 61, 2.0, 0.0);
        const auto want = f.expected["expected"]["hitch_frames"].get<std::vector<std::uint64_t>>();
        EXPECT_EQ(to_frames(spikes, f.frame_index), want) << name;
    }
}

TEST(SpikeDetector, RecoversInjectedAllocationSpikesExactly)
{
    const Fixture f = load_fixture("gc_spikes");
    const auto spikes = detect::mad_spikes(f.gc_alloc_bytes, 5.0, 1.0 * kMiB);
    const auto want =
        f.expected["expected"]["gc_alloc_spike_frames"].get<std::vector<std::uint64_t>>();
    EXPECT_EQ(to_frames(spikes, f.gc_alloc_frame), want);
}
