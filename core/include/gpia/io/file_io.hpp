#pragma once

#include <filesystem>
#include <string>

#include "gpia/result.hpp"

namespace gpia
{

// 按字节原样读入整个文件，失败时返回 IoError
[[nodiscard]] Result<std::string> read_file_bytes(const std::filesystem::path& path);

// 把路径转成 UTF-8 文本，供诊断信息和报告使用
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

} // namespace gpia
