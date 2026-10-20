#include "gpia/schema_version.hpp"

#include <charconv>
#include <system_error>

namespace gpia
{

namespace
{

// 整段都必须是无符号十进制数，空串和正负号都不接受
std::optional<std::uint32_t> parse_uint(std::string_view text) noexcept
{
    if (text.empty())
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const char* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<SchemaVersion> parse_schema_version(std::string_view text) noexcept
{
    const auto dot = text.find('.');
    if (dot == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto major = parse_uint(text.substr(0, dot));
    const auto minor = parse_uint(text.substr(dot + 1));
    if (!major || !minor)
    {
        return std::nullopt;
    }
    return SchemaVersion{.major = *major, .minor = *minor};
}

std::string to_string(SchemaVersion version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor);
}

} // namespace gpia
