#include "gpia/io/manifest_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "gpia/io/file_io.hpp"
#include "gpia/io/text.hpp"

namespace gpia
{

namespace
{

using nlohmann::json;

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();

std::string join(const std::string& parent, std::string_view key)
{
    return parent.empty() ? std::string(key) : parent + "." + std::string(key);
}

bool is_lower_hex_sha256(std::string_view text) noexcept
{
    return text.size() == 64 && std::ranges::all_of(text, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

// frames_file 只能是同目录下的普通文件名，防止路径穿越
bool is_plain_file_name(std::string_view name) noexcept
{
    return !name.empty() && name != "." && name != ".." &&
           name.find_first_of("/\\:") == std::string_view::npos;
}

// 收集字段级问题，读完整个 manifest 后再统一判定是否致命
class FieldReader
{
  public:
    explicit FieldReader(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

    [[nodiscard]] std::size_t errors() const noexcept { return errors_; }

    void error(std::string path, std::string message, std::string code = "MANIFEST_INVALID_FIELD")
    {
        ++errors_;
        diagnostics_.add({.severity = Severity::Error,
                          .code = std::move(code),
                          .message = std::move(message),
                          .field = std::move(path)});
    }

    void warn_unknown(const json& object, const std::string& parent,
                      std::initializer_list<std::string_view> known)
    {
        for (const auto& item : object.items())
        {
            if (std::ranges::find(known, std::string_view(item.key())) == known.end())
            {
                diagnostics_.add({.severity = Severity::Warning,
                                  .code = "MANIFEST_UNKNOWN_FIELD",
                                  .message = "ignored unknown field",
                                  .field = join(parent, sanitize_untrusted(item.key()))});
            }
        }
    }

    const json* member(const json& object, std::string_view key, const std::string& path)
    {
        const auto it = object.find(std::string(key));
        if (it == object.end())
        {
            error(path, "required field is missing", "MANIFEST_MISSING_FIELD");
            return nullptr;
        }
        return &*it;
    }

    std::optional<std::string> string(const json& object, std::string_view key,
                                      const std::string& parent)
    {
        const std::string path = join(parent, key);
        const json* value = member(object, key, path);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (!value->is_string())
        {
            error(path, "expected a string");
            return std::nullopt;
        }
        return value->get<std::string>();
    }

    // 字段必须存在，但允许取值为 null
    std::optional<std::optional<std::string>> nullable_string(const json& object,
                                                              std::string_view key,
                                                              const std::string& parent)
    {
        const std::string path = join(parent, key);
        const json* value = member(object, key, path);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (value->is_null())
        {
            return std::optional<std::string>{};
        }
        if (!value->is_string())
        {
            error(path, "expected a string or null");
            return std::nullopt;
        }
        return std::optional<std::string>{value->get<std::string>()};
    }

    std::optional<bool> boolean(const json& object, std::string_view key, const std::string& parent)
    {
        const std::string path = join(parent, key);
        const json* value = member(object, key, path);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (!value->is_boolean())
        {
            error(path, "expected a boolean");
            return std::nullopt;
        }
        return value->get<bool>();
    }

    std::optional<std::int64_t> integer(const json& object, std::string_view key,
                                        const std::string& parent, std::int64_t min,
                                        std::int64_t max = kInt64Max)
    {
        const std::string path = join(parent, key);
        const json* value = member(object, key, path);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (!value->is_number_integer() ||
            (value->is_number_unsigned() &&
             value->get<std::uint64_t>() > static_cast<std::uint64_t>(kInt64Max)))
        {
            error(path, "expected an integer");
            return std::nullopt;
        }
        const auto number = value->get<std::int64_t>();
        if (number < min || number > max)
        {
            error(path, "integer out of range: " + std::to_string(number));
            return std::nullopt;
        }
        return number;
    }

    const json* object(const json& parent_object, std::string_view key, const std::string& parent)
    {
        const std::string path = join(parent, key);
        const json* value = member(parent_object, key, path);
        if (value != nullptr && !value->is_object())
        {
            error(path, "expected an object");
            return nullptr;
        }
        return value;
    }

    const json* array(const json& parent_object, std::string_view key, const std::string& parent)
    {
        const std::string path = join(parent, key);
        const json* value = member(parent_object, key, path);
        if (value != nullptr && !value->is_array())
        {
            error(path, "expected an array");
            return nullptr;
        }
        return value;
    }

  private:
    Diagnostics& diagnostics_;
    std::size_t errors_ = 0;
};

template <typename T, typename U> void assign(std::optional<U> value, T& target)
{
    if (value)
    {
        target = static_cast<T>(std::move(*value));
    }
}

template <typename Enum, typename Parse>
void assign_enum(FieldReader& r, const json& object, std::string_view key,
                 const std::string& parent, Parse parse, Enum& target)
{
    if (const auto text = r.string(object, key, parent))
    {
        if (const auto value = parse(*text))
        {
            target = *value;
        }
        else
        {
            r.error(join(parent, key), "unknown value " + quote_untrusted(*text));
        }
    }
}

void read_build(FieldReader& r, const json& object, BuildInfo& build)
{
    const std::string p = "build";
    r.warn_unknown(object, p,
                   {"unity_version", "build_id", "development_build", "scripting_backend",
                    "graphics_api", "render_pipeline", "frame_timing_stats", "incremental_gc"});
    assign(r.string(object, "unity_version", p), build.unity_version);
    assign(r.string(object, "build_id", p), build.build_id);
    assign(r.boolean(object, "development_build", p), build.development_build);
    assign_enum(r, object, "scripting_backend", p, parse_scripting_backend,
                build.scripting_backend);
    assign(r.string(object, "graphics_api", p), build.graphics_api);
    assign(r.string(object, "render_pipeline", p), build.render_pipeline);
    assign(r.boolean(object, "frame_timing_stats", p), build.frame_timing_stats);
    assign(r.boolean(object, "incremental_gc", p), build.incremental_gc);
}

void read_device(FieldReader& r, const json& object, DeviceInfo& device)
{
    const std::string p = "device";
    r.warn_unknown(object, p,
                   {"os", "cpu", "cpu_logical_cores", "system_memory_mb", "gpu_name",
                    "gpu_memory_mb", "gpu_driver"});
    assign(r.string(object, "os", p), device.os);
    assign(r.string(object, "cpu", p), device.cpu);
    assign(r.integer(object, "cpu_logical_cores", p, 1, std::numeric_limits<std::int32_t>::max()),
           device.cpu_logical_cores);
    assign(r.integer(object, "system_memory_mb", p, 1), device.system_memory_mb);
    assign(r.string(object, "gpu_name", p), device.gpu_name);
    assign(r.integer(object, "gpu_memory_mb", p, 0), device.gpu_memory_mb);
    assign(r.nullable_string(object, "gpu_driver", p), device.gpu_driver);
}

void read_display(FieldReader& r, const json& object, DisplayInfo& display)
{
    const std::string p = "display";
    constexpr std::int64_t kInt32Max = std::numeric_limits<std::int32_t>::max();
    r.warn_unknown(object, p,
                   {"width", "height", "fullscreen_mode", "vsync_count", "target_frame_rate",
                    "quality_level"});
    assign(r.integer(object, "width", p, 1, kInt32Max), display.width);
    assign(r.integer(object, "height", p, 1, kInt32Max), display.height);
    assign(r.string(object, "fullscreen_mode", p), display.fullscreen_mode);
    assign(r.integer(object, "vsync_count", p, 0, 4), display.vsync_count);
    assign(r.integer(object, "target_frame_rate", p, -1, kInt32Max), display.target_frame_rate);
    assign(r.string(object, "quality_level", p), display.quality_level);
}

void read_scenario(FieldReader& r, const json& object, ScenarioInfo& scenario)
{
    const std::string p = "scenario";
    r.warn_unknown(object, p, {"name", "seed", "params"});
    assign(r.string(object, "name", p), scenario.name);
    assign(r.integer(object, "seed", p, std::numeric_limits<std::int64_t>::min()), scenario.seed);
    // nlohmann 的对象按键排序，dump 结果天然是规范形式
    if (const json* params = r.object(object, "params", p))
    {
        scenario.params_json = params->dump();
    }
}

void read_columns(FieldReader& r, const json& array, std::vector<ColumnInfo>& columns)
{
    for (std::size_t i = 0; i < array.size(); ++i)
    {
        const std::string p = "columns[" + std::to_string(i) + "]";
        const json& item = array[i];
        if (!item.is_object())
        {
            r.error(p, "expected an object");
            continue;
        }
        r.warn_unknown(item, p, {"name", "unit", "availability", "source"});
        ColumnInfo column;
        assign(r.string(item, "name", p), column.name);
        assign(r.string(item, "unit", p), column.unit);
        assign_enum(r, item, "availability", p, parse_availability, column.availability);
        assign(r.string(item, "source", p), column.source);
        columns.push_back(std::move(column));
    }
}

Error invalid_input(std::string message)
{
    return Error{.code = ErrorCode::InvalidInput, .message = std::move(message)};
}

} // namespace

bool is_valid_run_id(std::string_view run_id) noexcept
{
    return !run_id.empty() && run_id.size() <= 64 && std::ranges::all_of(run_id, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}

Result<SessionManifest> parse_manifest(std::string_view json_text, Diagnostics& diagnostics)
{
    json root;
    try
    {
        root = json::parse(json_text);
    }
    catch (const json::parse_error& e)
    {
        // 异常消息里可能夹带原始输入字节，先清洗再记录
        const std::string message = "invalid JSON: " + sanitize_untrusted(e.what(), 200);
        diagnostics.add(
            {.severity = Severity::Error, .code = "MANIFEST_INVALID_JSON", .message = message});
        return invalid_input("manifest.json " + message);
    }
    if (!root.is_object())
    {
        diagnostics.add({.severity = Severity::Error,
                         .code = "MANIFEST_INVALID_JSON",
                         .message = "top level must be an object"});
        return invalid_input("manifest.json top level must be an object");
    }

    FieldReader r{diagnostics};
    SessionManifest m;

    // 先确认版本，major 不同时其余结构可能完全不同
    const auto version_text = r.string(root, "schema_version", "");
    if (!version_text)
    {
        return invalid_input("manifest.json has no usable schema_version");
    }
    const auto version = parse_schema_version(*version_text);
    if (!version)
    {
        r.error("schema_version", "not a major.minor version: " + quote_untrusted(*version_text));
        return invalid_input("manifest.json has an invalid schema_version");
    }
    if (!is_supported(*version))
    {
        r.error("schema_version", "unsupported major version " + to_string(*version),
                "MANIFEST_UNSUPPORTED_VERSION");
        return invalid_input("manifest.json schema " + to_string(*version) + " is not supported");
    }
    m.schema_version = *version;

    r.warn_unknown(root, "",
                   {"schema_version", "run_id", "status", "abort_reason", "context",
                    "started_at_utc", "ended_at_utc", "frame_count", "warmup_frames", "frames_file",
                    "frames_sha256", "build", "device", "display", "scenario", "instrumentation",
                    "columns"});

    if (const auto run_id = r.string(root, "run_id", ""))
    {
        if (is_valid_run_id(*run_id))
        {
            m.run_id = *run_id;
        }
        else
        {
            r.error("run_id", "run_id may only contain letters, digits, '_' and '-'");
        }
    }
    assign_enum(r, root, "status", "", parse_run_status, m.status);
    if (root.contains("abort_reason"))
    {
        assign(r.nullable_string(root, "abort_reason", ""), m.abort_reason);
    }
    if (m.status == RunStatus::Aborted && (!m.abort_reason || m.abort_reason->empty()))
    {
        r.error("abort_reason", "required when status is aborted", "MANIFEST_MISSING_FIELD");
    }
    assign_enum(r, root, "context", "", parse_run_context, m.context);
    assign(r.string(root, "started_at_utc", ""), m.started_at_utc);
    assign(r.string(root, "ended_at_utc", ""), m.ended_at_utc);
    assign(r.integer(root, "frame_count", "", 0), m.frame_count);
    assign(r.integer(root, "warmup_frames", "", 0), m.warmup_frames);

    if (const auto frames_file = r.string(root, "frames_file", ""))
    {
        if (is_plain_file_name(*frames_file))
        {
            m.frames_file = *frames_file;
        }
        else
        {
            r.error("frames_file", "must be a plain file name in the run directory");
        }
    }
    if (const auto sha = r.string(root, "frames_sha256", ""))
    {
        if (is_lower_hex_sha256(*sha))
        {
            m.frames_sha256 = *sha;
        }
        else
        {
            r.error("frames_sha256", "must be 64 lowercase hex digits");
        }
    }

    if (const json* build = r.object(root, "build", ""))
    {
        read_build(r, *build, m.build);
    }
    if (const json* device = r.object(root, "device", ""))
    {
        read_device(r, *device, m.device);
    }
    if (const json* display = r.object(root, "display", ""))
    {
        read_display(r, *display, m.display);
    }
    if (const json* scenario = r.object(root, "scenario", ""))
    {
        read_scenario(r, *scenario, m.scenario);
    }
    if (const json* instrumentation = r.array(root, "instrumentation", ""))
    {
        for (std::size_t i = 0; i < instrumentation->size(); ++i)
        {
            const json& item = (*instrumentation)[i];
            if (item.is_string())
            {
                m.instrumentation.push_back(item.get<std::string>());
            }
            else
            {
                r.error("instrumentation[" + std::to_string(i) + "]", "expected a string");
            }
        }
    }
    if (const json* columns = r.array(root, "columns", ""))
    {
        read_columns(r, *columns, m.columns);
    }

    if (r.errors() > 0)
    {
        return invalid_input("manifest.json has " + std::to_string(r.errors()) + " invalid fields");
    }
    return m;
}

Result<SessionManifest> read_manifest(const std::filesystem::path& path, Diagnostics& diagnostics)
{
    Result<std::string> bytes = read_file_bytes(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return parse_manifest(*bytes, diagnostics);
}

} // namespace gpia
