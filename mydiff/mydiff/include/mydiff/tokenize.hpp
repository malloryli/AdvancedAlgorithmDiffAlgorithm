#pragma once

#include <string>
#include <vector>

namespace mydiff {

enum class granularity { lines, words, characters };

struct tokenize_options {
    granularity unit = granularity::lines;
    bool ignore_case = false;
    bool ignore_all_space = false;
    bool ignore_space_change = false;
};

struct token_list {
    std::vector<std::string> text;
    std::vector<int> symbol_ids;
    bool trailing_newline = true;
};

// A shared table assigns the same integer identifier to equal normalised tokens.
void tokenize_pair(const std::string& input_a, const std::string& input_b,
                   const tokenize_options& options,
                   token_list& tokens_a, token_list& tokens_b);

}  // namespace mydiff
