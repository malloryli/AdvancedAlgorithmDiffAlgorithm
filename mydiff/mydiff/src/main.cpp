#include <sys/stat.h>
#include <fcntl.h>
#include <io.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "mydiff/format.hpp"
#include "mydiff/myers.hpp"
#include "mydiff/patch.hpp"
#include "mydiff/slide.hpp"
#include "mydiff/tokenize.hpp"

using namespace mydiff;

namespace {

const char* version_string = "mydiff 1.0";

void print_usage(FILE* stream) {
    std::fprintf(stream,
R"(Usage: mydiff [OPTIONS] FILE1 FILE2
       mydiff patch [-R] [-o OUT] FILE PATCHFILE
Compare files line by line using Myers' O(ND) difference algorithm.
Use "-" for FILE1 or FILE2 to read standard input.

Output format (default: normal):
  -u, --unified[=N]        unified format with N lines of context (default 3)
  -U N                     same as --unified=N
  -y, --side-by-side       two-column output
  -W, --width=N            total width of side-by-side output (default 130)
      --suppress-common-lines
                           in side-by-side mode, only print differing lines
      --word-diff          compare words and show changes inline as [-old-]{+new+}
      --char-diff          compare characters and show changes inline
      --stat               print a summary (hunks, insertions, deletions) instead
  -q, --brief              report only whether the files differ
  -s, --report-identical-files
                           say so when two files are the same
  -L, --label LABEL        use LABEL in the unified header (give twice for both files)

Comparison:
  -i, --ignore-case        ignore case differences
  -w, --ignore-all-space   ignore all white space
  -b, --ignore-space-change
                           ignore changes in the amount of white space
      --algorithm=linear|greedy
                           choose the O(N+M)-space middle-snake search (default)
                           or the O(D^2)-space forward search
      --no-prune           do not remove lines unique to one file before searching
                           (slower; same result)
      --no-slide           do not slide inserted/deleted blocks onto blank-line
                           boundaries (show the raw Myers output)

Presentation:
      --color[=WHEN]       colorize output; WHEN is always, never or auto (default auto)

Patch subcommand:
  mydiff patch FILE PATCHFILE   print FILE with PATCHFILE applied (use - for stdin)
  -R, --reverse                 apply the patch backwards
  -o, --output OUT              write the result to OUT instead of standard output

  -h, --help               show this help
      --version            show version

Exit status is 0 if the inputs are the same, 1 if different, 2 on error.
)");
}

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "mydiff: %s\n", message.c_str());
    std::exit(2);
}

