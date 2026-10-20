#include "evidence_ledger.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace gpia::analysis
{

std::string EvidenceLedger::add_evidence(EvidenceType type, std::string description,
                                         std::optional<FrameRange> range, Fields data)
{
    std::string id = "ev:" + run_id_ + ":" + std::to_string(evidence_.size() + 1);
    evidence_.push_back(Evidence{.id = id,
                                 .type = type,
                                 .description = std::move(description),
                                 .frame_range = range,
                                 .data = std::move(data)});
    return id;
}

void EvidenceLedger::add_finding(Finding finding)
{
    // 引用不存在的证据属于分析器自身的 bug
    assert(!finding.evidence_ids.empty());
    assert(std::ranges::all_of(finding.evidence_ids,
                               [this](const std::string& id) { return has_evidence(id); }));
    finding.id = "fd:" + run_id_ + ":" + std::to_string(findings_.size() + 1);
    findings_.push_back(std::move(finding));
}

bool EvidenceLedger::has_evidence(const std::string& id) const noexcept
{
    return std::ranges::any_of(evidence_, [&](const Evidence& e) { return e.id == id; });
}

} // namespace gpia::analysis
