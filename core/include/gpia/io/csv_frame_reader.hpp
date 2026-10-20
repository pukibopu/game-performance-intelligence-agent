#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gpia/diagnostics.hpp"
#include "gpia/model/frame_record.hpp"
#include "gpia/result.hpp"

namespace gpia
{

struct CsvReadOptions
{
    std::size_t max_errors = 1000; // 错误超过这个数就判定整个文件无效
};

// 解析结果，frames 只包含通过全部校验的行
struct FrameTable
{
    std::vector<std::string> header;
    std::vector<FrameRecord> frames;
    std::uint64_t data_rows = 0;        // 表头之后的非空行数，包含被丢弃的行
    std::uint64_t frame_index_gaps = 0; // frame_index 出现跳号的次数
};

// 解析 frames.csv 文本，逐行问题记入 diagnostics，致命问题返回 Error
[[nodiscard]] Result<FrameTable> parse_frames_csv(std::string_view text, Diagnostics& diagnostics,
                                                  const CsvReadOptions& options = {});

[[nodiscard]] Result<FrameTable> read_frames_csv(const std::filesystem::path& path,
                                                 Diagnostics& diagnostics,
                                                 const CsvReadOptions& options = {});

// frames.csv 定义的全部列名，顺序即推荐的写出顺序
[[nodiscard]] std::span<const std::string_view> known_frame_columns() noexcept;

} // namespace gpia
