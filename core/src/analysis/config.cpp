#include "gpia/analysis/config.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <nlohmann/json.hpp>

#include "gpia/io/text.hpp"

namespace gpia
{

namespace
{

using nlohmann::json;

class ConfigReader
{
  public:
    explicit ConfigReader(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

    [[nodiscard]] std::size_t errors() const noexcept { return errors_; }

    void warn_unknown(const json& object, const std::string& path,
                      std::initializer_list<std::string_view> known)
    {
        for (const auto& item : object.items())
        {
            if (std::ranges::find(known, std::string_view(item.key())) == known.end())
            {
                diagnostics_.add({.severity = Severity::Warning,
                                  .code = "CONFIG_UNKNOWN_FIELD",
                                  .message = "ignored unknown config field",
                                  .field = path + sanitize_untrusted(item.key())});
            }
        }
    }

    void number(const json& object, std::string_view key, const std::string& path, double& target,
                double min)
    {
        const auto it = object.find(std::string(key));
        if (it == object.end())
        {
            return;
        }
        if (!it->is_number() || it->get<double>() < min)
        {
            fail(path + std::string(key), "expected a number of at least " + std::to_string(min));
            return;
        }
        target = it->get<double>();
    }

    void count(const json& object, std::string_view key, const std::string& path,
               std::size_t& target, std::size_t min)
    {
        const auto it = object.find(std::string(key));
        if (it == object.end())
        {
            return;
        }
        if (!it->is_number_unsigned() || it->get<std::size_t>() < min)
        {
            fail(path + std::string(key), "expected an integer of at least " + std::to_string(min));
            return;
        }
        target = it->get<std::size_t>();
    }

    const json* section(const json& object, std::string_view key)
    {
        const auto it = object.find(std::string(key));
        if (it == object.end())
        {
            return nullptr;
        }
        if (!it->is_object())
        {
            fail(std::string(key), "expected an object");
            return nullptr;
        }
        return &*it;
    }

  private:
    void fail(std::string path, std::string message)
    {
        ++errors_;
        diagnostics_.add({.severity = Severity::Error,
                          .code = "CONFIG_INVALID_FIELD",
                          .message = std::move(message),
                          .field = std::move(path)});
    }

    Diagnostics& diagnostics_;
    std::size_t errors_ = 0;
};

} // namespace

Result<AnalysisConfig> parse_analysis_config(std::string_view json_text, Diagnostics& diagnostics)
{
    json root;
    try
    {
        root = json::parse(json_text);
    }
    catch (const json::parse_error& e)
    {
        const std::string message = "invalid config JSON: " + sanitize_untrusted(e.what(), 200);
        diagnostics.add(
            {.severity = Severity::Error, .code = "CONFIG_INVALID_JSON", .message = message});
        return Error{.code = ErrorCode::InvalidInput, .message = message};
    }
    if (!root.is_object())
    {
        diagnostics.add({.severity = Severity::Error,
                         .code = "CONFIG_INVALID_JSON",
                         .message = "config top level must be an object"});
        return Error{.code = ErrorCode::InvalidInput,
                     .message = "config top level must be an object"};
    }

    AnalysisConfig config;
    ConfigReader r{diagnostics};
    r.warn_unknown(root, "", {"min_frames", "hitch", "cpu", "gc_alloc", "memory"});
    r.count(root, "min_frames", "", config.min_frames, 1);
    if (const json* hitch = r.section(root, "hitch"))
    {
        r.warn_unknown(*hitch, "hitch.", {"window", "k", "floor_ms"});
        r.count(*hitch, "window", "hitch.", config.hitch.window, 1);
        r.number(*hitch, "k", "hitch.", config.hitch.k, 1.0);
        if (hitch->contains("floor_ms"))
        {
            double floor = 0.0;
            r.number(*hitch, "floor_ms", "hitch.", floor, 0.0);
            config.hitch.floor_ms = floor;
        }
    }
    if (const json* cpu = r.section(root, "cpu"))
    {
        r.warn_unknown(*cpu, "cpu.", {"window", "k"});
        r.count(*cpu, "window", "cpu.", config.cpu.window, 1);
        r.number(*cpu, "k", "cpu.", config.cpu.k, 1.0);
    }
    if (const json* gc = r.section(root, "gc_alloc"))
    {
        r.warn_unknown(*gc, "gc_alloc.", {"k", "min_bytes"});
        r.number(*gc, "k", "gc_alloc.", config.gc_alloc.k, 0.0);
        r.number(*gc, "min_bytes", "gc_alloc.", config.gc_alloc.min_bytes, 0.0);
    }
    if (const json* memory = r.section(root, "memory"))
    {
        r.warn_unknown(
            *memory, "memory.",
            {"min_slope_mib_per_s", "min_r_squared", "min_duration_s", "min_collections"});
        r.number(*memory, "min_slope_mib_per_s", "memory.", config.memory.min_slope_mib_per_s, 0.0);
        r.number(*memory, "min_r_squared", "memory.", config.memory.min_r_squared, 0.0);
        r.number(*memory, "min_duration_s", "memory.", config.memory.min_duration_s, 0.0);
        r.count(*memory, "min_collections", "memory.", config.memory.min_collections, 2);
    }
    if (r.errors() > 0)
    {
        return Error{.code = ErrorCode::InvalidInput,
                     .message = "config has " + std::to_string(r.errors()) + " invalid fields"};
    }
    return config;
}

std::string to_json_text(const AnalysisConfig& config)
{
    json hitch{{"window", config.hitch.window}, {"k", config.hitch.k}};
    hitch["floor_ms"] = config.hitch.floor_ms ? json(*config.hitch.floor_ms) : json(nullptr);
    const json root{
        {"min_frames", config.min_frames},
        {"hitch", hitch},
        {"cpu", {{"window", config.cpu.window}, {"k", config.cpu.k}}},
        {"gc_alloc", {{"k", config.gc_alloc.k}, {"min_bytes", config.gc_alloc.min_bytes}}},
        {"memory",
         {{"min_slope_mib_per_s", config.memory.min_slope_mib_per_s},
          {"min_r_squared", config.memory.min_r_squared},
          {"min_duration_s", config.memory.min_duration_s},
          {"min_collections", config.memory.min_collections}}},
    };
    return root.dump();
}

} // namespace gpia
