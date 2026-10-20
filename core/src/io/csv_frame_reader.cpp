#include "gpia/io/csv_frame_reader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "gpia/io/file_io.hpp"

namespace gpia
{

namespace
{

using FieldPtr =
    std::variant<std::uint64_t FrameRecord::*, std::int64_t FrameRecord::*, double FrameRecord::*,
                 std::uint32_t FrameRecord::*, std::optional<double> FrameRecord::*,
                 std::optional<std::int64_t> FrameRecord::*,
                 std::optional<std::int32_t> FrameRecord::*>;

struct ColumnSpec
{
    std::string_view name;
    FieldPtr field;
    bool required = false;
    bool positive = false; // 为真时要求严格大于 0，否则只要求非负
};

// 列名到 FrameRecord 字段的唯一映射表
constexpr std::array<ColumnSpec, 20> kColumns{{
    {"frame_index", &FrameRecord::frame_index, true, false},
    {"timestamp_ns", &FrameRecord::timestamp_ns, true, false},
    {"frame_time_ms", &FrameRecord::frame_time_ms, true, true},
    {"cpu_main_thread_ms", &FrameRecord::cpu_main_thread_ms},
    {"cpu_render_thread_ms", &FrameRecord::cpu_render_thread_ms},
    {"cpu_present_wait_ms", &FrameRecord::cpu_present_wait_ms},
    {"gpu_frame_time_ms", &FrameRecord::gpu_frame_time_ms},
    {"gc_alloc_bytes", &FrameRecord::gc_alloc_bytes},
    {"gc_used_bytes", &FrameRecord::gc_used_bytes},
    {"gc_reserved_bytes", &FrameRecord::gc_reserved_bytes},
    {"gc_collection_count", &FrameRecord::gc_collection_count},
    {"total_used_bytes", &FrameRecord::total_used_bytes},
    {"total_reserved_bytes", &FrameRecord::total_reserved_bytes},
    {"system_used_bytes", &FrameRecord::system_used_bytes},
    {"draw_calls", &FrameRecord::draw_calls},
    {"setpass_calls", &FrameRecord::setpass_calls},
    {"batches", &FrameRecord::batches},
    {"triangles", &FrameRecord::triangles},
    {"vertices", &FrameRecord::vertices},
    {"scenario_marker", &FrameRecord::scenario_marker, true, false},
}};

constexpr auto kColumnNames = [] {
    std::array<std::string_view, kColumns.size()> names{};
    for (std::size_t i = 0; i < kColumns.size(); ++i)
    {
        names[i] = kColumns[i].name;
    }
    return names;
}();

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

template <typename T> struct Unwrap
{
    using type = T;
};

template <typename T> struct Unwrap<std::optional<T>>
{
    using type = T;
};

// 整段文本必须恰好是一个数，浮点数还必须是有限值
template <typename T> std::optional<T> parse_number(std::string_view text) noexcept
{
    T value{};
    const char* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    if constexpr (std::is_floating_point_v<T>)
    {
        if (!std::isfinite(value))
        {
            return std::nullopt;
        }
    }
    return value;
}

template <typename T> bool in_range(T value, bool positive) noexcept
{
    if constexpr (std::is_signed_v<T>)
    {
        if (value < 0)
        {
            return false;
        }
    }
    return !positive || value > 0;
}

enum class CellStatus : std::uint8_t
{
    Ok,
    Empty,
    Invalid,
    OutOfRange,
};

CellStatus assign_cell(FrameRecord& record, const ColumnSpec& spec, std::string_view cell)
{
    if (cell.empty())
    {
        return CellStatus::Empty;
    }
    return std::visit(
        [&](auto member) {
            using Value = typename Unwrap<std::remove_cvref_t<decltype(record.*member)>>::type;
            const std::optional<Value> parsed = parse_number<Value>(cell);
            if (!parsed)
            {
                return CellStatus::Invalid;
            }
            if (!in_range(*parsed, spec.positive))
            {
                return CellStatus::OutOfRange;
            }
            record.*member = *parsed;
            return CellStatus::Ok;
        },
        spec.field);
}

// 结果指向原文本，复用 out 的容量以免每行分配
void split_fields(std::string_view line, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t start = 0;
    while (true)
    {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos)
        {
            out.push_back(line.substr(start));
            return;
        }
        out.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
}

// 不可信的单元格内容截断并替换不可打印字节后再写进消息
std::string quoted(std::string_view cell)
{
    constexpr std::size_t kMaxShown = 32;
    std::string text = "'";
    for (const char c : cell.substr(0, kMaxShown))
    {
        text.push_back(c >= 0x20 && c <= 0x7E ? c : '?');
    }
    if (cell.size() > kMaxShown)
    {
        text.append("...");
    }
    text.push_back('\'');
    return text;
}

class LineCursor
{
  public:
    explicit LineCursor(std::string_view text) : text_(text) {}

