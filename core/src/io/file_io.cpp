#include "gpia/io/file_io.hpp"

#include <fstream>
#include <ios>
#include <system_error>

namespace gpia
{

std::string path_to_utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

Result<std::string> read_file_bytes(const std::filesystem::path& path)
{
    // 只写系统错误码而不写本地化消息，避免把非 UTF-8 文本带进报告
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return Error{.code = ErrorCode::IoError,
                     .message = "cannot read " + path_to_utf8(path) + ", system error " +
                                std::to_string(ec.value())};
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return Error{.code = ErrorCode::IoError, .message = "cannot open " + path_to_utf8(path)};
    }

    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !in.read(bytes.data(), static_cast<std::streamsize>(size)))
    {
        return Error{.code = ErrorCode::IoError, .message = "short read on " + path_to_utf8(path)};
    }
    return bytes;
}

} // namespace gpia
