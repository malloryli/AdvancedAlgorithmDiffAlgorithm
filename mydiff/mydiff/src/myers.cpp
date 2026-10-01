#include "mydiff/myers.hpp"

#include <algorithm>
#include <cassert>
#include <climits>

namespace mydiff {

namespace {

// Path segments are reconstructed from their endpoints and the matching
// symbols rather than relying on a particular recorded snake shape.
void append_segment_edits(const int* sequence_a, const int* sequence_b,
                          int position_x, int position_y,
                          int end_x, int end_y,
                          std::vector<edit>& output) {
    while (position_x < end_x || position_y < end_y) {

        while (position_x < end_x && position_y < end_y &&
               sequence_a[position_x] == sequence_b[position_y]) {
            output.push_back({edit_operation::equal, position_x, position_y});
            ++position_x;
            ++position_y;
        }

        if (position_x >= end_x && position_y >= end_y) {
            break;
        }

        int remaining_x = end_x - position_x;
        int remaining_y = end_y - position_y;

        if (remaining_x > remaining_y) {
            output.push_back({edit_operation::deletion, position_x, position_y});
            ++position_x;
        } else if (remaining_x < remaining_y) {
            output.push_back({edit_operation::insertion, position_x, position_y});
            ++position_y;
        } else {
            // This fallback guarantees termination if an invalid segment is supplied.
            output.push_back({edit_operation::deletion, position_x, position_y});
            ++position_x;
        }
    }
}

struct greedy_search {
    const int* sequence_a;
    int length_a;
    const int* sequence_b;
    int length_b;

    // Backtracking requires the active frontier from every completed round.
    std::vector<std::vector<int>> frontier_history;

    void run(std::vector<edit>& output) {
        const int max_edits = length_a + length_b;

        const int array_offset = max_edits + 1;
        std::vector<int> furthest_x(2 * max_edits + 3, 0);

        // The virtual neighbour allows the first round to use the general rule.
        furthest_x[array_offset + 1] = 0;

        for (int edit_count = 0; edit_count <= max_edits; ++edit_count) {

            frontier_history.emplace_back(furthest_x.begin() + (array_offset - edit_count),
                                          furthest_x.begin() + (array_offset + edit_count + 1));

            for (int diagonal = -edit_count; diagonal <= edit_count; diagonal += 2) {

                // The path that reaches further along x dominates the other
                // path on the same diagonal.
                int x;
                bool came_from_above = (diagonal == -edit_count) ||
                                       (diagonal != edit_count &&
                                        furthest_x[array_offset + diagonal - 1] <
                                        furthest_x[array_offset + diagonal + 1]);
                if (came_from_above) {
                    x = furthest_x[array_offset + diagonal + 1];
                } else {
                    x = furthest_x[array_offset + diagonal - 1] + 1;
                }

                int y = x - diagonal;

                while (x < length_a && y < length_b && sequence_a[x] == sequence_b[y]) {
                    ++x;
                    ++y;
                }

                furthest_x[array_offset + diagonal] = x;

                if (x >= length_a && y >= length_b) {
                    backtrack(edit_count, output);
                    return;
                }
            }
        }

        assert(false && "the greedy search must finish within N+M rounds");
    }

    // The saved frontiers reconstruct the path from the destination backwards.
    void backtrack(int final_edit_count, std::vector<edit>& output) {
        std::vector<edit> reversed;

        int x = length_a;
        int y = length_b;

        for (int edit_count = final_edit_count; edit_count >= 0; --edit_count) {

            const std::vector<int>& frontier = frontier_history[edit_count];
            auto furthest_x_on = [&](int diagonal) { return frontier[diagonal + edit_count]; };

            int diagonal = x - y;

            int previous_diagonal;
            bool came_from_above = (diagonal == -edit_count) ||
                                   (diagonal != edit_count &&
                                    furthest_x_on(diagonal - 1) < furthest_x_on(diagonal + 1));
            if (came_from_above) {
                previous_diagonal = diagonal + 1;
            } else {
                previous_diagonal = diagonal - 1;
            }

            int previous_x = 0;
            int previous_y = 0;
            if (edit_count > 0) {
                previous_x = furthest_x_on(previous_diagonal);
                previous_y = previous_x - previous_diagonal;
            }

            while (x > previous_x && y > previous_y) {
                --x;
                --y;
                reversed.push_back({edit_operation::equal, x, y});
            }

            if (edit_count > 0) {
                if (x == previous_x + 1 && y == previous_y) {
                    reversed.push_back({edit_operation::deletion, previous_x, previous_y});
                } else if (x == previous_x && y == previous_y + 1) {
                    reversed.push_back({edit_operation::insertion, previous_x, previous_y});
                } else {
                    assert(false && "malformed snake while backtracking");
                }
            }

            x = previous_x;
            y = previous_y;
        }

        std::reverse(reversed.begin(), reversed.end());
        output.insert(output.end(), reversed.begin(), reversed.end());
    }
};

// The linear space search divides each box at a middle snake on an optimal path.
struct point {
    int x;
    int y;
};

struct snake {
    point start;
    point finish;
};

struct linear_space_search {
    const int* sequence_a;
    int length_a;
    const int* sequence_b;
    int length_b;

