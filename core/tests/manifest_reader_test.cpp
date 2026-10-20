#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/manifest_reader.hpp"

using gpia::Availability;
using gpia::Diagnostics;
using gpia::ErrorCode;
using gpia::Severity;

namespace
{

const std::filesystem::path kExample =
    std::filesystem::path{GPIA_SCHEMAS_DIR} / "examples" / "run-manifest.example.json";

nlohmann::json example_json()
{
    const auto bytes = gpia::read_file_bytes(kExample);
    return nlohmann::json::parse(bytes.value());
}

bool has_code(const Diagnostics& d, std::string_view code, std::string_view field)
{
    for (const auto& item : d.items())
    {
        if (item.code == code && item.field == field)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(ManifestReader, ParsesSchemaExample)
{
    Diagnostics d;
    const auto m = gpia::read_manifest(kExample, d);

    ASSERT_TRUE(m) << m.error().message;
    EXPECT_TRUE(d.items().empty());
    EXPECT_EQ(m->schema_version, (gpia::SchemaVersion{.major = 1, .minor = 0}));
    EXPECT_EQ(m->run_id, "20261020T120000Z-gc_pressure-example");
    EXPECT_EQ(m->build.scripting_backend, gpia::ScriptingBackend::Mono);
    EXPECT_TRUE(m->build.development_build);
    EXPECT_FALSE(m->device.gpu_driver.has_value());
    EXPECT_EQ(m->display.target_frame_rate, -1);
    EXPECT_EQ(m->frame_count, 9u);
    ASSERT_EQ(m->columns.size(), 11u);
    EXPECT_EQ(m->columns[6].name, "gc_alloc_bytes");
    EXPECT_EQ(m->columns[6].availability, Availability::Measured);
    EXPECT_EQ(m->scenario.params_json, R"({"alloc_bytes":8388608,"period_frames":120})");
}

TEST(ManifestReader, RejectsInvalidJson)
{
    Diagnostics d;
    const auto m = gpia::parse_manifest("{\"schema_version\": ", d);

    ASSERT_FALSE(m);
    EXPECT_EQ(m.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(d.items().front().code, "MANIFEST_INVALID_JSON");
}

TEST(ManifestReader, RejectsUnsupportedMajorVersion)
{
    auto j = example_json();
    j["schema_version"] = "2.0";
    Diagnostics d;
    const auto m = gpia::parse_manifest(j.dump(), d);

    ASSERT_FALSE(m);
    EXPECT_TRUE(has_code(d, "MANIFEST_UNSUPPORTED_VERSION", "schema_version"));
}

TEST(ManifestReader, AcceptsNewerMinorVersionAndWarnsOnUnknownFields)
{
    auto j = example_json();
    j["schema_version"] = "1.7";
    j["future_field"] = 1;
    j["build"]["future_flag"] = true;
    Diagnostics d;
    const auto m = gpia::parse_manifest(j.dump(), d);

    ASSERT_TRUE(m);
    EXPECT_EQ(m->schema_version.minor, 7u);
    EXPECT_TRUE(has_code(d, "MANIFEST_UNKNOWN_FIELD", "future_field"));
    EXPECT_TRUE(has_code(d, "MANIFEST_UNKNOWN_FIELD", "build.future_flag"));
    EXPECT_EQ(d.count(Severity::Error), 0u);
}

TEST(ManifestReader, ReportsEveryBadFieldWithItsPath)
{
    auto j = example_json();
    j["build"].erase("unity_version");
    j["display"]["vsync_count"] = 9;
    j["columns"][2]["availability"] = "partial";
    j["status"] = "done";
    Diagnostics d;
    const auto m = gpia::parse_manifest(j.dump(), d);

    ASSERT_FALSE(m);
    EXPECT_TRUE(has_code(d, "MANIFEST_MISSING_FIELD", "build.unity_version"));
    EXPECT_TRUE(has_code(d, "MANIFEST_INVALID_FIELD", "display.vsync_count"));
    EXPECT_TRUE(has_code(d, "MANIFEST_INVALID_FIELD", "columns[2].availability"));
    EXPECT_TRUE(has_code(d, "MANIFEST_INVALID_FIELD", "status"));
}

TEST(ManifestReader, RejectsPathTraversalInFramesFile)
{
    auto j = example_json();
    j["frames_file"] = "..\\secrets.csv";
    Diagnostics d;
    const auto m = gpia::parse_manifest(j.dump(), d);

    ASSERT_FALSE(m);
    EXPECT_TRUE(has_code(d, "MANIFEST_INVALID_FIELD", "frames_file"));
}

TEST(ManifestReader, RequiresAbortReasonWhenAborted)
{
    auto j = example_json();
    j["status"] = "aborted";
    Diagnostics d;
    const auto m = gpia::parse_manifest(j.dump(), d);

    ASSERT_FALSE(m);
    EXPECT_TRUE(has_code(d, "MANIFEST_MISSING_FIELD", "abort_reason"));
}

TEST(ManifestReader, ValidatesRunId)
{
    EXPECT_TRUE(gpia::is_valid_run_id("20261020T120000Z-gc_pressure-1"));
    EXPECT_FALSE(gpia::is_valid_run_id(""));
    EXPECT_FALSE(gpia::is_valid_run_id("has space"));
    EXPECT_FALSE(gpia::is_valid_run_id("a:b"));
    EXPECT_FALSE(gpia::is_valid_run_id(std::string(65, 'a')));
}