    // 返回下一行并去掉行尾的回车，文本结束时返回空
    std::optional<std::string_view> next() noexcept
    {
        if (pos_ >= text_.size())
        {
            return std::nullopt;
        }
        const std::size_t newline = text_.find('\n', pos_);
        const std::size_t end = newline == std::string_view::npos ? text_.size() : newline;
        std::string_view line = text_.substr(pos_, end - pos_);
        pos_ = newline == std::string_view::npos ? text_.size() : newline + 1;
        ++line_number_;
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        return line;
    }

    [[nodiscard]] std::uint64_t line_number() const noexcept { return line_number_; }

  private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::uint64_t line_number_ = 0;
};

// 记录诊断并统计本次解析产生的错误数
class Reporter
{
  public:
    explicit Reporter(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

    void add(Severity severity, std::string code, std::string message, std::uint64_t line,
             std::optional<std::string_view> field = std::nullopt)
    {
        if (severity == Severity::Error)
        {
            ++errors_;
        }
        Diagnostic d{.severity = severity, .code = std::move(code), .message = std::move(message)};
        d.line = line;
        if (field)
        {
            d.field = std::string(*field);
        }
        diagnostics_.add(std::move(d));
    }

    [[nodiscard]] std::size_t errors() const noexcept { return errors_; }

  private:
    Diagnostics& diagnostics_;
    std::size_t errors_ = 0;
};

Error fatal(Reporter& reporter, std::string code, std::string message, std::uint64_t line,
            std::optional<std::string_view> field = std::nullopt)
{
    reporter.add(Severity::Error, std::move(code), message, line, field);
    return Error{.code = ErrorCode::InvalidInput, .message = std::move(message)};
}

void report_cell(Reporter& reporter, CellStatus status, const ColumnSpec& spec,
                 std::string_view cell, std::uint64_t line)
{
    switch (status)
    {
        case CellStatus::Ok:
            return;
        case CellStatus::Empty:
            reporter.add(Severity::Error, "CSV_MISSING_REQUIRED", "required value is empty", line,
                         spec.name);
            return;
        case CellStatus::Invalid:
            reporter.add(Severity::Error, "CSV_INVALID_NUMBER",
                         "not a finite number: " + quoted(cell), line, spec.name);
            return;
        case CellStatus::OutOfRange:
            reporter.add(
                Severity::Error, "CSV_OUT_OF_RANGE",
                std::string(spec.positive ? "must be positive: " : "must not be negative: ") +
                    quoted(cell),
                line, spec.name);
            return;
    }
}

} // namespace

std::span<const std::string_view> known_frame_columns() noexcept
{
    return kColumnNames;
}

Result<FrameTable> parse_frames_csv(std::string_view text, Diagnostics& diagnostics,
                                    const CsvReadOptions& options)
{
    Reporter reporter{diagnostics};
    if (text.starts_with(kUtf8Bom))
    {
        text.remove_prefix(kUtf8Bom.size());
        reporter.add(Severity::Info, "CSV_UTF8_BOM", "ignored a UTF-8 byte order mark", 1);
    }
    if (text.empty())
    {
        return fatal(reporter, "CSV_EMPTY_FILE", "frames.csv is empty", 1);
    }

    LineCursor lines{text};
    const std::string_view header_line = lines.next().value_or(std::string_view{});
    std::vector<std::string_view> fields;
    split_fields(header_line, fields);

    // 表头决定每个位置对应哪一列，未知列保持空指针
    FrameTable table;
    std::vector<const ColumnSpec*> specs(fields.size(), nullptr);
    std::size_t frame_index_pos = 0;
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const std::string_view name = fields[i];
        if (std::find(fields.begin(), fields.begin() + static_cast<std::ptrdiff_t>(i), name) !=
            fields.begin() + static_cast<std::ptrdiff_t>(i))
        {
            return fatal(reporter, "CSV_DUPLICATE_COLUMN", "duplicate column " + quoted(name), 1,
                         name);
        }
        table.header.emplace_back(name);
        const auto spec = std::ranges::find(kColumns, name, &ColumnSpec::name);
        if (spec == kColumns.end())
        {
            reporter.add(Severity::Warning, "CSV_UNKNOWN_COLUMN",
                         "ignored unknown column " + quoted(name), 1, name);
            continue;
        }
        specs[i] = &*spec;
        if (spec->name == "frame_index")
        {
            frame_index_pos = i;
        }
    }
    for (const ColumnSpec& spec : kColumns)
    {
        const bool present = std::ranges::any_of(
            table.header, [&](const std::string& name) { return name == spec.name; });
        if (spec.required && !present)
        {
            return fatal(reporter, "CSV_MISSING_COLUMN",
                         "required column " + quoted(spec.name) + " is missing", 1, spec.name);
        }
    }

