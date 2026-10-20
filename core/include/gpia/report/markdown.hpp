#pragma once

#include <string>

#include "gpia/diagnostics.hpp"
#include "gpia/model/report.hpp"

namespace gpia
{

// 生成给人看的 Markdown 摘要，内容完全来自报告本身
[[nodiscard]] std::string render_markdown(const Report& report, const Diagnostics& diagnostics);

} // namespace gpia
