#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "gpia/diagnostics.hpp"
#include "gpia/io/csv_frame_reader.hpp"

using gpia::Diagnostics;
using gpia::ErrorCode;
using gpia::Severity;

namespace
{

const std::filesystem::path kTestData{GPIA_TESTDATA_DIR};
const std::filesystem::path kSchemas{GPIA_SCHEMAS_DIR};

constexpr std::string_view kMinimalHeader =
    "frame_index,timestamp_ns,frame_time_ms,scenario_marker\n";

struct Expected
{
    std::uint64_t line;
    std::string_view code;
    std::optional<std::string_view> field;
};

// 只比较行号、诊断码和字段名，消息文本允许调整措辞
void expect_diagnostics(const Diagnostics& actual, const std::vector<Expected>& expected)
{
    ASSERT_EQ(actual.items().size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const auto& d = actual.items()[i];
        EXPECT_EQ(d.line, expected[i].line) << "diagnostic " << i;
        EXPECT_EQ(d.code, expected[i].code) << "diagnostic " << i;
        EXPECT_EQ(d.field, expected[i].field) << "diagnostic " << i;
    }
}

} // namespace

TEST(CsvFrameReader, ParsesMinimalFile)
{
    const std::string text =
        std::string(kMinimalHeader) + "0,16683000,16.683,0\n1,33367000,16.684,1\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table) << table.error().message;
    ASSERT_EQ(table->frames.size(), 2u);
    EXPECT_EQ(table->data_rows, 2u);
    EXPECT_EQ(table->frames[1].frame_index, 1u);
    EXPECT_EQ(table->frames[1].timestamp_ns, 33'367'000);
    EXPECT_DOUBLE_EQ(table->frames[1].frame_time_ms, 16.684);
    EXPECT_EQ(table->frames[1].scenario_marker, 1u);
    EXPECT_FALSE(table->frames[1].gc_alloc_bytes.has_value());
    EXPECT_TRUE(d.items().empty());
}

TEST(CsvFrameReader, MapsColumnsByName)
{
    const std::string text =
        "scenario_marker,gc_alloc_bytes,frame_time_ms,timestamp_ns,frame_index\n"
        "2,4096,16.5,1000,7\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    ASSERT_EQ(table->frames.size(), 1u);
    const auto& f = table->frames[0];
    EXPECT_EQ(f.frame_index, 7u);
    EXPECT_EQ(f.timestamp_ns, 1000);
    EXPECT_EQ(f.gc_alloc_bytes, 4096);
    EXPECT_EQ(f.scenario_marker, 2u);
}

TEST(CsvFrameReader, EmptyOptionalCellStaysMissing)
{
    const std::string text =
        "frame_index,timestamp_ns,frame_time_ms,gc_alloc_bytes,scenario_marker\n"
        "0,1000,16.6,,0\n"
        "1,2000,16.6,0,0\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    ASSERT_EQ(table->frames.size(), 2u);
    EXPECT_FALSE(table->frames[0].gc_alloc_bytes.has_value());
    EXPECT_EQ(table->frames[1].gc_alloc_bytes, 0); // 测得 0 字节与缺失必须区分
    EXPECT_TRUE(d.items().empty());
}

TEST(CsvFrameReader, WarnsAboutUnknownColumn)
{
    const std::string text = "frame_index,timestamp_ns,frame_time_ms,scenario_marker,fps\n"
                             "0,1000,16.6,0,60\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    EXPECT_EQ(table->frames.size(), 1u);
    expect_diagnostics(d, {{1, "CSV_UNKNOWN_COLUMN", "fps"}});
    EXPECT_EQ(d.items()[0].severity, Severity::Warning);
}

TEST(CsvFrameReader, FailsOnMissingRequiredColumn)
{
    Diagnostics d;
    const auto table =
        gpia::parse_frames_csv("frame_index,timestamp_ns,scenario_marker\n0,1,0\n", d);

    ASSERT_FALSE(table);
    EXPECT_EQ(table.error().code, ErrorCode::InvalidInput);
    expect_diagnostics(d, {{1, "CSV_MISSING_COLUMN", "frame_time_ms"}});
}

TEST(CsvFrameReader, FailsOnDuplicateColumn)
{
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(
        "frame_index,timestamp_ns,frame_time_ms,frame_time_ms,scenario_marker\n", d);

    ASSERT_FALSE(table);
    expect_diagnostics(d, {{1, "CSV_DUPLICATE_COLUMN", "frame_time_ms"}});
}

TEST(CsvFrameReader, FailsOnEmptyText)
{
    Diagnostics d;
    const auto table = gpia::parse_frames_csv("", d);

    ASSERT_FALSE(table);
    EXPECT_EQ(table.error().code, ErrorCode::InvalidInput);
    expect_diagnostics(d, {{1, "CSV_EMPTY_FILE", std::nullopt}});
}

TEST(CsvFrameReader, AcceptsBomAndCrLf)
{
    const std::string text = "\xEF\xBB\xBF"
                             "frame_index,timestamp_ns,frame_time_ms,scenario_marker\r\n"
                             "0,1000,16.6,0\r\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    EXPECT_EQ(table->frames.size(), 1u);
    expect_diagnostics(d, {{1, "CSV_UTF8_BOM", std::nullopt}});
}

