#pragma once

#include <vector>

#include "mydiff/myers.hpp"
#include "mydiff/tokenize.hpp"

namespace mydiff {

// Equivalent change blocks are moved towards blank line boundaries without
// changing the edit distance.
void slide_change_blocks(std::vector<edit>& script,
                         const token_list& tokens_a, const token_list& tokens_b);

}  // namespace mydiff
