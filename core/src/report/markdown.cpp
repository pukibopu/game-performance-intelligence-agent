#include "gpia/report/markdown.hpp"

#include <format>
#include <optional>
#include <string_view>

#include "gpia/version.hpp"

namespace gpia
{

namespace
{

constexpr std::size_t kMaxDiagnosticsShown = 20;

// 表格单元格里的竖线会打乱 Markdown 表格结构
std::string escape_cell(std::string_view text)
{
    std::string result;
    for (const char c : text)
    {
        if (c == '|')
        {
            result += "\\|";
        }
        else if (c == '\n')
        {
            result += ' ';
        }
        else
        {
            result += c;
        }
    }
    return result;
}

std::string format_value(std::string_view column, const std::optional<double>& value)
{
    if (!value)
    {
        return "-";
    }
    if (column.ends_with("_bytes"))
    {
        return std::format("{:.2f} MiB", *value / (1024.0 * 1024.0));
    }
    return std::format("{:.3f}", *value);
}

std::string frames_text(const std::optional<FrameRange>& range)
{
    if (!range)
    {
        return "-";
    }
    return range->first == range->last ? std::to_string(range->first)
                                       : std::format("{}-{}", range->first, range->last);
}

std::string_view severity_name(Severity severity) noexcept
{
    switch (severity)
    {
        case Severity::Info:
            return "info";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "error";
}

} // namespace

std::string render_markdown(const Report& report, const Diagnostics& diagnostics)
{
    std::string md;
    md += std::format("# GPIA analysis report: {}\n\n", report.run_id);
    md += std::format("- Outcome: **{}**\n", to_string(report.outcome));
    md += std::format("- Analyzer: gpia-core {}\n", version());
    md += std::format(
        "- Frames: {} analyzed of {} rows, {} warm-up skipped, {} invalid rows, {} index gaps\n\n",
        report.input.frames_analyzed, report.input.frames_total, report.input.warmup_frames_skipped,
        report.input.invalid_rows, report.input.frame_index_gaps);

    md += "## Summary\n\n";
    md += "| Metric | Count | Missing | Mean | P50 | P95 | P99 | Max |\n";
    md += "|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const ColumnSummary& c : report.summary)
    {
        const auto& s = c.summary;
        md += std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n", c.column, s.count,
                          s.missing, format_value(c.column, s.mean), format_value(c.column, s.p50),
                          format_value(c.column, s.p95), format_value(c.column, s.p99),
                          format_value(c.column, s.max));
    }

    md += std::format("\n## Findings ({})\n\n", report.findings.size());
    if (report.findings.empty())
    {
        md += "No findings.\n";
    }
    else
    {
        md += "| ID | Kind | Evidence level | Severity | Frames | Title |\n";
        md += "|---|---|---|---|---|---|\n";
        for (const Finding& f : report.findings)
        {
            md += std::format("| `{}` | {} | {} | {} | {} | {} |\n", f.id, to_string(f.kind),
                              to_string(f.level), to_string(f.severity), frames_text(f.frame_range),
                              escape_cell(f.title));
        }
    }

    bool has_missing = false;
    for (const Finding& f : report.findings)
    {
        for (const std::string& missing : f.missing_evidence)
        {
            if (!has_missing)
            {
                md += "\n### Evidence needed to strengthen these findings\n\n";
                has_missing = true;
            }
            md += std::format("- `{}`: {}\n", f.id, escape_cell(missing));
        }
    }

    md += "\n## Diagnostics\n\n";
    md += std::format("{} errors, {} warnings, {} info", diagnostics.count(Severity::Error),
                      diagnostics.count(Severity::Warning), diagnostics.count(Severity::Info));
    md += diagnostics.suppressed_count() > 0
              ? std::format(", {} not stored\n\n", diagnostics.suppressed_count())
              : std::string("\n\n");
    std::size_t shown = 0;
    for (const Diagnostic& d : diagnostics.items())
    {
        if (shown++ == kMaxDiagnosticsShown)
        {
            md += "- ...\n";
            break;
        }
        md += std::format("- {} `{}`", severity_name(d.severity), d.code);
        if (d.line)
        {
            md += std::format(" line {}", *d.line);
        }
        if (d.field)
        {
            md += std::format(" `{}`", *d.field);
        }
        md += std::format(": {}\n", escape_cell(d.message));
    }

    md += "\n> Evidence levels from strongest to weakest: measured, deterministic_finding, "
          "evidence_backed_diagnosis, hypothesis, insufficient_evidence.\n";
    return md;
}

} // namespace gpia