TEST(CsvFrameReader, RejectsNonFiniteAndPartialNumbers)
{
    const std::string text = "frame_index,timestamp_ns,frame_time_ms,draw_calls,scenario_marker\n"
                             "0,1000,nan,1,0\n"
                             "1,2000,16.6, 5,0\n"
                             "2,3000,16.6,5x,0\n"
                             "3,4000,1e400,1,0\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    ASSERT_EQ(table->frames.size(), 2u); // 第 1 行和第 4 行的必需列无效，整行丢弃
    EXPECT_FALSE(table->frames[0].draw_calls.has_value());
    expect_diagnostics(d, {{2, "CSV_INVALID_NUMBER", "frame_time_ms"},
                           {3, "CSV_INVALID_NUMBER", "draw_calls"},
                           {4, "CSV_INVALID_NUMBER", "draw_calls"},
                           {5, "CSV_INVALID_NUMBER", "frame_time_ms"}});
}

TEST(CsvFrameReader, DropsFrameIndexThatDoesNotIncrease)
{
    const std::string text =
        std::string(kMinimalHeader) + "5,1000,16.6,0\n5,2000,16.6,0\n6,3000,16.6,0\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    ASSERT_EQ(table->frames.size(), 2u);
    EXPECT_EQ(table->frames[1].frame_index, 6u);
    expect_diagnostics(d, {{3, "FRAME_INDEX_NOT_INCREASING", "frame_index"}});
}

TEST(CsvFrameReader, GivesUpAfterTooManyErrors)
{
    const std::string text = std::string(kMinimalHeader) + "0,1,x,0\n1,2,x,0\n2,3,x,0\n3,4,x,0\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d, {.max_errors = 2});

    ASSERT_FALSE(table);
    EXPECT_EQ(table.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(d.items().back().code, "CSV_TOO_MANY_ERRORS");
}

TEST(CsvFrameReader, SanitizesUntrustedBytesInMessages)
{
    const std::string text = std::string(kMinimalHeader) + "0,1000,\xFF\x01zz,0\n";
    Diagnostics d;
    const auto table = gpia::parse_frames_csv(text, d);

    ASSERT_TRUE(table);
    ASSERT_EQ(d.items().size(), 1u);
    for (const char c : d.items()[0].message)
    {
        EXPECT_TRUE(c >= 0x20 && c <= 0x7E)
            << "byte " << static_cast<int>(static_cast<unsigned char>(c));
    }
}

TEST(CsvFrameReader, CorruptRowsFixtureMatchesGroundTruth)
{
    Diagnostics d;
    const auto table = gpia::read_frames_csv(kTestData / "corrupt_rows" / "frames.csv", d);

    ASSERT_TRUE(table) << table.error().message;
    EXPECT_EQ(table->data_rows, 58u);
    EXPECT_EQ(table->frames.size(), 51u);
    expect_diagnostics(d, {{12, "CSV_INVALID_NUMBER", "frame_time_ms"},
                           {16, "CSV_INVALID_NUMBER", "draw_calls"},
                           {22, "CSV_INVALID_NUMBER", "gc_used_bytes"},
                           {27, "CSV_INVALID_NUMBER", "frame_time_ms"},
                           {32, "CSV_OUT_OF_RANGE", "frame_time_ms"},
                           {35, "CSV_MISSING_REQUIRED", "frame_time_ms"},
                           {42, "TIMESTAMP_NOT_INCREASING", "timestamp_ns"},
                           {52, "FRAME_INDEX_GAP", "frame_index"},
                           {55, "CSV_COLUMN_COUNT_MISMATCH", std::nullopt},
                           {59, "CSV_COLUMN_COUNT_MISMATCH", std::nullopt}});
}

TEST(CsvFrameReader, CleanFixturesHaveNoDiagnostics)
{
    const std::vector<std::pair<std::string_view, std::size_t>> cases = {
        {"normal", 300},          {"cpu_spikes", 300},    {"gc_spikes", 300},
        {"missing_columns", 300}, {"memory_growth", 900}, {"header_only", 0}};
    for (const auto& [name, rows] : cases)
    {
        Diagnostics d;
        const auto table = gpia::read_frames_csv(kTestData / std::string(name) / "frames.csv", d);
        ASSERT_TRUE(table) << name;
        EXPECT_EQ(table->frames.size(), rows) << name;
        EXPECT_TRUE(d.items().empty()) << name;
    }
}

TEST(CsvFrameReader, EmptyFileFixtureIsFatal)
{
    Diagnostics d;
    const auto table = gpia::read_frames_csv(kTestData / "empty_file" / "frames.csv", d);

    ASSERT_FALSE(table);
    EXPECT_EQ(table.error().code, ErrorCode::InvalidInput);
}

TEST(CsvFrameReader, MissingFileIsIoError)
{
    Diagnostics d;
    const auto table = gpia::read_frames_csv(kTestData / "no_such_dir" / "frames.csv", d);

    ASSERT_FALSE(table);
    EXPECT_EQ(table.error().code, ErrorCode::IoError);
}

TEST(CsvFrameReader, SchemaExampleParses)
{
    Diagnostics d;
    const auto table = gpia::read_frames_csv(kSchemas / "examples" / "frames.example.csv", d);

    ASSERT_TRUE(table);
    ASSERT_EQ(table->frames.size(), 9u);
    EXPECT_EQ(table->frames[5].gc_collection_count, 13);
    EXPECT_FALSE(table->frames[5].cpu_main_thread_ms.has_value());
}

TEST(CsvFrameReader, KnownColumnsMatchFrameRecordOrder)
{
    const auto columns = gpia::known_frame_columns();
    ASSERT_EQ(columns.size(), 20u);
    EXPECT_EQ(columns.front(), "frame_index");
    EXPECT_EQ(columns.back(), "scenario_marker");
}
