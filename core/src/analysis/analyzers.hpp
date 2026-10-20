#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "evidence_ledger.hpp"

#include "gpia/analysis/config.hpp"
#include "gpia/model/frame_record.hpp"

namespace gpia::analysis
{

// 一个被判定为卡顿的帧，GC 分析会用它做同帧关联
struct HitchEvent
{
    std::uint64_t frame_index = 0;
    double frame_time_ms = 0.0;
    double baseline_ms = 0.0;
    std::string evidence_id;
};

std::vector<HitchEvent> analyze_hitches(std::span<const FrameRecord> frames,
                                        const HitchConfig& config, double floor_ms,
                                        EvidenceLedger& ledger);

void analyze_cpu(std::span<const FrameRecord> frames, const CpuConfig& config,
                 EvidenceLedger& ledger);

void analyze_gc(std::span<const FrameRecord> frames, const GcAllocConfig& config,
                const std::vector<HitchEvent>& hitches, EvidenceLedger& ledger);

void analyze_memory(std::span<const FrameRecord> frames, const MemoryConfig& config,
                    EvidenceLedger& ledger);

// 把帧号相邻的事件归成一组，返回每组在输入中的起止下标
template <typename Event, typename FrameOf>
std::vector<std::pair<std::size_t, std::size_t>> group_consecutive(const std::vector<Event>& events,
                                                                   FrameOf frame_of)
{
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (!groups.empty() && frame_of(events[i]) == frame_of(events[groups.back().second]) + 1)
        {
            groups.back().second = i;
        }
        else
        {
            groups.emplace_back(i, i);
        }
    }
    return groups;
}

} // namespace gpia::analysis
