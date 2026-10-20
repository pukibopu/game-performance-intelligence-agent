#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gpia
{

// 增量计算 SHA-256，用于确认 frames.csv 与 manifest 配套
class Sha256
{
  public:
    Sha256() noexcept;

    void update(std::span<const std::byte> data) noexcept;
    void update(std::string_view text) noexcept;

    // 补齐并输出 32 字节摘要，调用后对象不能再继续使用
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

  private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
};

// 计算整段数据的 SHA-256 并输出 64 位小写十六进制
[[nodiscard]] std::string sha256_hex(std::string_view data);

} // namespace gpia