    std::vector<point> path;
    std::vector<int> forward_frontier;
    std::vector<int> reverse_frontier;

    void run(std::vector<edit>& output) {
        path.clear();
        path.push_back({0, 0});
        find_path(0, 0, length_a, length_b);
        path.push_back({length_a, length_b});

        for (size_t index = 0; index + 1 < path.size(); ++index) {
            append_segment_edits(sequence_a, sequence_b,
                                 path[index].x, path[index].y,
                                 path[index + 1].x, path[index + 1].y,
                                 output);
        }
    }

    // The caller adds the box corners, while this function appends its interior path.
    void find_path(int left, int top, int right, int bottom) {
        const int width = right - left;
        const int height = bottom - top;

        if (width == 0 || height == 0) {
            return;
        }

        snake middle;
        if (!find_middle_snake(left, top, right, bottom, middle)) {
            return;
        }

        find_path(left, top, middle.start.x, middle.start.y);

        bool start_is_corner = (middle.start.x == left && middle.start.y == top);
        bool finish_is_corner = (middle.finish.x == right && middle.finish.y == bottom);
        bool snake_is_a_point = (middle.finish.x == middle.start.x && middle.finish.y == middle.start.y);

        if (!start_is_corner) {
            path.push_back(middle.start);
        }
        if (!finish_is_corner && !snake_is_a_point) {
            path.push_back(middle.finish);
        }

        find_path(middle.finish.x, middle.finish.y, right, bottom);
    }

