#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gpia
{

// 形如 1.0 的 schema 版本，major 不同即不兼容
struct SchemaVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;

    friend auto operator<=>(const SchemaVersion&, const SchemaVersion&) = default;
};

// 本版 Core 能读写的 major 版本
inline constexpr std::uint32_t kSupportedSchemaMajor = 1;

// 解析 major.minor，多余字符、空段或负号都视为无效
[[nodiscard]] std::optional<SchemaVersion> parse_schema_version(std::string_view text) noexcept;

[[nodiscard]] std::string to_string(SchemaVersion version);

[[nodiscard]] constexpr bool is_supported(SchemaVersion version) noexcept
{
    return version.major == kSupportedSchemaMajor;
}

} // namespace gpia
