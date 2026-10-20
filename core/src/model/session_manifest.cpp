#include "gpia/model/session_manifest.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace gpia
{

namespace
{

// 每个枚举只维护一张名称表，双向转换都查这张表
template <typename Enum, std::size_t N>
using NameTable = std::array<std::pair<Enum, std::string_view>, N>;

constexpr NameTable<Availability, 2> kAvailabilityNames{{
    {Availability::Measured, "measured"},
    {Availability::Unsupported, "unsupported"},
}};

constexpr NameTable<RunStatus, 2> kRunStatusNames{{
    {RunStatus::Completed, "completed"},
    {RunStatus::Aborted, "aborted"},
}};

constexpr NameTable<RunContext, 2> kRunContextNames{{
    {RunContext::Player, "player"},
    {RunContext::Editor, "editor"},
}};

constexpr NameTable<ScriptingBackend, 2> kScriptingBackendNames{{
    {ScriptingBackend::Mono, "mono"},
    {ScriptingBackend::Il2Cpp, "il2cpp"},
}};

template <typename Enum, std::size_t N>
constexpr std::string_view name_of(const NameTable<Enum, N>& table, Enum value) noexcept
{
    for (const auto& [entry, name] : table)
    {
        if (entry == value)
        {
            return name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
constexpr std::optional<Enum> value_of(const NameTable<Enum, N>& table,
                                       std::string_view text) noexcept
{
    for (const auto& [entry, name] : table)
    {
        if (name == text)
        {
            return entry;
        }
    }
    return std::nullopt;
}

} // namespace

const ColumnInfo* SessionManifest::find_column(std::string_view name) const noexcept
{
    for (const ColumnInfo& column : columns)
    {
        if (column.name == name)
        {
            return &column;
        }
    }
    return nullptr;
}

std::string_view to_string(Availability value) noexcept
{
    return name_of(kAvailabilityNames, value);
}

std::string_view to_string(RunStatus value) noexcept
{
    return name_of(kRunStatusNames, value);
}

std::string_view to_string(RunContext value) noexcept
{
    return name_of(kRunContextNames, value);
}

std::string_view to_string(ScriptingBackend value) noexcept
{
    return name_of(kScriptingBackendNames, value);
}

std::optional<Availability> parse_availability(std::string_view text) noexcept
{
    return value_of(kAvailabilityNames, text);
}

std::optional<RunStatus> parse_run_status(std::string_view text) noexcept
{
    return value_of(kRunStatusNames, text);
}

std::optional<RunContext> parse_run_context(std::string_view text) noexcept
{
    return value_of(kRunContextNames, text);
}

std::optional<ScriptingBackend> parse_scripting_backend(std::string_view text) noexcept
{
    return value_of(kScriptingBackendNames, text);
}

} // namespace gpia