    bool find_middle_snake(int left, int top, int right, int bottom, snake& result) {
        const int width = right - left;
        const int height = bottom - top;
        const int box_size = width + height;

        if (box_size == 0) {
            return false;
        }

        // Reverse diagonals are numbered relative to the bottom right corner.
        const int delta = width - height;
        const bool delta_is_odd = (delta % 2 != 0);

        const int max_rounds = (box_size + 1) / 2;
        const int array_offset = max_rounds + 1;

        forward_frontier.assign(2 * max_rounds + 3, INT_MIN);
        reverse_frontier.assign(2 * max_rounds + 3, INT_MAX);

        // Virtual neighbours allow both searches to use the general recurrence.
        forward_frontier[array_offset + 1] = left;
        reverse_frontier[array_offset + 1] = bottom;

        for (int edit_count = 0; edit_count <= max_rounds; ++edit_count) {

            for (int diagonal = edit_count; diagonal >= -edit_count; diagonal -= 2) {

                int snake_start_x;
                int x;
                bool came_from_above = (diagonal == -edit_count) ||
                                       (diagonal != edit_count &&
                                        forward_frontier[array_offset + diagonal - 1] <
                                        forward_frontier[array_offset + diagonal + 1]);
                if (came_from_above) {
                    snake_start_x = forward_frontier[array_offset + diagonal + 1];
                    x = snake_start_x;
                } else {
                    snake_start_x = forward_frontier[array_offset + diagonal - 1];
                    x = snake_start_x + 1;
                }

                int y = top + (x - left) - diagonal;

                // A vertical entry places the recorded start one row above.
                int snake_start_y = y;
                if (edit_count > 0 && x == snake_start_x) {
                    snake_start_y = y - 1;
                }

                while (x < right && y < bottom && sequence_a[x] == sequence_b[y]) {
                    ++x;
                    ++y;
                }

                forward_frontier[array_offset + diagonal] = x;

                // For odd delta, the forward round can use only reverse
                // diagonals completed in the preceding round.
                int reverse_diagonal = diagonal - delta;
                bool reverse_is_defined = (reverse_diagonal >= -(edit_count - 1) &&
                                           reverse_diagonal <= edit_count - 1);

                if (delta_is_odd && reverse_is_defined &&
                    y >= reverse_frontier[array_offset + reverse_diagonal]) {
                    result.start = {snake_start_x, snake_start_y};
                    result.finish = {x, y};
                    return true;
                }
            }

            for (int reverse_diagonal = edit_count; reverse_diagonal >= -edit_count; reverse_diagonal -= 2) {

                int snake_start_y;
                int y;
                bool came_from_right = (reverse_diagonal == -edit_count) ||
                                       (reverse_diagonal != edit_count &&
                                        reverse_frontier[array_offset + reverse_diagonal - 1] >
                                        reverse_frontier[array_offset + reverse_diagonal + 1]);
                if (came_from_right) {
                    snake_start_y = reverse_frontier[array_offset + reverse_diagonal + 1];
                    y = snake_start_y;
                } else {
                    snake_start_y = reverse_frontier[array_offset + reverse_diagonal - 1];
                    y = snake_start_y - 1;
                }

                int diagonal = reverse_diagonal + delta;
                int x = left + (y - top) + diagonal;

                int snake_start_x = x;
                if (edit_count > 0 && y == snake_start_y) {
                    snake_start_x = x + 1;
                }

                while (x > left && y > top && sequence_a[x - 1] == sequence_b[y - 1]) {
                    --x;
                    --y;
                }

                reverse_frontier[array_offset + reverse_diagonal] = y;

                // For even delta, the reverse round can use forward diagonals
                // completed in the current round.
                bool forward_is_defined = (diagonal >= -edit_count && diagonal <= edit_count);

                if (!delta_is_odd && forward_is_defined &&
                    x <= forward_frontier[array_offset + diagonal]) {
                    result.start = {x, y};
                    result.finish = {snake_start_x, snake_start_y};
                    return true;
                }
            }
        }

        assert(false && "the middle snake must be found within ceil((N+M)/2) rounds");
        return false;
    }
};

void run_search(const int* sequence_a, int length_a,
                const int* sequence_b, int length_b,
                algorithm_kind algorithm, std::vector<edit>& output) {
    if (length_a == 0 && length_b == 0) {
        return;
    }

    if (algorithm == algorithm_kind::greedy) {
        greedy_search search{sequence_a, length_a, sequence_b, length_b, {}};
        search.run(output);
    } else {
        linear_space_search search{sequence_a, length_a, sequence_b, length_b, {}, {}, {}};
        search.run(output);
    }
}

// Symbols absent from the other sequence are restored as forced edits after
// the reduced search completes.
void run_search_with_pruning(const int* sequence_a, int length_a,
                             const int* sequence_b, int length_b,
                             algorithm_kind algorithm, std::vector<edit>& output) {
    int max_symbol = -1;
    for (int index = 0; index < length_a; ++index) max_symbol = std::max(max_symbol, sequence_a[index]);
    for (int index = 0; index < length_b; ++index) max_symbol = std::max(max_symbol, sequence_b[index]);

    std::vector<bool> occurs_in_a(max_symbol + 1, false);
    std::vector<bool> occurs_in_b(max_symbol + 1, false);
    for (int index = 0; index < length_a; ++index) occurs_in_a[sequence_a[index]] = true;
    for (int index = 0; index < length_b; ++index) occurs_in_b[sequence_b[index]] = true;

    std::vector<int> survivors_a;
    std::vector<int> survivors_b;
    std::vector<int> original_index_a;
    std::vector<int> original_index_b;

    for (int index = 0; index < length_a; ++index) {
        if (occurs_in_b[sequence_a[index]]) {
            survivors_a.push_back(sequence_a[index]);
            original_index_a.push_back(index);
        }
    }
    for (int index = 0; index < length_b; ++index) {
        if (occurs_in_a[sequence_b[index]]) {
            survivors_b.push_back(sequence_b[index]);
            original_index_b.push_back(index);
        }
    }

    std::vector<edit> survivor_script;
    run_search(survivors_a.data(), (int)survivors_a.size(),
               survivors_b.data(), (int)survivors_b.size(),
               algorithm, survivor_script);

    // Pruned symbols are emitted before the next surviving edit at their
    // original positions.
    int position_x = 0;
    int position_y = 0;

    auto flush_pruned_up_to = [&](int target_x, int target_y) {
        while (position_x < target_x) {
            output.push_back({edit_operation::deletion, position_x, position_y});
            ++position_x;
        }
        while (position_y < target_y) {
            output.push_back({edit_operation::insertion, position_x, position_y});
            ++position_y;
        }
    };

    for (const edit& current : survivor_script) {
        int target_x = position_x;
        int target_y = position_y;
        if (current.operation != edit_operation::insertion) target_x = original_index_a[current.index_a];
        if (current.operation != edit_operation::deletion) target_y = original_index_b[current.index_b];

        flush_pruned_up_to(target_x, target_y);

        output.push_back({current.operation, position_x, position_y});
        if (current.operation != edit_operation::insertion) ++position_x;
        if (current.operation != edit_operation::deletion) ++position_y;
    }

    flush_pruned_up_to(length_a, length_b);
}

}  // namespace

// Common prefixes and suffixes are removed before the remaining search.
std::vector<edit> compute_diff(const std::vector<int>& sequence_a,
                               const std::vector<int>& sequence_b,
                               algorithm_kind algorithm,
                               bool prune_unique_symbols) {
    const int length_a = (int)sequence_a.size();
    const int length_b = (int)sequence_b.size();

    int prefix_length = 0;
    while (prefix_length < length_a && prefix_length < length_b &&
           sequence_a[prefix_length] == sequence_b[prefix_length]) {
        ++prefix_length;
    }

    int suffix_length = 0;
    while (suffix_length < length_a - prefix_length && suffix_length < length_b - prefix_length &&
           sequence_a[length_a - 1 - suffix_length] == sequence_b[length_b - 1 - suffix_length]) {
        ++suffix_length;
    }

    std::vector<edit> output;
    output.reserve((size_t)std::max(length_a, length_b));

    for (int index = 0; index < prefix_length; ++index) {
        output.push_back({edit_operation::equal, index, index});
    }

    const int middle_length_a = length_a - prefix_length - suffix_length;
    const int middle_length_b = length_b - prefix_length - suffix_length;

    if (middle_length_a > 0 || middle_length_b > 0) {
        std::vector<edit> middle_script;

        if (prune_unique_symbols) {
            run_search_with_pruning(sequence_a.data() + prefix_length, middle_length_a,
                                    sequence_b.data() + prefix_length, middle_length_b,
                                    algorithm, middle_script);
        } else {
            run_search(sequence_a.data() + prefix_length, middle_length_a,
                       sequence_b.data() + prefix_length, middle_length_b,
                       algorithm, middle_script);
        }

        for (const edit& current : middle_script) {
            output.push_back({current.operation,
                              current.index_a + prefix_length,
                              current.index_b + prefix_length});
        }
    }

    for (int index = 0; index < suffix_length; ++index) {
        output.push_back({edit_operation::equal,
                          length_a - suffix_length + index,
                          length_b - suffix_length + index});
    }

    return output;
}

int edit_distance(const std::vector<edit>& script) {
    int distance = 0;
    for (const edit& current : script) {
        if (current.operation != edit_operation::equal) {
            ++distance;
        }
    }
    return distance;
}

bool script_is_valid(const std::vector<int>& sequence_a,
                     const std::vector<int>& sequence_b,
                     const std::vector<edit>& script) {
    std::vector<int> rebuilt;
    int position_x = 0;
    int position_y = 0;

    for (const edit& current : script) {
        if (current.index_a != position_x || current.index_b != position_y) {
            return false;
        }

        switch (current.operation) {
            case edit_operation::equal:
                if (position_x >= (int)sequence_a.size() || position_y >= (int)sequence_b.size()) return false;
                if (sequence_a[position_x] != sequence_b[position_y]) return false;
                rebuilt.push_back(sequence_a[position_x]);
                ++position_x;
                ++position_y;
                break;

            case edit_operation::deletion:
                if (position_x >= (int)sequence_a.size()) return false;
                ++position_x;
                break;

            case edit_operation::insertion:
                if (position_y >= (int)sequence_b.size()) return false;
                rebuilt.push_back(sequence_b[position_y]);
                ++position_y;
                break;
        }
    }

    return position_x == (int)sequence_a.size() &&
           position_y == (int)sequence_b.size() &&
           rebuilt == sequence_b;
}

}  // namespace mydiff
