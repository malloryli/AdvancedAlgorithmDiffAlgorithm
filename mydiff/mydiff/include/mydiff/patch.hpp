#pragma once

#include <string>
#include <vector>

namespace mydiff {

struct patch_line {
    char tag;
    std::string text;
    bool no_newline;
};

struct patch_hunk {
    int start_a;
    int length_a;
    int start_b;
    int length_b;
    std::vector<patch_line> lines;
};

struct unified_patch {
    std::string old_label;
    std::string new_label;
    std::vector<patch_hunk> hunks;
};

// Only the first file in a unified diff is parsed.
bool parse_unified_patch(const std::string& text, unified_patch& result, std::string& error_message);

struct apply_result {
    bool ok = false;
    std::string output;
    std::string error_message;
    int hunks_applied = 0;
    int hunks_with_offset = 0;
};

// A hunk is searched for near its stated position when the context has moved.
apply_result apply_patch(const std::string& original, const unified_patch& patch, bool reverse);

}  // namespace mydiff
