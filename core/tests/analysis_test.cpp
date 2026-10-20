#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/analysis/analyze.hpp"
#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/run_loader.hpp"

using gpia::EvidenceLevel;
using gpia::FindingKind;
using gpia::Outcome;

namespace fs = std::filesystem;

namespace
{

const fs::path kTestData{GPIA_TESTDATA_DIR};

struct Analyzed
{
    gpia::Report report;
    nlohmann::json expected;
};

Analyzed analyze(const std::string& name, const gpia::AnalysisConfig& config = {})
{
    gpia::Diagnostics d;
    const auto run = gpia::load_run(kTestData / name, d).value();
    return {
        gpia::analyze_run(run, config, d),
        nlohmann::json::parse(gpia::read_file_bytes(kTestData / name / "expected.json").value())};
}

// 取某类结论覆盖的全部帧号，用来与 ground truth 精确比较
std::vector<std::uint64_t> frames_of(const gpia::Report& report, FindingKind kind)
{
    std::vector<std::uint64_t> frames;
    for (const auto& f : report.findings)
    {
        if (f.kind == kind && f.frame_range)
        {
            for (std::uint64_t i = f.frame_range->first; i <= f.frame_range->last; ++i)
            {
                frames.push_back(i);
            }
        }
    }
    std::ranges::sort(frames);
    return frames;
}

std::vector<std::uint64_t> expected_frames(const nlohmann::json& expected, const char* key)
{
    return expected["expected"][key].get<std::vector<std::uint64_t>>();
}

} // namespace

TEST(Analysis, HitchesMatchGroundTruthOnEveryFixture)
{
    for (const std::string name :
         {"normal", "cpu_spikes", "gc_spikes", "missing_columns", "memory_growth"})
    {
        const auto [report, expected] = analyze(name);
        EXPECT_EQ(frames_of(report, FindingKind::FrameHitch),
                  expected_frames(expected, "hitch_frames"))
            << name;
        EXPECT_EQ(frames_of(report, FindingKind::GcAllocSpike),
                  expected_frames(expected, "gc_alloc_spike_frames"))
            << name;
        const bool grew = !frames_of(report, FindingKind::MemoryGrowth).empty();
        EXPECT_EQ(grew, expected["expected"]["memory_growth"].get<bool>()) << name;
    }
}

TEST(Analysis, WarmupSpikeIsNotReported)
{
    const auto [report, expected] = analyze("cpu_spikes");
    const auto hitches = frames_of(report, FindingKind::FrameHitch);
    EXPECT_EQ(std::ranges::count(hitches, std::uint64_t{5}), 0);
    EXPECT_EQ(report.input.warmup_frames_skipped, 20u);
    EXPECT_EQ(report.input.frames_analyzed, 280u);
}

TEST(Analysis, GcCollectionHitchStaysHypothesisWithoutMarkerTiming)
{
    const auto [report, expected] = analyze("gc_spikes");
    const auto gc_hitches = frames_of(report, FindingKind::GcCollectionHitch);
    EXPECT_EQ(gc_hitches, expected_frames(expected, "gc_collection_frames"));
    for (const auto& f : report.findings)
    {
        if (f.kind == FindingKind::GcCollectionHitch)
        {
            EXPECT_EQ(f.level, EvidenceLevel::Hypothesis);
            EXPECT_FALSE(f.missing_evidence.empty()); // 假设必须写明还缺什么证据
        }
    }
}

TEST(Analysis, GcCollectionLevelsMatchGroundTruth)
{
    for (const std::string name : {"gc_spikes", "missing_columns", "gc_marker_timing"})
    {
        const auto [report, expected] = analyze(name);
        const auto& want = expected["expected"]["gc_collection_levels"];
        std::size_t seen = 0;
        for (const auto& f : report.findings)
        {
            if (f.kind != FindingKind::GcCollectionHitch)
            {
                continue;
            }
            ++seen;
            const std::string frame = std::to_string(f.frame_range->first);
            ASSERT_TRUE(want.contains(frame)) << name << " frame " << frame;
            EXPECT_EQ(std::string(gpia::to_string(f.level)), want[frame].get<std::string>())
                << name << " frame " << frame;
        }
        EXPECT_EQ(seen, want.size()) << name;
    }
}

TEST(Analysis, AllocationSpikeWithoutHitchIsNotBlamedOnGc)
{
    const auto [report, expected] = analyze("gc_spikes");
    const auto hitches = frames_of(report, FindingKind::FrameHitch);
    for (const std::uint64_t frame : expected_frames(expected, "gc_alloc_spike_frames"))
    {
        EXPECT_EQ(std::ranges::count(hitches, frame), 0) << frame;
    }
}

TEST(Analysis, MissingAllocationColumnGivesInsufficientEvidence)
{
    const auto [report, expected] = analyze("missing_columns");
    const auto it = std::ranges::find_if(report.findings, [](const auto& f) {
        return f.kind == FindingKind::InsufficientData && f.rule == "gc_alloc.availability";
    });
    ASSERT_NE(it, report.findings.end());
    EXPECT_EQ(it->level, EvidenceLevel::InsufficientEvidence);
}

