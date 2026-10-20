#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace gpia
{
// 错误类别
enum class ErrorCode : std::uint8_t
{
    IoError,          // 输入不存在或无法读取，退出码 3
    InvalidInput,     // 输入违反 schema 或格式，退出码 4
    InsufficientData, // 输入可读但数据不足，退出码 5
    Internal,         // gpia-core 自身的 bug，退出码 10
};

struct Error
{
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};

template <typename T> class [[nodiscard]] Result
{
  public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}
    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & { return std::get<T>(storage_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(storage_); }
    // 右值
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(storage_)); }
    [[nodiscard]] const Error& error() const& { return std::get<Error>(storage_); }

    T& operator*() & noexcept
    {
        assert(has_value());
        return *std::get_if<T>(&storage_);
    }
    const T& operator*() const& noexcept
    {
        assert(has_value());
        return *std::get_if<T>(&storage_);
    }
    T* operator->() noexcept
    {
        assert(has_value());
        return std::get_if<T>(&storage_);
    }
    const T* operator->() const noexcept
    {
        assert(has_value());
        return std::get_if<T>(&storage_);
    }

  private:
    std::variant<T, Error> storage_;
};
} // namespace gpia
