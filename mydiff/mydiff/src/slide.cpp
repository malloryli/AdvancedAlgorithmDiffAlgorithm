#include "mydiff/slide.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace mydiff {

namespace {

bool is_blank(const std::string& text) {
    for (unsigned char character : text) {
        if (!std::isspace(character)) return false;
    }
    return true;
}

// Higher scores favour boundaries near blank lines.
int position_score(const token_list& tokens, int first, int length) {
    int score = 0;

    if (is_blank(tokens.text[first])) score += 2;
    if (first > 0 && is_blank(tokens.text[first - 1])) score += 1;
    if (first + length < (int)tokens.text.size() && is_blank(tokens.text[first + length])) score += 1;

    return score;
}

void reindex(std::vector<edit>& script) {
    int position_x = 0;
    int position_y = 0;

    for (edit& current : script) {
        current.index_a = position_x;
        current.index_b = position_y;
        if (current.operation != edit_operation::insertion) ++position_x;
        if (current.operation != edit_operation::deletion) ++position_y;
    }
}

}  // namespace

void slide_change_blocks(std::vector<edit>& script,
                         const token_list& tokens_a, const token_list& tokens_b) {
    const int script_length = (int)script.size();
    int index = 0;
    int position_x = 0;
    int position_y = 0;

    while (index < script_length) {
        const edit_operation operation = script[index].operation;

        if (operation == edit_operation::equal) {
            ++index;
            ++position_x;
            ++position_y;
            continue;
        }

        int block_start = index;
        int block_end = index;
        bool is_pure = true;

        while (block_end < script_length && script[block_end].operation != edit_operation::equal) {
            if (script[block_end].operation != operation) is_pure = false;
            ++block_end;
        }

        if (!is_pure) {
            for (int scan = block_start; scan < block_end; ++scan) {
                if (script[scan].operation != edit_operation::insertion) ++position_x;
                if (script[scan].operation != edit_operation::deletion) ++position_y;
            }
            index = block_end;
            continue;
        }

        const token_list& tokens = (operation == edit_operation::insertion) ? tokens_b : tokens_a;
        const std::vector<int>& ids = tokens.symbol_ids;
        const int block_length = block_end - block_start;
        const int first = (operation == edit_operation::insertion) ? position_y : position_x;

        // A block can cross an equal line only when that line matches the
        // corresponding end of the block.
        int slide_up = 0;
        while (block_start - slide_up - 1 >= 0 &&
               script[block_start - slide_up - 1].operation == edit_operation::equal &&
               ids[first - slide_up - 1] == ids[first - slide_up + block_length - 1]) {
            ++slide_up;
        }

        int slide_down = 0;
        while (block_end + slide_down < script_length &&
               script[block_end + slide_down].operation == edit_operation::equal &&
               ids[first + slide_down] == ids[first + slide_down + block_length]) {
            ++slide_down;
        }

        int best_shift = 0;
        int best_score = position_score(tokens, first, block_length);

        for (int shift = -slide_up; shift <= slide_down; ++shift) {
            if (shift == 0) continue;
            int score = position_score(tokens, first + shift, block_length);

            if (score > best_score || (score == best_score && std::abs(shift) < std::abs(best_shift))) {
                best_score = score;
                best_shift = shift;
            }
        }

        // Sliding rewrites only the operation order within the equivalent region.
        int region_start = block_start;
        int region_end = block_end;

        if (best_shift < 0) {
            int up = -best_shift;
            region_start = block_start - up;
            for (int scan = region_start; scan < region_start + block_length; ++scan) script[scan].operation = operation;
            for (int scan = region_start + block_length; scan < block_end; ++scan) script[scan].operation = edit_operation::equal;
        } else if (best_shift > 0) {
            int down = best_shift;
            region_end = block_end + down;
            for (int scan = block_start; scan < block_start + down; ++scan) script[scan].operation = edit_operation::equal;
            for (int scan = block_start + down; scan < region_end; ++scan) script[scan].operation = operation;
        }

        // Position counters are recalculated across the rewritten region.
        position_x -= (block_start - region_start);
        position_y -= (block_start - region_start);

        for (int scan = region_start; scan < region_end; ++scan) {
            if (script[scan].operation != edit_operation::insertion) ++position_x;
            if (script[scan].operation != edit_operation::deletion) ++position_y;
        }

        index = region_end;
    }

    reindex(script);
}

}  // namespace mydiff
