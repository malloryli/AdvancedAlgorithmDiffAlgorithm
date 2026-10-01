#include "mydiff/format.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace mydiff {

std::vector<hunk> make_hunks(const std::vector<edit>& script, int context_lines) {
    std::vector<hunk> hunks;
    const int script_length = (int)script.size();
    int index = 0;

    while (index < script_length) {
        if (script[index].operation == edit_operation::equal) {
            ++index;
            continue;
        }

        // Changes belong to one hunk while the equal region between them fits
        // within the context of both changes.
        int first_change = index;
        int last_change = index;
        int scan = index + 1;

        while (scan < script_length) {
            if (script[scan].operation != edit_operation::equal) {
                last_change = scan;
                ++scan;
                continue;
            }

            int next_change = scan;
            while (next_change < script_length && script[next_change].operation == edit_operation::equal) {
                ++next_change;
            }

            if (next_change < script_length && next_change - scan <= 2 * context_lines) {
                last_change = next_change;
                scan = next_change + 1;
            } else {
                break;
            }
        }

        int region_start = std::max(0, first_change - context_lines);
        int region_end = std::min(script_length, last_change + context_lines + 1);

        hunk current;
        current.start_a = script[region_start].index_a;
        current.start_b = script[region_start].index_b;
        current.length_a = 0;
        current.length_b = 0;
        current.edits.assign(script.begin() + region_start, script.begin() + region_end);

        for (const edit& current_edit : current.edits) {
            if (current_edit.operation != edit_operation::insertion) ++current.length_a;
            if (current_edit.operation != edit_operation::deletion) ++current.length_b;
        }

        hunks.push_back(std::move(current));
        index = region_end;
    }

    return hunks;
}

colour_palette no_colours() {
    return colour_palette{};
}

colour_palette ansi_colours() {
    colour_palette colours;
    colours.reset = "\x1b[0m";
    colours.deletion = "\x1b[31m";
    colours.insertion = "\x1b[32m";
    colours.hunk_header = "\x1b[36m";
    colours.file_header = "\x1b[1m";
    return colours;
}

namespace {

// A maximal sequence of unequal edits forms one change block.
struct change_block {
    int first_a;
    int count_a;
    int first_b;
    int count_b;
    int position_a;
    int position_b;
};

struct script_segment {
    bool is_change;
    edit equal_edit;
    change_block block;
};

std::vector<script_segment> split_into_segments(const std::vector<edit>& script) {
    std::vector<script_segment> segments;
    const int script_length = (int)script.size();
    int index = 0;

    while (index < script_length) {
        if (script[index].operation == edit_operation::equal) {
            script_segment segment{};
            segment.is_change = false;
            segment.equal_edit = script[index];
            segments.push_back(segment);
            ++index;
            continue;
        }

        change_block block{};
        block.position_a = script[index].index_a;
        block.position_b = script[index].index_b;
        bool seen_deletion = false;
        bool seen_insertion = false;

        while (index < script_length && script[index].operation != edit_operation::equal) {
            const edit& current = script[index];

            if (current.operation == edit_operation::deletion) {
                if (!seen_deletion) {
                    block.first_a = current.index_a;
                    seen_deletion = true;
                }
                ++block.count_a;
            } else {
                if (!seen_insertion) {
                    block.first_b = current.index_b;
                    seen_insertion = true;
                }
                ++block.count_b;
            }

            ++index;
        }

        script_segment segment{};
        segment.is_change = true;
        segment.block = block;
        segments.push_back(segment);
    }

    return segments;
}

std::string normal_range(int first, int count) {
    if (count <= 1) {
        return std::to_string(first + 1);
    }
    return std::to_string(first + 1) + "," + std::to_string(first + count);
}

const char* no_newline_marker = "\\ No newline at end of file\n";

bool needs_no_newline_marker(const token_list& tokens, int index) {
    return !tokens.trailing_newline && index == (int)tokens.text.size() - 1;
}

std::string expand_tabs(const std::string& text, int tab_width = 8) {
    std::string output;
    int column = 0;

    for (char character : text) {
        if (character == '\t') {
            int padding = tab_width - (column % tab_width);
            output.append(padding, ' ');
            column += padding;
        } else {
            output.push_back(character);
            ++column;
        }
    }

    return output;
}

std::string fit_to_width(const std::string& text, int width, bool pad) {
    std::string expanded = expand_tabs(text);

    if ((int)expanded.size() > width) {
        expanded.resize(width);
    }
    if (pad && (int)expanded.size() < width) {
        expanded.append(width - expanded.size(), ' ');
    }

    return expanded;
}

bool is_all_space(const std::string& text) {
    for (unsigned char character : text) {
        if (!std::isspace(character)) return false;
    }
    return true;
}

std::string leading_whitespace(const std::string& text) {
    size_t index = 0;
    while (index < text.size() && std::isspace((unsigned char)text[index])) ++index;
    return text.substr(0, index);
}

std::string trailing_whitespace(const std::string& text) {
    size_t index = text.size();
    while (index > 0 && std::isspace((unsigned char)text[index - 1])) --index;
    return text.substr(index);
}

}  // namespace

