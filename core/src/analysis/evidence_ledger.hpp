#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gpia/model/report.hpp"

namespace gpia::analysis
{

inline Scalar as_int(std::int64_t value)
{
    return Scalar{value};
}

inline Scalar as_real(double value)
{
    return Scalar{value};
}

inline Scalar as_text(std::string value)
{
    return Scalar{std::move(value)};
}

// 统一分配证据与结论的 ID，保证结论引用的证据一定存在
class EvidenceLedger
{
  public:
    explicit EvidenceLedger(std::string run_id) : run_id_(std::move(run_id)) {}

    std::string add_evidence(EvidenceType type, std::string description,
                             std::optional<FrameRange> range, Fields data);

    void add_finding(Finding finding);

    [[nodiscard]] std::vector<Evidence> take_evidence() noexcept { return std::move(evidence_); }
    [[nodiscard]] std::vector<Finding> take_findings() noexcept { return std::move(findings_); }

  private:
    [[nodiscard]] bool has_evidence(const std::string& id) const noexcept;

    std::string run_id_;
    std::vector<Evidence> evidence_;
    std::vector<Finding> findings_;
};

} // namespace gpia::analysis
