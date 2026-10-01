#include "mydiff/patch.hpp"

#include <cctype>
#include <cstring>

namespace mydiff {

namespace {

std::vector<std::string> split_lines(const std::string& input, bool& trailing_newline) {
    std::vector<std::string> lines;
    size_t start = 0;
    trailing_newline = true;

    while (start < input.size()) {
        size_t newline = input.find('\n', start);

        if (newline == std::string::npos) {
            lines.push_back(input.substr(start));
            trailing_newline = false;
            break;
        }

        lines.push_back(input.substr(start, newline - start));
        start = newline + 1;
    }

    return lines;
}

bool starts_with(const std::string& text, const char* prefix) {
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool parse_range(const std::string& text, size_t& position, int& start, int& length) {
    size_t scan = position;

    if (scan >= text.size() || !std::isdigit((unsigned char)text[scan])) return false;

    start = 0;
    while (scan < text.size() && std::isdigit((unsigned char)text[scan])) {
        start = start * 10 + (text[scan] - '0');
        ++scan;
    }

    length = 1;
    if (scan < text.size() && text[scan] == ',') {
        ++scan;
        if (scan >= text.size() || !std::isdigit((unsigned char)text[scan])) return false;

        length = 0;
        while (scan < text.size() && std::isdigit((unsigned char)text[scan])) {
            length = length * 10 + (text[scan] - '0');
            ++scan;
        }
    }

    position = scan;
    return true;
}

bool parse_hunk_header(const std::string& line, patch_hunk& result) {
    if (!starts_with(line, "@@ -")) return false;

    size_t position = 4;
    int start_a, length_a, start_b, length_b;

    if (!parse_range(line, position, start_a, length_a)) return false;
    if (position + 2 > line.size() || line[position] != ' ' || line[position + 1] != '+') return false;
    position += 2;
    if (!parse_range(line, position, start_b, length_b)) return false;
    if (line.compare(position, 3, " @@") != 0) return false;

    // Empty ranges identify the line before the insertion position.
    result.start_a = (length_a == 0) ? start_a : start_a - 1;
    result.start_b = (length_b == 0) ? start_b : start_b - 1;
    result.length_a = length_a;
    result.length_b = length_b;
    return true;
}

std::string header_label(const std::string& line) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos) return line.substr(4);
    return line.substr(4, tab - 4);
}

}  // namespace

bool parse_unified_patch(const std::string& text, unified_patch& result, std::string& error_message) {
    bool ignored_trailing_newline;
    std::vector<std::string> lines = split_lines(text, ignored_trailing_newline);
    size_t index = 0;
    result = unified_patch{};

    while (index < lines.size() && !starts_with(lines[index], "--- ") && !starts_with(lines[index], "@@ ")) {
        ++index;
    }

    if (index < lines.size() && starts_with(lines[index], "--- ")) {
        result.old_label = header_label(lines[index]);
        ++index;

        if (index >= lines.size() || !starts_with(lines[index], "+++ ")) {
            error_message = "expected a '+++' header after '---'";
            return false;
        }
        result.new_label = header_label(lines[index]);
        ++index;
    }

    while (index < lines.size()) {
        patch_hunk current;

        if (!parse_hunk_header(lines[index], current)) {
            if (result.hunks.empty()) {
                error_message = "malformed hunk header at line " + std::to_string(index + 1) + ": " + lines[index];
                return false;
            }
            break;
        }
        ++index;

        int remaining_a = current.length_a;
        int remaining_b = current.length_b;

        while ((remaining_a > 0 || remaining_b > 0) && index < lines.size()) {
            const std::string& line = lines[index];

            if (starts_with(line, "\\")) {
                if (!current.lines.empty()) current.lines.back().no_newline = true;
                ++index;
                continue;
            }

            // Blank context lines are accepted when their leading marker is absent.
            char tag = line.empty() ? ' ' : line[0];
            std::string body = line.empty() ? "" : line.substr(1);

            if (tag == ' ') {
                --remaining_a;
                --remaining_b;
            } else if (tag == '-') {
                --remaining_a;
            } else if (tag == '+') {
                --remaining_b;
            } else {
                error_message = "unexpected line inside a hunk at line " + std::to_string(index + 1) + ": " + line;
                return false;
            }

            current.lines.push_back({tag, body, false});
            ++index;
        }

        if (remaining_a != 0 || remaining_b != 0) {
            error_message = "a hunk is truncated: its line counts do not match the header";
            return false;
        }

        if (index < lines.size() && starts_with(lines[index], "\\")) {
            if (!current.lines.empty()) current.lines.back().no_newline = true;
            ++index;
        }

        result.hunks.push_back(std::move(current));
    }

    if (result.hunks.empty()) {
        error_message = "no hunks found";
        return false;
    }

    return true;
}