std::string format_normal(const std::vector<edit>& script,
                          const token_list& tokens_a, const token_list& tokens_b,
                          const colour_palette& colours) {
    std::string output;

    for (const script_segment& segment : split_into_segments(script)) {
        if (!segment.is_change) {
            continue;
        }
        const change_block& block = segment.block;

        std::string header;
        if (block.count_a > 0 && block.count_b > 0) {
            header = normal_range(block.first_a, block.count_a) + "c" + normal_range(block.first_b, block.count_b);
        } else if (block.count_a > 0) {
            header = normal_range(block.first_a, block.count_a) + "d" + std::to_string(block.position_b);
        } else {
            header = std::to_string(block.position_a) + "a" + normal_range(block.first_b, block.count_b);
        }
        output += colours.hunk_header + header + colours.reset + "\n";

        for (int offset = 0; offset < block.count_a; ++offset) {
            int line = block.first_a + offset;
            output += colours.deletion + "< " + tokens_a.text[line] + colours.reset + "\n";
            if (needs_no_newline_marker(tokens_a, line)) output += no_newline_marker;
        }

        if (block.count_a > 0 && block.count_b > 0) {
            output += "---\n";
        }

        for (int offset = 0; offset < block.count_b; ++offset) {
            int line = block.first_b + offset;
            output += colours.insertion + "> " + tokens_b.text[line] + colours.reset + "\n";
            if (needs_no_newline_marker(tokens_b, line)) output += no_newline_marker;
        }
    }

    return output;
}

static std::string unified_range(int start, int length) {
    // Empty ranges use the preceding line number followed by ",0".
    if (length == 0) return std::to_string(start) + ",0";
    if (length == 1) return std::to_string(start + 1);
    return std::to_string(start + 1) + "," + std::to_string(length);
}

std::string format_unified(const std::vector<edit>& script,
                           const token_list& tokens_a, const token_list& tokens_b,
                           int context_lines,
                           const file_header_info& header_a, const file_header_info& header_b,
                           const colour_palette& colours) {
    std::vector<hunk> hunks = make_hunks(script, context_lines);
    if (hunks.empty()) {
        return "";
    }

    std::string output;

    output += colours.file_header + "--- " + header_a.label;
    if (!header_a.timestamp.empty()) output += "\t" + header_a.timestamp;
    output += colours.reset + "\n";

    output += colours.file_header + "+++ " + header_b.label;
    if (!header_b.timestamp.empty()) output += "\t" + header_b.timestamp;
    output += colours.reset + "\n";

    for (const hunk& current : hunks) {
        output += colours.hunk_header + "@@ -" + unified_range(current.start_a, current.length_a) +
                  " +" + unified_range(current.start_b, current.length_b) + " @@" + colours.reset + "\n";

        for (const edit& current_edit : current.edits) {
            switch (current_edit.operation) {
                case edit_operation::equal:
                    output += " " + tokens_a.text[current_edit.index_a] + "\n";
                    if (needs_no_newline_marker(tokens_a, current_edit.index_a)) output += no_newline_marker;
                    break;

                case edit_operation::deletion:
                    output += colours.deletion + "-" + tokens_a.text[current_edit.index_a] + colours.reset + "\n";
                    if (needs_no_newline_marker(tokens_a, current_edit.index_a)) output += no_newline_marker;
                    break;

                case edit_operation::insertion:
                    output += colours.insertion + "+" + tokens_b.text[current_edit.index_b] + colours.reset + "\n";
                    if (needs_no_newline_marker(tokens_b, current_edit.index_b)) output += no_newline_marker;
                    break;
            }
        }
    }

    return output;
}

