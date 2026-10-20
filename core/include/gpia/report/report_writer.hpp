#pragma once

#include <string>

#include "gpia/diagnostics.hpp"
#include "gpia/model/report.hpp"

namespace gpia
{

// 按 analysis-report v1 写出 JSON，诊断部分取自 diagnostics
[[nodiscard]] std::string write_report_json(const Report& report, const Diagnostics& diagnostics);

// 把一条诊断写成单行 JSON，供 CLI 逐行输出到 stderr
[[nodiscard]] std::string diagnostic_json_line(const Diagnostic& diagnostic);

} // namespace gpia
