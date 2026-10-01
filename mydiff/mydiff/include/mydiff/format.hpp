#pragma once

#include <string>
#include <vector>

#include "mydiff/myers.hpp"
#include "mydiff/tokenize.hpp"

namespace mydiff {

// A hunk contains one or more changes with the requested surrounding context.
// Changes are merged when their context overlaps.
struct hunk {
    int start_a;
    int length_a;
    int start_b;
    int length_b;
    std::vector<edit> edits;
};

std::vector<hunk> make_hunks(const std::vector<edit>& script, int context_lines);

struct colour_palette {
    std::string reset;
    std::string deletion;
    std::string insertion;
    std::string hunk_header;
    std::string file_header;
};

colour_palette no_colours();
colour_palette ansi_colours();

struct file_header_info {
    std::string label;
    std::string timestamp;
};

std::string format_normal(const std::vector<edit>& script,
                          const token_list& tokens_a, const token_list& tokens_b,
                          const colour_palette& colours);

std::string format_unified(const std::vector<edit>& script,
                           const token_list& tokens_a, const token_list& tokens_b,
                           int context_lines,
                           const file_header_info& header_a, const file_header_info& header_b,
                           const colour_palette& colours);

std::string format_side_by_side(const std::vector<edit>& script,
                                const token_list& tokens_a, const token_list& tokens_b,
                                int total_width, bool suppress_common_lines,
                                const colour_palette& colours);

// Inline output marks deletions with [-old-] and insertions with {+new+}.
std::string format_inline(const std::vector<edit>& script,
                          const token_list& tokens_a, const token_list& tokens_b,
                          const colour_palette& colours);

struct diff_statistics {
    int insertions = 0;
    int deletions = 0;
    int hunks = 0;
};

diff_statistics compute_statistics(const std::vector<edit>& script, int context_lines);

std::string format_statistics(const diff_statistics& statistics,
                              const std::string& name_a, const std::string& name_b,
                              const colour_palette& colours);

}  // namespace mydiff
