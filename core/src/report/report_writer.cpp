#include "gpia/report/report_writer.hpp"

#include <cassert>
#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "gpia/version.hpp"

namespace gpia
{

namespace
{

// 保留插入顺序，让输出的字段顺序与 schema 一致，便于人工阅读
using json = nlohmann::ordered_json;

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

json number(double value)
{
    // NaN 或无穷写进 JSON 会变成 null，出现即说明分析器有 bug
    assert(std::isfinite(value));
    return json(value);
}

json to_json(const Scalar& scalar)
{
    return std::visit(
        [](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>)
            {
                return number(v);
            }
            else
            {
                return json(v);
            }
        },
        scalar);
}

json to_json(const Fields& fields)
{
    json object = json::object();
    for (const auto& [key, value] : fields)
    {
        object[key] = to_json(value);
    }
    return object;
}

json to_json(const FrameRange& range)
{
    return json{{"first", range.first}, {"last", range.last}};
}

json to_json(const stats::Summary& s)
{
    json object{{"count", s.count}, {"missing", s.missing}};
    const std::pair<const char*, const std::optional<double>*> optional_fields[] = {
        {"mean", &s.mean}, {"stddev", &s.stddev}, {"min", &s.min}, {"max", &s.max},
        {"p50", &s.p50},   {"p95", &s.p95},       {"p99", &s.p99},
    };
    for (const auto& [key, value] : optional_fields)
    {
        if (*value)
        {
            object[key] = number(**value);
        }
    }
    return object;
}

json to_json(const Diagnostic& d)
{
    json object{{"severity", std::string(severity_name(d.severity))},
                {"code", d.code},
                {"message", d.message}};
    if (d.line)
    {
        object["line"] = *d.line;
    }
    if (d.field)
    {
        object["field"] = *d.field;
    }
    return object;
}

json to_json(const Evidence& e)
{
    json object{
        {"id", e.id}, {"type", std::string(to_string(e.type))}, {"description", e.description}};
    if (e.frame_range)
    {
        object["frame_range"] = to_json(*e.frame_range);
    }
    object["data"] = to_json(e.data);
    return object;
}

json to_json(const Finding& f)
{
    json object{{"id", f.id},
                {"kind", std::string(to_string(f.kind))},
                {"severity", std::string(to_string(f.severity))},
                {"evidence_level", std::string(to_string(f.level))},
                {"title", f.title}};
    if (f.frame_range)
    {
        object["frame_range"] = to_json(*f.frame_range);
    }
    json metrics = json::object();
    for (const auto& [key, value] : f.metrics)
    {
        metrics[key] = number(value);
    }
    object["metrics"] = metrics;
    object["evidence_ids"] = f.evidence_ids;
    object["rule"] = json{{"name", f.rule}, {"params", to_json(f.rule_params)}};
    object["missing_evidence"] = f.missing_evidence;
    return object;
}

} // namespace

std::string write_report_json(const Report& report, const Diagnostics& diagnostics)
{
    json root;
    root["schema_version"] = to_string(report.schema_version);
    root["report_kind"] = "analysis";
    root["run_id"] = report.run_id;
    root["outcome"] = std::string(to_string(report.outcome));
    root["analyzer"] = json{{"name", "gpia-core"},
                            {"version", std::string(version())},
                            {"config", json::parse(report.config_json)}};
    root["input"] = json{{"frames_sha256", report.input.frames_sha256},
                         {"frames_total", report.input.frames_total},
                         {"frames_analyzed", report.input.frames_analyzed},
                         {"warmup_frames_skipped", report.input.warmup_frames_skipped},
                         {"frame_index_gaps", report.input.frame_index_gaps},
                         {"invalid_rows", report.input.invalid_rows}};

    json summary = json::object();
    for (const ColumnSummary& column : report.summary)
    {
        summary[column.column] = to_json(column.summary);
    }
    root["summary"] = summary;

    json evidence = json::array();
    for (const Evidence& e : report.evidence)
    {
        evidence.push_back(to_json(e));
    }
    root["evidence"] = evidence;

    json findings = json::array();
    for (const Finding& f : report.findings)
    {
        findings.push_back(to_json(f));
    }
    root["findings"] = findings;

    json items = json::array();
    for (const Diagnostic& d : diagnostics.items())
    {
        items.push_back(to_json(d));
    }
    if (diagnostics.suppressed_count() > 0)
    {
        items.push_back(json{{"severity", "info"},
                             {"code", "DIAGNOSTICS_SUPPRESSED"},
                             {"message", std::to_string(diagnostics.suppressed_count()) +
                                             " more diagnostics were counted but not stored"}});
    }
    root["diagnostics"] = items;
    return root.dump(2) + "\n";
}

std::string diagnostic_json_line(const Diagnostic& diagnostic)
{
    return to_json(diagnostic).dump();
}

} // namespace gpia
