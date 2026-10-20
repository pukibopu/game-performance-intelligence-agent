#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gpia
{

enum class Severity : std::uint8_t
{
    Info,    // 值得记录但不影响分析
    Warning, // 数据可用但质量存疑
    Error,   // 对应的数据已被丢弃
};

// 一条数据质量问题，会原样写入报告
struct Diagnostic
{
    Severity severity = Severity::Info;
    std::string code; // 形如 CSV_INVALID_NUMBER 的稳定标识
    std::string message;
    std::optional<std::uint64_t> line; // 输入文件中从 1 开始的行号
    std::optional<std::string> field;  // 相关的列名或 JSON 字段名
};

// 诊断码必须以大写字母开头，其余只能是大写字母、数字或下划线
[[nodiscard]] bool is_valid_diagnostic_code(std::string_view code) noexcept;

// 收集诊断并按严重度计数，超过上限后只计数不保存
class Diagnostics
{
  public:
    // 限制保存条数，防止不可信输入撑爆内存
    explicit Diagnostics(std::size_t max_stored = 100);

    void add(Diagnostic diagnostic);

    [[nodiscard]] std::span<const Diagnostic> items() const noexcept { return items_; }
    [[nodiscard]] std::size_t count(Severity severity) const noexcept;
    [[nodiscard]] bool has_errors() const noexcept { return count(Severity::Error) > 0; }
    [[nodiscard]] std::size_t suppressed_count() const noexcept { return suppressed_; }

  private:
    std::vector<Diagnostic> items_;
    std::size_t max_stored_;
    std::array<std::size_t, 3> counts_{}; // 按 Severity 的取值索引
    std::size_t suppressed_ = 0;
};

} // namespace gpia