TEST(Analysis, TooFewFramesIsInsufficientData)
{
    const auto [report, expected] = analyze("header_only");
    EXPECT_EQ(report.outcome, Outcome::InsufficientData);
    ASSERT_EQ(report.findings.size(), 1u);
    EXPECT_EQ(report.findings[0].kind, FindingKind::InsufficientData);
}

TEST(Analysis, DataQualityProblemsDowngradeOutcome)
{
    const auto [report, expected] = analyze("corrupt_rows");
    EXPECT_EQ(report.outcome, Outcome::OkWithWarnings);
    EXPECT_EQ(report.input.invalid_rows, 7u);
    EXPECT_EQ(report.input.frame_index_gaps, 1u);
}

TEST(Analysis, EveryFindingCitesExistingEvidence)
{
    for (const auto& entry : fs::directory_iterator(kTestData))
    {
        const std::string name = entry.path().filename().string();
        if (name == "empty_file")
        {
            continue;
        }
        const auto [report, expected] = analyze(name);
        std::set<std::string> ids;
        for (const auto& e : report.evidence)
        {
            EXPECT_TRUE(ids.insert(e.id).second) << "duplicate evidence id " << e.id;
            EXPECT_TRUE(e.id.starts_with("ev:" + report.run_id + ":")) << e.id;
        }
        for (const auto& f : report.findings)
        {
            EXPECT_TRUE(f.id.starts_with("fd:" + report.run_id + ":")) << f.id;
            ASSERT_FALSE(f.evidence_ids.empty()) << f.id;
            for (const auto& id : f.evidence_ids)
            {
                EXPECT_TRUE(ids.contains(id)) << f.id << " cites missing " << id;
            }
        }
    }
}

TEST(Analysis, FloorIsDerivedFromTargetFrameRate)
{
    const auto [report, expected] = analyze("normal");
    const auto config = nlohmann::json::parse(report.config_json);
    EXPECT_DOUBLE_EQ(config["hitch"]["floor_ms"].get<double>(),
                     25.0); // 目标 60 帧时为 1.5 倍帧预算
}

TEST(Analysis, ConfigOverridesChangeDetection)
{
    gpia::Diagnostics d;
    auto config = gpia::parse_analysis_config(R"({"hitch": {"k": 10.0}})", d);
    ASSERT_TRUE(config);
    const auto [report, expected] = analyze("cpu_spikes", *config);
    EXPECT_TRUE(frames_of(report, FindingKind::FrameHitch).empty()); // 三倍尖峰低于十倍阈值
}

// 真实 Player 采集的样本，固定下来防止分析规则在真实数据上悄悄退化
TEST(Analysis, GoldenPlayerRunsKeepTheirConclusions)
{
    const fs::path golden{GPIA_GOLDEN_DIR};
    struct Case
    {
        const char* name;
        FindingKind kind;
        std::size_t injected;
        std::optional<EvidenceLevel> gc_level;
    };
    const Case cases[] = {
        {"cpu_spike_mono_release", FindingKind::FrameHitch, 10, std::nullopt},
        {"gc_forced_mono_dev", FindingKind::GcCollectionHitch, 10,
         EvidenceLevel::EvidenceBackedDiagnosis},
        {"gc_forced_il2cpp_release", FindingKind::GcCollectionHitch, 10, EvidenceLevel::Hypothesis},
    };
    for (const Case& c : cases)
    {
        gpia::Diagnostics d;
        const auto run = gpia::load_run(golden / c.name, d);
        ASSERT_TRUE(run) << c.name;
        const auto report = gpia::analyze_run(*run, {}, d);

        std::set<std::uint64_t> injected;
        for (const auto& f : run->table.frames)
        {
            if (f.scenario_marker != 0 && f.frame_index >= run->manifest.warmup_frames)
            {
                injected.insert(f.frame_index);
            }
        }
        ASSERT_EQ(injected.size(), c.injected) << c.name;
        std::size_t matched = 0;
        for (const auto& f : report.findings)
        {
            if (f.kind == c.kind && injected.contains(f.frame_range->first))
            {
                ++matched;
                if (c.gc_level)
                {
                    EXPECT_EQ(f.level, *c.gc_level) << c.name;
                }
            }
        }
        EXPECT_EQ(matched, c.injected) << c.name;
    }
}

TEST(Analysis, InvalidConfigIsRejected)
{
    gpia::Diagnostics d;
    const auto config = gpia::parse_analysis_config(R"({"hitch": {"k": "big"}, "extra": 1})", d);
    ASSERT_FALSE(config);
    EXPECT_EQ(d.count(gpia::Severity::Error), 1u);
    EXPECT_EQ(d.count(gpia::Severity::Warning), 1u);
}
