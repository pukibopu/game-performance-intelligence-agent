#include "gpia/model/report.hpp"

namespace gpia
{

std::string_view to_string(Outcome value) noexcept
{
    switch (value)
    {
        case Outcome::Ok:
            return "ok";
        case Outcome::OkWithWarnings:
            return "ok_with_warnings";
        case Outcome::InsufficientData:
            return "insufficient_data";
        case Outcome::InvalidInput:
            return "invalid_input";
    }
    return "unknown";
}

std::string_view to_string(EvidenceType value) noexcept
{
    switch (value)
    {
        case EvidenceType::ThresholdCrossing:
            return "threshold_crossing";
        case EvidenceType::CounterDelta:
            return "counter_delta";
        case EvidenceType::CoOccurrence:
            return "co_occurrence";
        case EvidenceType::Statistic:
            return "statistic";
        case EvidenceType::DataAvailability:
            return "data_availability";
    }
    return "unknown";
}

std::string_view to_string(FindingKind value) noexcept
{
    switch (value)
    {
        case FindingKind::FrameHitch:
            return "frame_hitch";
        case FindingKind::CpuSpike:
            return "cpu_spike";
        case FindingKind::GcAllocSpike:
            return "gc_alloc_spike";
        case FindingKind::GcCollectionHitch:
            return "gc_collection_hitch";
        case FindingKind::MemoryGrowth:
            return "memory_growth";
        case FindingKind::InsufficientData:
            return "insufficient_data";
    }
    return "unknown";
}

std::string_view to_string(FindingSeverity value) noexcept
{
    switch (value)
    {
        case FindingSeverity::Info:
            return "info";
        case FindingSeverity::Low:
            return "low";
        case FindingSeverity::Medium:
            return "medium";
        case FindingSeverity::High:
            return "high";
    }
    return "unknown";
}

std::string_view to_string(EvidenceLevel value) noexcept
{
    switch (value)
    {
        case EvidenceLevel::Measured:
            return "measured";
        case EvidenceLevel::DeterministicFinding:
            return "deterministic_finding";
        case EvidenceLevel::EvidenceBackedDiagnosis:
            return "evidence_backed_diagnosis";
        case EvidenceLevel::Hypothesis:
            return "hypothesis";
        case EvidenceLevel::InsufficientEvidence:
            return "insufficient_evidence";
    }
    return "unknown";
}

} // namespace gpia
