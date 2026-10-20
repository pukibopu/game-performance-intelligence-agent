#include "gpia/io/run_loader.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "gpia/io/file_io.hpp"
#include "gpia/io/manifest_reader.hpp"
#include "gpia/io/sha256.hpp"
#include "gpia/io/text.hpp"

namespace gpia
{

namespace
{

void warn(Diagnostics& diagnostics, std::string code, std::string message, std::string field = {})
{
    Diagnostic d{
        .severity = Severity::Warning, .code = std::move(code), .message = std::move(message)};
    if (!field.empty())
    {
        d.field = std::move(field);
    }
    diagnostics.add(std::move(d));
}

// 列声明与 CSV 表头不一致说明采集端和契约有偏差，但不妨碍分析
void check_columns(const SessionManifest& manifest, const FrameTable& table,
                   Diagnostics& diagnostics)
{
    for (const ColumnInfo& column : manifest.columns)
    {
        if (std::ranges::find(table.header, column.name) == table.header.end())
        {
            warn(diagnostics, "MANIFEST_COLUMN_NOT_IN_CSV",
                 "column is declared in manifest.json but missing from frames.csv", column.name);
        }
    }
    for (const std::string& name : table.header)
    {
        if (manifest.find_column(name) == nullptr)
        {
            warn(diagnostics, "CSV_COLUMN_NOT_IN_MANIFEST",
                 "column appears in frames.csv but is not declared in manifest.json", name);
        }
    }
}

} // namespace

Result<LoadedRun> load_run(const std::filesystem::path& run_dir, Diagnostics& diagnostics,
                           const LoadOptions& options)
{
    Result<SessionManifest> manifest = read_manifest(run_dir / "manifest.json", diagnostics);
    if (!manifest)
    {
        return manifest.error();
    }

    Result<std::string> bytes = read_file_bytes(run_dir / manifest->frames_file);
    if (!bytes)
    {
        return bytes.error();
    }

    // 摘要不符说明数据被截断或改动，结论将无法追溯，所以直接拒绝
    std::string actual_sha = sha256_hex(*bytes);
    if (actual_sha != manifest->frames_sha256)
    {
        diagnostics.add({.severity = Severity::Error,
                         .code = "FRAMES_HASH_MISMATCH",
                         .message = "frames.csv sha256 is " + actual_sha +
                                    " but manifest.json says " + manifest->frames_sha256,
                         .field = "frames_sha256"});
        return Error{.code = ErrorCode::InvalidInput,
                     .message = "frames.csv does not match manifest.json frames_sha256"};
    }

    Result<FrameTable> table = parse_frames_csv(*bytes, diagnostics, options.csv);
    if (!table)
    {
        return table.error();
    }

    if (table->data_rows != manifest->frame_count)
    {
        warn(diagnostics, "FRAME_COUNT_MISMATCH",
             "manifest.json frame_count is " + std::to_string(manifest->frame_count) +
                 " but frames.csv has " + std::to_string(table->data_rows) + " data rows",
             "frame_count");
    }
    if (manifest->status == RunStatus::Aborted)
    {
        warn(diagnostics, "RUN_ABORTED",
             "the run was aborted: " +
                 sanitize_untrusted(manifest->abort_reason.value_or("no reason given"), 200),
             "status");
    }
    check_columns(*manifest, *table, diagnostics);

    return LoadedRun{.manifest = std::move(manifest).value(),
                     .table = std::move(table).value(),
                     .frames_sha256 = std::move(actual_sha)};
}

} // namespace gpia
