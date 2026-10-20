#include <gtest/gtest.h>

#include "gpia/diagnostics.hpp"

using gpia::Diagnostic;
using gpia::Diagnostics;
using gpia::Severity;

TEST(Diagnostics, StartsEmpty)
{
    const Diagnostics d;
    EXPECT_TRUE(d.items().empty());
    EXPECT_EQ(d.count(Severity::Error), 0u);
    EXPECT_FALSE(d.has_errors());
    EXPECT_EQ(d.suppressed_count(), 0u);
}

TEST(Diagnostics, CountsBySeverity)
{
    Diagnostics d;
    d.add({.severity = Severity::Info, .code = "TRAILING_FRAME_TIMING_MISSING", .message = "a"});
    d.add({.severity = Severity::Warning, .code = "UNKNOWN_COLUMN", .message = "b"});
    d.add({.severity = Severity::Error, .code = "CSV_INVALID_NUMBER", .message = "c"});
    d.add({.severity = Severity::Error, .code = "CSV_INVALID_NUMBER", .message = "d"});

    EXPECT_EQ(d.count(Severity::Info), 1u);
    EXPECT_EQ(d.count(Severity::Warning), 1u);
    EXPECT_EQ(d.count(Severity::Error), 2u);
    EXPECT_TRUE(d.has_errors());
}

TEST(Diagnostics, KeepsOptionalLocation)
{
    Diagnostics d;
    d.add({.severity = Severity::Error,
           .code = "CSV_INVALID_NUMBER",
           .message = "not a number: abc",
           .line = 812,
           .field = "frame_time_ms"});
    d.add({.severity = Severity::Warning, .code = "FRAME_INDEX_GAP", .message = "gap"});

    ASSERT_EQ(d.items().size(), 2u);
    const Diagnostic& located = d.items()[0];
    ASSERT_TRUE(located.line.has_value());
    EXPECT_EQ(*located.line, 812u);
    EXPECT_EQ(located.field, "frame_time_ms");
    EXPECT_FALSE(d.items()[1].line.has_value());
    EXPECT_FALSE(d.items()[1].field.has_value());
}

TEST(Diagnostics, StoresUpToLimitButCountsAll)
{
    Diagnostics d{3};
    for (int i = 0; i < 5; ++i)
    {
        d.add({.severity = Severity::Error, .code = "CSV_INVALID_NUMBER", .message = "bad"});
    }

    // 超过上限的部分只计数不保存
    EXPECT_EQ(d.items().size(), 3u);
    EXPECT_EQ(d.count(Severity::Error), 5u);
    EXPECT_EQ(d.suppressed_count(), 2u);
}

TEST(Diagnostics, ValidatesCodeFormat)
{
    EXPECT_TRUE(gpia::is_valid_diagnostic_code("CSV_INVALID_NUMBER"));
    EXPECT_TRUE(gpia::is_valid_diagnostic_code("A"));
    EXPECT_TRUE(gpia::is_valid_diagnostic_code("A1_B2"));
    EXPECT_FALSE(gpia::is_valid_diagnostic_code(""));
    EXPECT_FALSE(gpia::is_valid_diagnostic_code("csv_invalid"));
    EXPECT_FALSE(gpia::is_valid_diagnostic_code("1ABC"));
    EXPECT_FALSE(gpia::is_valid_diagnostic_code("_ABC"));
    EXPECT_FALSE(gpia::is_valid_diagnostic_code("CSV-INVALID"));
}