    table.frames.reserve(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')));
    std::optional<std::uint64_t> last_index;
    std::optional<std::int64_t> last_timestamp;

    while (const std::optional<std::string_view> line = lines.next())
    {
        const std::uint64_t line_no = lines.line_number();
        if (line->empty())
        {
            reporter.add(Severity::Warning, "CSV_EMPTY_LINE", "skipped an empty line", line_no);
            continue;
        }
        ++table.data_rows;
        split_fields(*line, fields);
        const bool shape_ok = fields.size() == specs.size();
        if (!shape_ok)
        {
            reporter.add(Severity::Error, "CSV_COLUMN_COUNT_MISMATCH",
                         "expected " + std::to_string(specs.size()) + " fields, got " +
                             std::to_string(fields.size()),
                         line_no);
        }

        // 被丢弃的行也要参与跳号判断，否则丢一行就会误报一次丢帧
        FrameRecord record{};
        bool keep = shape_ok;
        if (frame_index_pos < fields.size())
        {
            const ColumnSpec& spec = *specs[frame_index_pos];
            const std::string_view cell = fields[frame_index_pos];
            const CellStatus status = assign_cell(record, spec, cell);
            if (status != CellStatus::Ok)
            {
                if (shape_ok)
                {
                    report_cell(reporter, status, spec, cell, line_no);
                }
                keep = false;
            }
            else if (last_index && record.frame_index <= *last_index)
            {
                if (shape_ok)
                {
                    reporter.add(Severity::Error, "FRAME_INDEX_NOT_INCREASING",
                                 "frame_index " + std::to_string(record.frame_index) +
                                     " does not follow " + std::to_string(*last_index),
                                 line_no, spec.name);
                }
                keep = false;
            }
            else
            {
                if (shape_ok && last_index && record.frame_index > *last_index + 1)
                {
                    reporter.add(Severity::Warning, "FRAME_INDEX_GAP",
                                 std::to_string(record.frame_index - *last_index - 1) +
                                     " frames missing before frame_index " +
                                     std::to_string(record.frame_index),
                                 line_no, spec.name);
                }
                last_index = record.frame_index;
            }
        }

        if (shape_ok)
        {
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                if (i == frame_index_pos || specs[i] == nullptr)
                {
                    continue;
                }
                const ColumnSpec& spec = *specs[i];
                const CellStatus status = assign_cell(record, spec, fields[i]);
                if (status == CellStatus::Ok || (status == CellStatus::Empty && !spec.required))
                {
                    continue;
                }
                report_cell(reporter, status, spec, fields[i], line_no);
                if (spec.required)
                {
                    keep = false;
                }
            }
        }

        if (keep && last_timestamp && record.timestamp_ns <= *last_timestamp)
        {
            reporter.add(Severity::Error, "TIMESTAMP_NOT_INCREASING",
                         "timestamp_ns " + std::to_string(record.timestamp_ns) +
                             " does not follow " + std::to_string(*last_timestamp),
                         line_no, "timestamp_ns");
            keep = false;
        }
        if (keep)
        {
            last_timestamp = record.timestamp_ns;
            table.frames.push_back(record);
        }
        if (reporter.errors() > options.max_errors)
        {
            return fatal(reporter, "CSV_TOO_MANY_ERRORS",
                         "more than " + std::to_string(options.max_errors) + " errors", line_no);
        }
    }
    return table;
}

Result<FrameTable> read_frames_csv(const std::filesystem::path& path, Diagnostics& diagnostics,
                                   const CsvReadOptions& options)
{
    Result<std::string> bytes = read_file_bytes(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return parse_frames_csv(*bytes, diagnostics, options);
}

} // namespace gpia
