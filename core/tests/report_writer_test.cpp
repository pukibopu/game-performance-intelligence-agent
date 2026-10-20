#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/analysis/analyze.hpp"
#include "gpia/diagnostics.hpp"
#include "gpia/io/run_loader.hpp"
#include "gpia/report/markdown.hpp"
#include "gpia/report/report_writer.hpp"

namespace fs = std::filesystem;

namespace
{

const fs::path kTestData{GPIA_TESTDATA_DIR};

struct Written
{
    nlohmann::json json;
    std::string markdown;
};

Written write(const std::string& name)
{
    gpia::Diagnostics d;
    const auto run = gpia::load_run(kTestData / name, d).value();
    const auto report = gpia::analyze_run(run, {}, d);
    return {nlohmann::json::parse(gpia::write_report_json(report, d)),
            gpia::render_markdown(report, d)};
}

} // namespace

TEST(ReportWriter, WritesTheReportSkeleton)
{
    const auto [j, md] = write("gc_spikes");
    EXPECT_EQ(j["schema_version"], "1.0");
    EXPECT_EQ(j["report_kind"], "analysis");
    EXPECT_EQ(j["run_id"], "synthetic-gc_spikes");
    EXPECT_EQ(j["outcome"], "ok");
    EXPECT_EQ(j["analyzer"]["name"], "gpia-core");
    EXPECT_TRUE(j["analyzer"]["config"].is_object());
    EXPECT_EQ(j["input"]["frames_analyzed"], 280);
    EXPECT_TRUE(j["summary"].contains("frame_time_ms"));
    EXPECT_TRUE(j["diagnostics"].is_array());
}

TEST(ReportWriter, OmitsStatisticsInsteadOfWritingZero)
{
    const auto [j, md] = write("missing_columns");
    const auto& alloc = j["summary"]["gc_alloc_bytes"];
    EXPECT_EQ(alloc["count"], 0);
    EXPECT_EQ(alloc["missing"], 280);
    EXPECT_FALSE(alloc.contains("mean"));
    EXPECT_FALSE(alloc.contains("p99"));
}

TEST(ReportWriter, WritesFindingFields)
{
    const auto [j, md] = write("gc_spikes");
    bool saw_hypothesis = false;
    for (const auto& f : j["findings"])
    {
        EXPECT_TRUE(f.contains("rule"));
        EXPECT_TRUE(f["rule"].contains("params"));
        EXPECT_FALSE(f["evidence_ids"].empty());
        if (f["evidence_level"] == "hypothesis")
        {
            saw_hypothesis = true;
            EXPECT_FALSE(f["missing_evidence"].empty());
        }
    }
    EXPECT_TRUE(saw_hypothesis);
}

TEST(ReportWriter, DiagnosticLineIsSingleLineJson)
{
    const std::string line = gpia::diagnostic_json_line({.severity = gpia::Severity::Warning,
                                                         .code = "FRAME_INDEX_GAP",
                                                         .message = "2 frames missing",
                                                         .line = 52,
                                                         .field = "frame_index"});
    EXPECT_EQ(line.find('\n'), std::string::npos);
    const auto j = nlohmann::json::parse(line);
    EXPECT_EQ(j["severity"], "warning");
    EXPECT_EQ(j["line"], 52);
}

TEST(ReportWriter, MarkdownListsFindingsAndMissingEvidence)
{
    const auto [j, md] = write("gc_spikes");
    EXPECT_NE(md.find("# GPIA analysis report: synthetic-gc_spikes"), std::string::npos);
    EXPECT_NE(md.find("| frame_time_ms |"), std::string::npos);
    EXPECT_NE(md.find("gc_collection_hitch"), std::string::npos);
    EXPECT_NE(md.find("GC.Collect marker duration for frame 70"), std::string::npos);
}