std::string format_side_by_side(const std::vector<edit>& script,
                                const token_list& tokens_a, const token_list& tokens_b,
                                int total_width, bool suppress_common_lines,
                                const colour_palette& colours) {
    const int column_width = std::max(8, (total_width - 3) / 2);
    std::string output;

    auto add_row = [&](const std::string* left, const std::string* right, char separator) {
        std::string left_text = fit_to_width(left ? *left : "", column_width, true);
        std::string right_text = fit_to_width(right ? *right : "", column_width, false);

        bool colour_left = (separator == '<' || separator == '|');
        bool colour_right = (separator == '>' || separator == '|');

        if (colour_left) output += colours.deletion;
        output += left_text;
        if (colour_left) output += colours.reset;

        output += " ";
        output += separator;
        output += " ";

        if (colour_right) output += colours.insertion;
        output += right_text;
        if (colour_right) output += colours.reset;

        output += "\n";
    };

    for (const script_segment& segment : split_into_segments(script)) {
        if (!segment.is_change) {
            if (!suppress_common_lines) {
                add_row(&tokens_a.text[segment.equal_edit.index_a],
                        &tokens_b.text[segment.equal_edit.index_b], ' ');
            }
            continue;
        }

        const change_block& block = segment.block;
        int paired = std::min(block.count_a, block.count_b);

        for (int offset = 0; offset < paired; ++offset) {
            add_row(&tokens_a.text[block.first_a + offset], &tokens_b.text[block.first_b + offset], '|');
        }
        for (int offset = paired; offset < block.count_a; ++offset) {
            add_row(&tokens_a.text[block.first_a + offset], nullptr, '<');
        }
        for (int offset = paired; offset < block.count_b; ++offset) {
            add_row(nullptr, &tokens_b.text[block.first_b + offset], '>');
        }
    }

    return output;
}

std::string format_inline(const std::vector<edit>& script,
                          const token_list& tokens_a, const token_list& tokens_b,
                          const colour_palette& colours) {
    std::string output;

    for (const script_segment& segment : split_into_segments(script)) {
        if (!segment.is_change) {
            output += tokens_a.text[segment.equal_edit.index_a];
            continue;
        }

        const change_block& block = segment.block;
        std::string deleted;
        std::string inserted;
        for (int offset = 0; offset < block.count_a; ++offset) deleted += tokens_a.text[block.first_a + offset];
        for (int offset = 0; offset < block.count_b; ++offset) inserted += tokens_b.text[block.first_b + offset];

        // Whitespace-only changes retain the new spacing without markers.
        if (is_all_space(deleted) && is_all_space(inserted)) {
            output += inserted;
            continue;
        }

        // Shared boundary whitespace is moved outside the markers so inline
        // changes remain readable.
        std::string leading;
        std::string trailing;

        if (deleted.empty()) {
            leading = leading_whitespace(inserted);
            trailing = trailing_whitespace(inserted);
        } else if (inserted.empty()) {
            leading = leading_whitespace(deleted);
            trailing = trailing_whitespace(deleted);
        } else {
            if (leading_whitespace(deleted) == leading_whitespace(inserted)) leading = leading_whitespace(deleted);
            if (trailing_whitespace(deleted) == trailing_whitespace(inserted)) trailing = trailing_whitespace(deleted);
        }

        if (!deleted.empty()) deleted.erase(0, leading.size());
        if (!inserted.empty()) inserted.erase(0, leading.size());
        if (deleted.size() >= trailing.size()) deleted.resize(deleted.size() - trailing.size());
        if (inserted.size() >= trailing.size()) inserted.resize(inserted.size() - trailing.size());

        output += leading;
        if (!deleted.empty()) output += colours.deletion + "[-" + deleted + "-]" + colours.reset;
        if (!inserted.empty()) output += colours.insertion + "{+" + inserted + "+}" + colours.reset;
        output += trailing;
    }

    return output;
}

diff_statistics compute_statistics(const std::vector<edit>& script, int context_lines) {
    diff_statistics statistics;

    for (const edit& current : script) {
        if (current.operation == edit_operation::insertion) ++statistics.insertions;
        if (current.operation == edit_operation::deletion) ++statistics.deletions;
    }

    statistics.hunks = (int)make_hunks(script, context_lines).size();
    return statistics;
}

std::string format_statistics(const diff_statistics& statistics,
                              const std::string& name_a, const std::string& name_b,
                              const colour_palette& colours) {
    int total = statistics.insertions + statistics.deletions;
    int bar_width = std::min(total, 40);
    int plus_count = total > 0 ? (int)((long long)statistics.insertions * bar_width / total) : 0;
    int minus_count = bar_width - plus_count;

    std::string output;
    output += " " + name_a + " => " + name_b + " | " + std::to_string(total) + " ";
    output += colours.insertion + std::string(plus_count, '+') + colours.reset;
    output += colours.deletion + std::string(minus_count, '-') + colours.reset + "\n";

    output += " " + std::to_string(statistics.hunks) + (statistics.hunks == 1 ? " hunk, " : " hunks, ");
    output += std::to_string(statistics.insertions) + (statistics.insertions == 1 ? " insertion(+), " : " insertions(+), ");
    output += std::to_string(statistics.deletions) + (statistics.deletions == 1 ? " deletion(-)\n" : " deletions(-)\n");

    return output;
}

}  // namespace mydiff
