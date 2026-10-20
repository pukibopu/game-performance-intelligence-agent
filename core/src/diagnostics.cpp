#include "gpia/diagnostics.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace gpia
{

namespace
{

// 只按 ASCII 判断，不受 locale 影响
constexpr bool is_upper(char c) noexcept
{
    return c >= 'A' && c <= 'Z';
}

constexpr bool is_code_char(char c) noexcept
{
    return is_upper(c) || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

bool is_valid_diagnostic_code(std::string_view code) noexcept
{
    return !code.empty() && is_upper(code.front()) && std::ranges::all_of(code, is_code_char);
}

Diagnostics::Diagnostics(std::size_t max_stored) : max_stored_(max_stored) {}

void Diagnostics::add(Diagnostic diagnostic)
{
    // 诊断码格式错误是程序 bug，所以用 assert 而不是返回错误
    assert(is_valid_diagnostic_code(diagnostic.code));

    ++counts_[static_cast<std::size_t>(diagnostic.severity)];
    if (items_.size() < max_stored_)
    {
        items_.push_back(std::move(diagnostic));
    }
    else
    {
        ++suppressed_;
    }
}

std::size_t Diagnostics::count(Severity severity) const noexcept
{
    return counts_[static_cast<std::size_t>(severity)];
}

} // namespace gpia
