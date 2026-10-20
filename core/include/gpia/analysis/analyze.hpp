#pragma once

#include "gpia/analysis/config.hpp"
#include "gpia/diagnostics.hpp"
#include "gpia/io/run_loader.hpp"
#include "gpia/model/report.hpp"

namespace gpia
{

// 对已校验的 run 做全部确定性分析，全程不调用 LLM
[[nodiscard]] Report analyze_run(const LoadedRun& run, const AnalysisConfig& config,
                                 const Diagnostics& diagnostics);

} // namespace gpia
