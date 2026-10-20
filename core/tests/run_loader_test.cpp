#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/run_loader.hpp"

using gpia::Diagnostics;
using gpia::ErrorCode;
using gpia::Severity;

namespace fs = std::filesystem;

namespace
{

const fs::path kTestData{GPIA_TESTDATA_DIR};

nlohmann::json read_json(const fs::path& path)
{
    return nlohmann::json::parse(gpia::read_file_bytes(path).value());
}

// 每个测试用独立的临时目录，测试结束自动删除
class TempRunDir
{
  public:
    explicit TempRunDir(const fs::path& source)
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("gpia-test-" + std::to_string(rd()));
        fs::create_directories(path_);
        for (const auto& entry : fs::directory_iterator(source))
        {
            fs::copy_file(entry.path(), path_ / entry.path().filename());
        }
    }
    ~TempRunDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempRunDir(const TempRunDir&) = delete;
    TempRunDir& operator=(const TempRunDir&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

  private:
    fs::path path_;
};

} // namespace

TEST(RunLoader, FixturesMatchGeneratorGroundTruth)
{
    for (const auto& entry : fs::directory_iterator(kTestData))
    {
        const auto expected = read_json(entry.path() / "expected.json");
        const std::string name = expected["case"].get<std::string>();
        Diagnostics d;
        const auto run = gpia::load_run(entry.path(), d);

        if (!expected["fatal_error"].is_null())
        {
            ASSERT_FALSE(run) << name;
            EXPECT_EQ(d.items().back().code, expected["fatal_error"].get<std::string>()) << name;
            continue;
        }
        ASSERT_TRUE(run) << name << ": " << run.error().message;
        EXPECT_EQ(run->table.frames.size(), expected["valid_rows"].get<std::size_t>()) << name;
        EXPECT_EQ(run->manifest.warmup_frames, expected["warmup_frames"].get<std::uint64_t>())
            << name;

        const auto& want = expected["expected"]["diagnostics"];
        ASSERT_EQ(d.items().size(), want.size()) << name;
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            EXPECT_EQ(d.items()[i].code, want[i]["code"].get<std::string>()) << name << " #" << i;
            EXPECT_EQ(d.items()[i].line, want[i]["line"].get<std::uint64_t>()) << name << " #" << i;
        }
    }
}

TEST(RunLoader, RejectsTamperedFrames)
{
    const TempRunDir dir{kTestData / "normal"};
    {
        std::ofstream out(dir.path() / "frames.csv", std::ios::binary | std::ios::app);
        out << "300,9999999999,16.6,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
    }
    Diagnostics d;
    const auto run = gpia::load_run(dir.path(), d);

    ASSERT_FALSE(run);
    EXPECT_EQ(run.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(d.items().back().code, "FRAMES_HASH_MISMATCH");
}

TEST(RunLoader, MissingRunDirectoryIsIoError)
{
    Diagnostics d;
    const auto run = gpia::load_run(kTestData / "no_such_case", d);

    ASSERT_FALSE(run);
    EXPECT_EQ(run.error().code, ErrorCode::IoError);
}

TEST(RunLoader, WarnsWhenFrameCountDisagrees)
{
    const TempRunDir dir{kTestData / "normal"};
    auto manifest = read_json(dir.path() / "manifest.json");
    manifest["frame_count"] = 299;
    {
        std::ofstream out(dir.path() / "manifest.json", std::ios::binary | std::ios::trunc);
        out << manifest.dump(2);
    }
    Diagnostics d;
    const auto run = gpia::load_run(dir.path(), d);

    ASSERT_TRUE(run);
    ASSERT_EQ(d.items().size(), 1u);
    EXPECT_EQ(d.items()[0].code, "FRAME_COUNT_MISMATCH");
    EXPECT_EQ(d.items()[0].severity, Severity::Warning);
}
