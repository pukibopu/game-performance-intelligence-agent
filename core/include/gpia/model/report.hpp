#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gpia/schema_version.hpp"
#include "gpia/stats/summary.hpp"

namespace gpia
{

enum class Outcome : std::uint8_t
{
    Ok,
    OkWithWarnings,
    InsufficientData,
    InvalidInput,
};

enum class EvidenceType : std::uint8_t
{
    ThresholdCrossing,
    CounterDelta,
    CoOccurrence,
    Statistic,
    DataAvailability,
};

enum class FindingKind : std::uint8_t
{
    FrameHitch,
    CpuSpike,
    GcAllocSpike,
    GcCollectionHitch,
    MemoryGrowth,
    InsufficientData,
};

enum class FindingSeverity : std::uint8_t
{
    Info,
    Low,
    Medium,
    High,
};

// 证据等级从强到弱排列，Agent 不能把结论说得比这里更强
enum class EvidenceLevel : std::uint8_t
{
    Measured,
    DeterministicFinding,
    EvidenceBackedDiagnosis,
    Hypothesis,
    InsufficientEvidence,
};

// 报告里的标量取值，写出时对应 JSON 的整数、浮点、字符串或布尔
using Scalar = std::variant<std::int64_t, double, std::string, bool>;
using Fields = std::vector<std::pair<std::string, Scalar>>;

struct FrameRange
{
    std::uint64_t first = 0;
    std::uint64_t last = 0;
};

struct Evidence
{
    std::string id;
    EvidenceType type = EvidenceType::Statistic;
    std::string description;
    std::optional<FrameRange> frame_range;
    Fields data;
};

struct Finding
{
    std::string id;
    FindingKind kind = FindingKind::FrameHitch;
    FindingSeverity severity = FindingSeverity::Info;
    EvidenceLevel level = EvidenceLevel::DeterministicFinding;
    std::string title;
    std::optional<FrameRange> frame_range;
    std::vector<std::pair<std::string, double>> metrics;
    std::vector<std::string> evidence_ids;
    std::string rule;
    Fields rule_params;
    std::vector<std::string> missing_evidence;
};

struct InputInfo
{
    std::string frames_sha256;
    std::uint64_t frames_total = 0;
    std::uint64_t frames_analyzed = 0;
    std::uint64_t warmup_frames_skipped = 0;
    std::uint64_t frame_index_gaps = 0;
    std::uint64_t invalid_rows = 0;
};

struct ColumnSummary
{
    std::string column;
    stats::Summary summary;
};

// analysis-report 在内存中的表示，诊断信息另由 Diagnostics 保存
struct Report
{
    SchemaVersion schema_version{.major = 1, .minor = 0};
    std::string run_id;
    Outcome outcome = Outcome::Ok;
    std::string config_json; // 实际生效的分析配置，派生值已经填好
    InputInfo input;
    std::vector<ColumnSummary> summary;
    std::vector<Evidence> evidence;
    std::vector<Finding> findings;
};

[[nodiscard]] std::string_view to_string(Outcome value) noexcept;
[[nodiscard]] std::string_view to_string(EvidenceType value) noexcept;
[[nodiscard]] std::string_view to_string(FindingKind value) noexcept;
[[nodiscard]] std::string_view to_string(FindingSeverity value) noexcept;
[[nodiscard]] std::string_view to_string(EvidenceLevel value) noexcept;

} // namespace gpia
