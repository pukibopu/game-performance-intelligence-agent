#include <string_view>

#include <gtest/gtest.h>

#include "gpia/schema_version.hpp"

using gpia::SchemaVersion;

static_assert(gpia::is_supported(SchemaVersion{.major = 1, .minor = 0}));
static_assert(!gpia::is_supported(SchemaVersion{.major = 2, .minor = 0}));

TEST(SchemaVersion, ParsesMajorMinor)
{
    EXPECT_EQ(gpia::parse_schema_version("1.0"), (SchemaVersion{.major = 1, .minor = 0}));
    EXPECT_EQ(gpia::parse_schema_version("1.12"), (SchemaVersion{.major = 1, .minor = 12}));
}

TEST(SchemaVersion, RejectsMalformedText)
{
    for (const std::string_view text : {"", "1", "1.", ".1", "1.0.0", "a.b", "-1.0", "1.-1", "+1.0",
                                        " 1.0", "1.0 ", "99999999999.0"})
    {
        EXPECT_FALSE(gpia::parse_schema_version(text).has_value()) << "input: '" << text << "'";
    }
}

TEST(SchemaVersion, OrdersByMajorThenMinor)
{
    EXPECT_LT((SchemaVersion{.major = 1, .minor = 0}), (SchemaVersion{.major = 1, .minor = 1}));
    EXPECT_LT((SchemaVersion{.major = 1, .minor = 9}), (SchemaVersion{.major = 2, .minor = 0}));
    EXPECT_EQ((SchemaVersion{.major = 1, .minor = 1}), (SchemaVersion{.major = 1, .minor = 1}));
}

TEST(SchemaVersion, FormatsAsText)
{
    EXPECT_EQ(gpia::to_string(SchemaVersion{.major = 1, .minor = 0}), "1.0");
    EXPECT_EQ(gpia::to_string(SchemaVersion{.major = 1, .minor = 12}), "1.12");
}
