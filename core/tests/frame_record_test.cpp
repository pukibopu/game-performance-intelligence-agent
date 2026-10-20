#include <type_traits>

#include <gtest/gtest.h>

#include "gpia/model/frame_record.hpp"

using gpia::FrameRecord;

// 聚合类型才能用指定初始化器构造
static_assert(std::is_aggregate_v<FrameRecord>);
// 可平凡复制意味着 vector 扩容时可以按字节搬移元素
static_assert(std::is_trivially_copyable_v<FrameRecord>);

TEST(FrameRecord, DefaultConstructedHasNoOptionalValues)
{
    const FrameRecord r{};
    EXPECT_EQ(r.frame_index, 0u);
    EXPECT_EQ(r.frame_time_ms, 0.0);
    EXPECT_FALSE(r.cpu_main_thread_ms.has_value());
    EXPECT_FALSE(r.gc_alloc_bytes.has_value());
    EXPECT_FALSE(r.draw_calls.has_value());
    EXPECT_EQ(r.scenario_marker, 0u);
}

TEST(FrameRecord, DesignatedInitializersSetOnlyNamedFields)
{
    const FrameRecord r{
        .frame_index = 2,
        .timestamp_ns = 58'013'000,
        .frame_time_ms = 24.646,
        .gc_alloc_bytes = 8'388'608,
        .scenario_marker = 1,
    };
    EXPECT_EQ(r.frame_index, 2u);
    ASSERT_TRUE(r.gc_alloc_bytes.has_value());
    EXPECT_EQ(*r.gc_alloc_bytes, 8'388'608);
    EXPECT_FALSE(r.cpu_main_thread_ms.has_value()); // 未指定的可选字段保持为空而不是 0
}

TEST(FrameRecord, MissingIsDifferentFromZero)
{
    const FrameRecord missing{};
    FrameRecord zero{};
    zero.gc_alloc_bytes = 0;
    EXPECT_NE(missing, zero); // 没有数据与测得 0 字节必须不相等
}

TEST(FrameRecord, CopiesAreIndependent)
{
    const FrameRecord a{.frame_index = 1, .frame_time_ms = 16.6};
    FrameRecord b = a;
    b.frame_time_ms = 33.3;
    EXPECT_EQ(a.frame_time_ms, 16.6); // 同一字面量且没有运算，可以精确比较
    EXPECT_NE(a, b);
}

// 每帧 AoS 开销，改列时必须有意识地更新这个数字
TEST(FrameRecord, SizeIsDocumented)
{
    EXPECT_EQ(sizeof(FrameRecord), 296u); // v1.1 新增三个可选列，从 256 增加到 296
}
