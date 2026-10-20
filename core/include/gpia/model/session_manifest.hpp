#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gpia/schema_version.hpp"

namespace gpia
{

// 列级可用性，单元格缺失另由空值表示
enum class Availability : std::uint8_t
{
    Measured,
    Unsupported,
};

enum class RunStatus : std::uint8_t
{
    Completed,
    Aborted,
};

// 回归比较只接受 Player 采集的数据
enum class RunContext : std::uint8_t
{
    Player,
    Editor,
};

enum class ScriptingBackend : std::uint8_t
{
    Mono,
    Il2Cpp,
};

struct ColumnInfo
{
    std::string name;
    std::string unit;
    Availability availability = Availability::Measured;
    std::string source;
};

struct BuildInfo
{
    std::string unity_version;
    std::string build_id;
    bool development_build = false;
    ScriptingBackend scripting_backend = ScriptingBackend::Mono;
    std::string graphics_api;
    std::string render_pipeline;
    bool frame_timing_stats = false;
    bool incremental_gc = false;
};

struct DeviceInfo
{
    std::string os;
    std::string cpu;
    std::int32_t cpu_logical_cores = 0;
    std::int64_t system_memory_mb = 0;
    std::string gpu_name;
    std::int64_t gpu_memory_mb = 0;
    std::optional<std::string> gpu_driver; // Unity 读不到驱动版本时为空
};

struct DisplayInfo
{
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::string fullscreen_mode;
    std::int32_t vsync_count = 0;
    std::int32_t target_frame_rate = -1;
    std::string quality_level;
};

struct ScenarioInfo
{
    std::string name;
    std::int64_t seed = 0;
    std::string params_json; // 规范化后的 JSON 文本，Core 只做相等比较
};

// manifest.json 在内存中的表示，描述一次采集的环境与数据列
struct SessionManifest
{
    SchemaVersion schema_version;
    std::string run_id;
    RunStatus status = RunStatus::Completed;
    std::optional<std::string> abort_reason;
    RunContext context = RunContext::Player;
    std::string started_at_utc;
    std::string ended_at_utc;
    std::uint64_t frame_count = 0;
    std::uint64_t warmup_frames = 0;
    std::string frames_file;
    std::string frames_sha256;
    BuildInfo build;
    DeviceInfo device;
    DisplayInfo display;
    ScenarioInfo scenario;
    std::vector<std::string> instrumentation;
    std::vector<ColumnInfo> columns;

    // 按列名查找，找不到时返回空指针
    [[nodiscard]] const ColumnInfo* find_column(std::string_view name) const noexcept;
};

[[nodiscard]] std::string_view to_string(Availability value) noexcept;
[[nodiscard]] std::string_view to_string(RunStatus value) noexcept;
[[nodiscard]] std::string_view to_string(RunContext value) noexcept;
[[nodiscard]] std::string_view to_string(ScriptingBackend value) noexcept;

// 只接受 schema 规定的小写写法，其他输入一律返回空
[[nodiscard]] std::optional<Availability> parse_availability(std::string_view text) noexcept;
[[nodiscard]] std::optional<RunStatus> parse_run_status(std::string_view text) noexcept;
[[nodiscard]] std::optional<RunContext> parse_run_context(std::string_view text) noexcept;
[[nodiscard]] std::optional<ScriptingBackend> parse_scripting_backend(
    std::string_view text) noexcept;

} // namespace gpia
