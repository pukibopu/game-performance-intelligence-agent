#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "gpia/analysis/analyze.hpp"
#include "gpia/analysis/config.hpp"
#include "gpia/diagnostics.hpp"
#include "gpia/io/file_io.hpp"
#include "gpia/io/run_loader.hpp"
#include "gpia/io/text.hpp"
#include "gpia/report/markdown.hpp"
#include "gpia/report/report_writer.hpp"
#include "gpia/version.hpp"

namespace
{

namespace fs = std::filesystem;

// 退出码是 CLI 对调用方的契约，工具层据此区分错误类型
enum ExitCode : int
{
    kOk = 0,
    kUsage = 2,
    kIoError = 3,
    kInvalidInput = 4,
    kInsufficientData = 5,
    kInternal = 10,
};

int exit_code_for(gpia::ErrorCode code) noexcept
{
    switch (code)
    {
        case gpia::ErrorCode::IoError:
            return kIoError;
        case gpia::ErrorCode::InvalidInput:
            return kInvalidInput;
        case gpia::ErrorCode::InsufficientData:
            return kInsufficientData;
        case gpia::ErrorCode::Internal:
            return kInternal;
    }
    return kInternal;
}

void print_diagnostics(const gpia::Diagnostics& diagnostics)
{
    for (const gpia::Diagnostic& d : diagnostics.items())
    {
        std::cerr << gpia::diagnostic_json_line(d) << '\n';
    }
}

void print_fatal(const gpia::Error& error)
{
    std::cerr << gpia::diagnostic_json_line(
                     {.severity = gpia::Severity::Error, .code = "FATAL", .message = error.message})
              << '\n';
}

// 输出路径为空时写到 stdout，否则按字节原样写入文件
bool write_output(const std::string& text, const std::optional<fs::path>& path)
{
    if (!path)
    {
        std::cout << text;
        return static_cast<bool>(std::cout);
    }
    std::ofstream out(*path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

int run_validate(const fs::path& run_dir)
{
    gpia::Diagnostics diagnostics;
    const auto run = gpia::load_run(run_dir, diagnostics);
    print_diagnostics(diagnostics);
    if (!run)
    {
        print_fatal(run.error());
        return exit_code_for(run.error().code);
    }
    const nlohmann::ordered_json summary{{"run_id", run->manifest.run_id},
                                         {"valid", true},
                                         {"data_rows", run->table.data_rows},
                                         {"valid_frames", run->table.frames.size()}};
    std::cout << summary.dump() << '\n';
    return kOk;
}

struct AnalyzeOptions
{
    fs::path run_dir;
    std::optional<fs::path> config_path;
    std::optional<fs::path> out_path;
    std::optional<fs::path> markdown_path;
    std::string format = "json";
};

int run_analyze(const AnalyzeOptions& options)
{
    gpia::Diagnostics diagnostics;
    gpia::AnalysisConfig config;
    if (options.config_path)
    {
        const auto text = gpia::read_file_bytes(*options.config_path);
        if (!text)
        {
            print_fatal(text.error());
            return exit_code_for(text.error().code);
        }
        auto parsed = gpia::parse_analysis_config(*text, diagnostics);
        if (!parsed)
        {
            print_diagnostics(diagnostics);
            print_fatal(parsed.error());
            return exit_code_for(parsed.error().code);
        }
        config = std::move(parsed).value();
    }

    const auto run = gpia::load_run(options.run_dir, diagnostics);
    if (!run)
    {
        print_diagnostics(diagnostics);
        print_fatal(run.error());
        return exit_code_for(run.error().code);
    }

    const gpia::Report report = gpia::analyze_run(*run, config, diagnostics);
    print_diagnostics(diagnostics);

    const std::string text = options.format == "md" ? gpia::render_markdown(report, diagnostics)
                                                    : gpia::write_report_json(report, diagnostics);
    if (!write_output(text, options.out_path))
    {
        print_fatal({.code = gpia::ErrorCode::IoError, .message = "cannot write the report"});
        return kIoError;
    }
    if (options.markdown_path &&
        !write_output(gpia::render_markdown(report, diagnostics), options.markdown_path))
    {
        print_fatal(
            {.code = gpia::ErrorCode::IoError, .message = "cannot write the Markdown summary"});
        return kIoError;
    }
    return report.outcome == gpia::Outcome::InsufficientData ? kInsufficientData : kOk;
}

int run(int argc, char** argv)
{
    CLI::App app{"GPIA performance analysis core", "gpia-core"};
    app.set_version_flag("--version", "gpia-core " + std::string(gpia::version()));
    app.require_subcommand(1);

    fs::path validate_dir;
    CLI::App* validate =
        app.add_subcommand("validate", "Check a run directory without analyzing it");
    validate->add_option("--run", validate_dir, "Run directory with manifest.json and frames.csv")
        ->required();

    AnalyzeOptions analyze_options;
    CLI::App* analyze = app.add_subcommand("analyze", "Analyze a run directory and write a report");
    analyze->add_option("--run", analyze_options.run_dir, "Run directory")->required();
    analyze->add_option("--config", analyze_options.config_path, "Analysis config JSON");
    analyze->add_option("--out", analyze_options.out_path,
                        "Write the report here instead of stdout");
    analyze->add_option("--md-out", analyze_options.markdown_path, "Also write a Markdown summary");
    analyze->add_option("--format", analyze_options.format, "Report format")
        ->check(CLI::IsMember({"json", "md"}));

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        // 帮助和版本信息也通过异常返回，它们的退出码为 0
        return app.exit(e) == 0 ? kOk : kUsage;
    }

    if (*validate)
    {
        return run_validate(validate_dir);
    }
    return run_analyze(analyze_options);
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    // 报告必须是 LF 换行的原始字节，关闭 Windows 控制台的换行转换
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << gpia::diagnostic_json_line(
                         {.severity = gpia::Severity::Error,
                          .code = "INTERNAL_ERROR",
                          .message = gpia::sanitize_untrusted(e.what(), 200)})
                  << '\n';
        return kInternal;
    }
}