bool read_input_file(const std::string& path, std::string& contents) {
    if (path == "-") {
        contents.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        return true;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    contents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

std::string file_timestamp(const std::string& path) {
    if (path == "-") return "";

    struct stat file_status;
    if (stat(path.c_str(), &file_status) != 0) return "";

    std::tm local_time;
    localtime_s(&local_time, &file_status.st_mtime);

    char date_part[64];
    char zone_part[8];
    char nanoseconds[16];
    std::strftime(date_part, sizeof date_part, "%Y-%m-%d %H:%M:%S", &local_time);
    std::strftime(zone_part, sizeof zone_part, "%z", &local_time);
    std::snprintf(nanoseconds, sizeof nanoseconds, ".%09d", 0);

    return std::string(date_part) + nanoseconds + " " + zone_part;
}

enum class output_format { normal, unified, side_by_side, inline_tokens, statistics, brief };

struct command_options {
    output_format format = output_format::normal;
    int context_lines = 3;
    int total_width = 130;
    bool suppress_common_lines = false;
    bool report_identical = false;
    std::string colour_mode = "auto";
    std::vector<std::string> labels;
    tokenize_options tokenizer;
    algorithm_kind algorithm = algorithm_kind::linear_space;
    bool prune_unique_symbols = true;
    bool slide_blocks = true;
    std::vector<std::string> files;
};

int parse_non_negative(const std::string& text, const char* what) {
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < 0) fail(std::string("invalid ") + what + ": " + text);
    return (int)value;
}

bool has_prefix(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

command_options parse_arguments(int argc, char** argv) {
    command_options options;

    for (int index = 1; index < argc; ++index) {
        std::string argument = argv[index];

        auto next_value = [&](const char* option_name) -> std::string {
            if (index + 1 >= argc) fail(std::string("option ") + option_name + " requires an argument");
            return argv[++index];
        };

        if (argument == "--") {
            for (++index; index < argc; ++index) options.files.push_back(argv[index]);
            break;
        }

        if (argument == "-h" || argument == "--help") { print_usage(stdout); std::exit(0); }
        if (argument == "--version") { std::printf("%s\n", version_string); std::exit(0); }

        if (argument == "-u" || argument == "--unified") {
            options.format = output_format::unified;
        } else if (has_prefix(argument, "--unified=")) {
            options.format = output_format::unified;
            options.context_lines = parse_non_negative(argument.substr(10), "context");
        } else if (argument == "-U") {
            options.format = output_format::unified;
            options.context_lines = parse_non_negative(next_value("-U"), "context");
        } else if (argument.size() > 2 && argument[0] == '-' && argument[1] == 'U') {
            options.format = output_format::unified;
            options.context_lines = parse_non_negative(argument.substr(2), "context");
        } else if (argument == "--normal") {
            options.format = output_format::normal;
        } else if (argument == "-y" || argument == "--side-by-side") {
            options.format = output_format::side_by_side;
        } else if (argument == "-W") {
            options.total_width = parse_non_negative(next_value("-W"), "width");
        } else if (has_prefix(argument, "--width=")) {
            options.total_width = parse_non_negative(argument.substr(8), "width");
        } else if (argument == "--suppress-common-lines") {
            options.suppress_common_lines = true;
        } else if (argument == "--word-diff") {
            options.format = output_format::inline_tokens;
            options.tokenizer.unit = granularity::words;
        } else if (argument == "--char-diff") {
            options.format = output_format::inline_tokens;
            options.tokenizer.unit = granularity::characters;
        } else if (argument == "--stat") {
            options.format = output_format::statistics;
        } else if (argument == "-q" || argument == "--brief") {
            options.format = output_format::brief;
        } else if (argument == "-s" || argument == "--report-identical-files") {
            options.report_identical = true;
        } else if (argument == "-L" || argument == "--label") {
            options.labels.push_back(next_value("-L"));
        } else if (has_prefix(argument, "--label=")) {
            options.labels.push_back(argument.substr(8));
        } else if (argument == "-i" || argument == "--ignore-case") {
            options.tokenizer.ignore_case = true;
        } else if (argument == "-w" || argument == "--ignore-all-space") {
            options.tokenizer.ignore_all_space = true;
        } else if (argument == "-b" || argument == "--ignore-space-change") {
            options.tokenizer.ignore_space_change = true;
        } else if (argument == "--color") {
            options.colour_mode = "always";
        } else if (has_prefix(argument, "--color=")) {
            options.colour_mode = argument.substr(8);
        } else if (argument == "--no-color") {
            options.colour_mode = "never";
        } else if (has_prefix(argument, "--algorithm=")) {
            std::string value = argument.substr(12);
            if (value == "linear") options.algorithm = algorithm_kind::linear_space;
            else if (value == "greedy") options.algorithm = algorithm_kind::greedy;
            else fail("unknown algorithm: " + value + " (expected linear or greedy)");
        } else if (argument == "--no-prune") {
            options.prune_unique_symbols = false;
        } else if (argument == "--no-slide") {
            options.slide_blocks = false;
        } else if (argument.size() > 2 && argument[0] == '-' && argument[1] != '-') {
            for (size_t position = 1; position < argument.size(); ++position) {
                switch (argument[position]) {
                    case 'i': options.tokenizer.ignore_case = true; break;
                    case 'w': options.tokenizer.ignore_all_space = true; break;
                    case 'b': options.tokenizer.ignore_space_change = true; break;
                    case 'u': options.format = output_format::unified; break;
                    case 'y': options.format = output_format::side_by_side; break;
                    case 'q': options.format = output_format::brief; break;
                    case 's': options.report_identical = true; break;
                    default: fail("unknown option: " + argument + " (try --help)");
                }
            }
        } else if (argument.size() > 1 && argument[0] == '-') {
            fail("unknown option: " + argument + " (try --help)");
        } else {
            options.files.push_back(argument);
        }
    }

    if (options.colour_mode != "always" && options.colour_mode != "never" && options.colour_mode != "auto") {
        fail("invalid --color value: " + options.colour_mode);
    }

    return options;
}

int run_diff(int argc, char** argv) {
    command_options options = parse_arguments(argc, argv);

    if (options.files.size() != 2) {
        std::fprintf(stderr, "mydiff: expected exactly two files\n");
        print_usage(stderr);
        return 2;
    }
    if (options.files[0] == "-" && options.files[1] == "-") {
        fail("only one input can be standard input");
    }

    std::string contents_a;
    std::string contents_b;
    if (!read_input_file(options.files[0], contents_a)) fail(options.files[0] + ": " + std::strerror(errno));
    if (!read_input_file(options.files[1], contents_b)) fail(options.files[1] + ": " + std::strerror(errno));

    bool use_colour = options.colour_mode == "always" ||
                      (options.colour_mode == "auto" && isatty(STDOUT_FILENO));
    colour_palette colours = use_colour ? ansi_colours() : no_colours();

    token_list tokens_a;
    token_list tokens_b;
    tokenize_pair(contents_a, contents_b, options.tokenizer, tokens_a, tokens_b);

    std::vector<edit> script = compute_diff(tokens_a.symbol_ids, tokens_b.symbol_ids,
                                            options.algorithm, options.prune_unique_symbols);

    if (options.slide_blocks && options.tokenizer.unit == granularity::lines) {
        slide_change_blocks(script, tokens_a, tokens_b);
    }

    bool identical = edit_distance(script) == 0;

    if (identical) {
        if (options.report_identical || options.format == output_format::statistics) {
            std::printf("Files %s and %s are identical\n", options.files[0].c_str(), options.files[1].c_str());
        } else if (options.format == output_format::inline_tokens) {
            std::fwrite(contents_a.data(), 1, contents_a.size(), stdout);
        }
        return 0;
    }

    std::string output;

    switch (options.format) {
        case output_format::brief:
            std::printf("Files %s and %s differ\n", options.files[0].c_str(), options.files[1].c_str());
            return 1;

        case output_format::normal:
            output = format_normal(script, tokens_a, tokens_b, colours);
            break;

        case output_format::unified: {
            file_header_info header_a;
            file_header_info header_b;
            header_a.label = options.labels.size() > 0 ? options.labels[0] : options.files[0];
            header_b.label = options.labels.size() > 1 ? options.labels[1] : options.files[1];
            if (options.labels.size() == 0) header_a.timestamp = file_timestamp(options.files[0]);
            if (options.labels.size() < 2) header_b.timestamp = file_timestamp(options.files[1]);
            output = format_unified(script, tokens_a, tokens_b, options.context_lines, header_a, header_b, colours);
            break;
        }

        case output_format::side_by_side:
            output = format_side_by_side(script, tokens_a, tokens_b, options.total_width,
                                         options.suppress_common_lines, colours);
            break;

        case output_format::inline_tokens:
            output = format_inline(script, tokens_a, tokens_b, colours);
            break;

        case output_format::statistics:
            output = format_statistics(compute_statistics(script, options.context_lines),
                                       options.files[0], options.files[1], colours);
            break;
    }

    std::fwrite(output.data(), 1, output.size(), stdout);
    return 1;
}

int run_patch(int argc, char** argv) {
    bool reverse = false;
    std::string output_path;
    std::vector<std::string> files;

    for (int index = 2; index < argc; ++index) {
        std::string argument = argv[index];

        if (argument == "-R" || argument == "--reverse") {
            reverse = true;
        } else if (argument == "-o" || argument == "--output") {
            if (index + 1 >= argc) fail("-o requires an argument");
            output_path = argv[++index];
        } else if (argument == "-h" || argument == "--help") {
            print_usage(stdout);
            return 0;
        } else if (argument.size() > 1 && argument[0] == '-') {
            fail("unknown patch option: " + argument);
        } else {
            files.push_back(argument);
        }
    }

    if (files.size() != 2) fail("usage: mydiff patch [-R] [-o OUT] FILE PATCHFILE");

    std::string original;
    std::string patch_text;
    if (!read_input_file(files[0], original)) fail(files[0] + ": " + std::strerror(errno));
    if (!read_input_file(files[1], patch_text)) fail(files[1] + ": " + std::strerror(errno));

    unified_patch patch;
    std::string error_message;
    if (!parse_unified_patch(patch_text, patch, error_message)) fail("cannot parse patch: " + error_message);

    apply_result result = apply_patch(original, patch, reverse);

    if (!result.ok) {
        std::fprintf(stderr, "mydiff: %s\n", result.error_message.c_str());
        return 1;
    }

    if (result.hunks_with_offset > 0) {
        std::fprintf(stderr, "mydiff: %d of %d hunks applied with an offset\n",
                     result.hunks_with_offset, result.hunks_applied);
    }

    if (output_path.empty()) {
        std::fwrite(result.output.data(), 1, result.output.size(), stdout);
    } else {
        std::ofstream stream(output_path, std::ios::binary);
        if (!stream) fail(output_path + ": " + std::strerror(errno));
        stream.write(result.output.data(), (std::streamsize)result.output.size());
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    if (argc >= 2 && std::strcmp(argv[1], "patch") == 0) {
        return run_patch(argc, argv);
    }
    return run_diff(argc, argv);
}
