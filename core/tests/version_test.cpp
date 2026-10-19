#include <regex>
#include <string>

#include <gtest/gtest.h>

#include "gpia/version.hpp"

TEST(Version, IsNotEmpty)
{
    EXPECT_FALSE(gpia::version().empty());
}

// Must match analyzer.version in schemas/analysis-report.v1.schema.json
TEST(Version, MatchesReportSchemaPattern)
{
    const std::string v{gpia::version()};
    const std::regex semver{R"(^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$)"};
    EXPECT_TRUE(std::regex_match(v, semver)) << "version was: " << v;
}
