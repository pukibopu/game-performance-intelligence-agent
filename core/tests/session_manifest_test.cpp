#include <string_view>

#include <gtest/gtest.h>

#include "gpia/model/session_manifest.hpp"

using gpia::Availability;
using gpia::ColumnInfo;
using gpia::SessionManifest;

TEST(SessionManifest, EnumNamesRoundTrip)
{
    for (const std::string_view text : {"measured", "unsupported"})
    {
        const auto value = gpia::parse_availability(text);
        ASSERT_TRUE(value.has_value()) << text;
        EXPECT_EQ(gpia::to_string(*value), text);
    }
    for (const std::string_view text : {"completed", "aborted"})
    {
        const auto value = gpia::parse_run_status(text);
        ASSERT_TRUE(value.has_value()) << text;
        EXPECT_EQ(gpia::to_string(*value), text);
    }
    for (const std::string_view text : {"player", "editor"})
    {
        const auto value = gpia::parse_run_context(text);
        ASSERT_TRUE(value.has_value()) << text;
        EXPECT_EQ(gpia::to_string(*value), text);
    }
    for (const std::string_view text : {"mono", "il2cpp"})
    {
        const auto value = gpia::parse_scripting_backend(text);
        ASSERT_TRUE(value.has_value()) << text;
        EXPECT_EQ(gpia::to_string(*value), text);
    }
}

TEST(SessionManifest, ParsingIsStrict)
{
    EXPECT_FALSE(gpia::parse_availability("Measured").has_value());
    EXPECT_FALSE(gpia::parse_availability("").has_value());
    EXPECT_FALSE(gpia::parse_run_status("done").has_value());
    EXPECT_FALSE(gpia::parse_run_context("Player").has_value());
    EXPECT_FALSE(gpia::parse_scripting_backend("IL2CPP").has_value());
}

TEST(SessionManifest, FindsColumnByName)
{
    SessionManifest manifest;
    manifest.columns.push_back(ColumnInfo{.name = "frame_time_ms",
                                          .unit = "ms",
                                          .availability = Availability::Measured,
                                          .source = "x"});
    manifest.columns.push_back(ColumnInfo{.name = "gc_alloc_bytes",
                                          .unit = "bytes",
                                          .availability = Availability::Unsupported,
                                          .source = "y"});

    const ColumnInfo* gc = manifest.find_column("gc_alloc_bytes");
    ASSERT_NE(gc, nullptr);
    EXPECT_EQ(gc->availability, Availability::Unsupported);
    EXPECT_EQ(manifest.find_column("draw_calls"), nullptr);
}
