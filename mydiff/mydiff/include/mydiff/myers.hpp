#pragma once

#include <vector>

namespace mydiff {

enum class edit_operation { equal, deletion, insertion };

// Both indices are retained so consumers can calculate positions without
// replaying the preceding edits.
struct edit {
    edit_operation operation;
    int index_a;
    int index_b;
};

enum class algorithm_kind { linear_space, greedy };

// Symbols absent from the other sequence can be pruned without changing the
// shortest edit distance because they cannot belong to a common subsequence.
std::vector<edit> compute_diff(const std::vector<int>& sequence_a,
                               const std::vector<int>& sequence_b,
                               algorithm_kind algorithm = algorithm_kind::linear_space,
                               bool prune_unique_symbols = true);

int edit_distance(const std::vector<edit>& script);

bool script_is_valid(const std::vector<int>& sequence_a,
                     const std::vector<int>& sequence_b,
                     const std::vector<edit>& script);

}  // namespace mydiff
