#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gpia
{

// 截断不可信文本并把非可打印 ASCII 字节换成问号，结果可以安全写进报告
[[nodiscard]] std::string sanitize_untrusted(std::string_view text, std::size_t max_length = 64);

// 带单引号的 sanitize_untrusted，用于诊断消息里引用输入内容
[[nodiscard]] std::string quote_untrusted(std::string_view text, std::size_t max_length = 32);

} // namespace gpia
