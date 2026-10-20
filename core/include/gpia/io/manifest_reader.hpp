#pragma once

#include <filesystem>
#include <string_view>

#include "gpia/diagnostics.hpp"
#include "gpia/model/session_manifest.hpp"
#include "gpia/result.hpp"

namespace gpia
{

// 解析 manifest.json 文本，结构错误都致命，未知字段只发警告
[[nodiscard]] Result<SessionManifest> parse_manifest(std::string_view json_text,
                                                     Diagnostics& diagnostics);

[[nodiscard]] Result<SessionManifest> read_manifest(const std::filesystem::path& path,
                                                    Diagnostics& diagnostics);

// run_id 会出现在证据 ID 里，只允许字母数字下划线和连字符
[[nodiscard]] bool is_valid_run_id(std::string_view run_id) noexcept;

} // namespace gpia
