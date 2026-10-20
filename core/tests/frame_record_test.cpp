#include <type_traits>

#include <gtest/gtest.h>

#include "gpia/model/frame_record.hpp"

using gpia::FrameRecord;

// ---------- compile-time properties ----------
// Aggregate: built with designated initializers, no constructor boilerplate.
static_assert(std::is_aggregate_v<FrameRecord>);
// Trivially copyable: std::vector<FrameRecord> can relocate elements with memcpy when it grows.
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
    EXPECT_FALSE(r.cpu_main_thread_ms.has_value()); // not named -> stays empty, NOT zero
}

TEST(FrameRecord, MissingIsDifferentFromZero)
{
    const FrameRecord missing{};
    FrameRecord zero{};
    zero.gc_alloc_bytes = 0;
    EXPECT_NE(missing, zero); // "no data" and "measured 0 bytes" must never compare equal
}

TEST(FrameRecord, CopiesAreIndependent)
{
    const FrameRecord a{.frame_index = 1, .frame_time_ms = 16.6};
    FrameRecord b = a;
    b.frame_time_ms = 33.3;
    EXPECT_EQ(a.frame_time_ms, 16.6); // same literal, no arithmetic: exact compare is fine
    EXPECT_NE(a, b);
}


TEST(FrameRecord, SizeIsDocumented)
{
    EXPECT_EQ(sizeof(FrameRecord), 256u);
}
