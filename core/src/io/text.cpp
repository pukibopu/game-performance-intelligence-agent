#include "gpia/io/text.hpp"

#include <algorithm>

namespace gpia
{

std::string sanitize_untrusted(std::string_view text, std::size_t max_length)
{
    std::string result;
    result.reserve(std::min(text.size(), max_length) + 3);
    for (const char c : text.substr(0, max_length))
    {
        result.push_back(c >= 0x20 && c <= 0x7E ? c : '?');
    }
    if (text.size() > max_length)
    {
        result.append("...");
    }
    return result;
}

std::string quote_untrusted(std::string_view text, std::size_t max_length)
{
    return "'" + sanitize_untrusted(text, max_length) + "'";
}

} // namespace gpia