apply_result apply_patch(const std::string& original, const unified_patch& patch, bool reverse) {
    apply_result result;

    bool original_trailing_newline;
    std::vector<std::string> old_lines = split_lines(original, original_trailing_newline);

    std::vector<std::string> new_lines;
    bool output_trailing_newline = true;

    auto copy_old_line = [&](size_t old_index) {
        new_lines.push_back(old_lines[old_index]);
        bool is_last = (old_index + 1 == old_lines.size());
        output_trailing_newline = !(is_last && !original_trailing_newline);
    };

    auto add_patch_line = [&](const patch_line& line) {
        new_lines.push_back(line.text);
        output_trailing_newline = !line.no_newline;
    };

    const char deletion_tag = reverse ? '+' : '-';
    const char insertion_tag = reverse ? '-' : '+';

    size_t old_position = 0;

    for (size_t hunk_index = 0; hunk_index < patch.hunks.size(); ++hunk_index) {
        const patch_hunk& current = patch.hunks[hunk_index];

        std::vector<const patch_line*> expected_lines;
        for (const patch_line& line : current.lines) {
            if (line.tag == ' ' || line.tag == deletion_tag) expected_lines.push_back(&line);
        }

        auto matches_at = [&](size_t candidate) {
            if (candidate + expected_lines.size() > old_lines.size()) return false;
            for (size_t offset = 0; offset < expected_lines.size(); ++offset) {
                if (old_lines[candidate + offset] != expected_lines[offset]->text) return false;
            }
            return true;
        };

        long expected_position = reverse ? current.start_b : current.start_a;
        long found_position = -1;

        // The search expands from the stated position without crossing an
        // earlier applied hunk.
        for (long distance = 0; distance <= (long)old_lines.size() && found_position < 0; ++distance) {
            long below = expected_position + distance;
            long above = expected_position - distance;

            if (below >= (long)old_position && matches_at((size_t)below)) {
                found_position = below;
            } else if (distance > 0 && above >= (long)old_position && matches_at((size_t)above)) {
                found_position = above;
            }
        }

        if (found_position < 0) {
            result.error_message = "hunk #" + std::to_string(hunk_index + 1) +
                                   " FAILED: context not found near line " + std::to_string(expected_position + 1);
            return result;
        }

        if (found_position != expected_position) ++result.hunks_with_offset;

        while (old_position < (size_t)found_position) {
            copy_old_line(old_position);
            ++old_position;
        }

        for (const patch_line& line : current.lines) {
            if (line.tag == ' ') {
                copy_old_line(old_position);
                ++old_position;
            } else if (line.tag == deletion_tag) {
                ++old_position;
            } else if (line.tag == insertion_tag) {
                add_patch_line(line);
            }
        }

        ++result.hunks_applied;
    }

    while (old_position < old_lines.size()) {
        copy_old_line(old_position);
        ++old_position;
    }

    std::string joined;
    for (size_t index = 0; index < new_lines.size(); ++index) {
        joined += new_lines[index];
        if (index + 1 < new_lines.size() || output_trailing_newline) joined += '\n';
    }

    result.ok = true;
    result.output = std::move(joined);
    return result;
}

}  // namespace mydiff
