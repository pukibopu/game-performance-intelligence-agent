#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "gpia/result.hpp"

using gpia::Error;
using gpia::ErrorCode;
using gpia::Result;

// Result 可复制，并且在 T 可 noexcept 移动时同样可 noexcept 移动
static_assert(std::is_copy_constructible_v<Result<std::string>>);
static_assert(std::is_nothrow_move_constructible_v<Result<std::vector<int>>>);

namespace
{

Result<int> parse_positive(int x)
{
    if (x <= 0)
    {
        return Error{.code = ErrorCode::InvalidInput, .message = "must be positive"};
    }
    return x;
}

} // namespace

TEST(Result, HoldsValue)
{
    const Result<int> r = 42;
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r);
    EXPECT_EQ(r.value(), 42);
    EXPECT_EQ(*r, 42);
}

TEST(Result, HoldsError)
{
    const Result<int> r = Error{.code = ErrorCode::IoError, .message = "cannot open frames.csv"};
    EXPECT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_EQ(r.error().message, "cannot open frames.csv");
}

TEST(Result, CheckedAccessThrowsOnWrongAlternative)
{
    const Result<int> bad = Error{.code = ErrorCode::InvalidInput, .message = "bad"};
    EXPECT_THROW((void)bad.value(), std::bad_variant_access);

    const Result<int> ok = 1;
    EXPECT_THROW((void)ok.error(), std::bad_variant_access);
}

TEST(Result, LvalueValueCopiesRvalueValueMoves)
{
    Result<std::vector<int>> r = std::vector<int>(1000, 7);
    const int* buffer = r->data();

    const std::vector<int> copied = r.value(); // 左值调用会复制到新缓冲区
    EXPECT_NE(copied.data(), buffer);

    const std::vector<int> moved = std::move(r).value(); // 右值调用会直接移走原缓冲区
    EXPECT_EQ(moved.data(), buffer);
    EXPECT_EQ(moved.size(), 1000u);
}

TEST(Result, ArrowReachesMembers)
{
    const Result<std::string> r = std::string{"frames.csv"};
    EXPECT_EQ(r->size(), 10u);
}

TEST(Result, WorksAsFunctionReturnType)
{
    const auto ok = parse_positive(5);
    ASSERT_TRUE(ok);
    EXPECT_EQ(*ok, 5);

    const auto bad = parse_positive(-1);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, ErrorCode::InvalidInput);
}
