#pragma once

#include <filesystem>
#include <string>

#include "gpia/diagnostics.hpp"
#include "gpia/io/csv_frame_reader.hpp"
#include "gpia/model/session_manifest.hpp"
#include "gpia/result.hpp"

namespace gpia
{

struct LoadOptions
{
    CsvReadOptions csv;
};

struct LoadedRun
{
    SessionManifest manifest;
    FrameTable table;
    std::string frames_sha256; // 实际计算出的摘要
};

// 读取并交叉校验一个 run 目录，manifest 与数据不配套时拒绝分析
[[nodiscard]] Result<LoadedRun> load_run(const std::filesystem::path& run_dir,
                                         Diagnostics& diagnostics, const LoadOptions& options = {});

} // namespace gpia
