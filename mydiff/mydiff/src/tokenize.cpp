#include "mydiff/tokenize.hpp"

#include <cctype>
#include <deque>
#include <string_view>
#include <unordered_map>

namespace mydiff {

namespace {

bool is_space(unsigned char character) {
    return std::isspace(character) != 0;
}

std::vector<std::string> split_lines(const std::string& input, bool& trailing_newline) {
    std::vector<std::string> lines;
    size_t start = 0;

    while (start < input.size()) {
        size_t newline = input.find('\n', start);

        if (newline == std::string::npos) {
            lines.push_back(input.substr(start));
            trailing_newline = false;
            return lines;
        }

        lines.push_back(input.substr(start, newline - start));
        start = newline + 1;
    }

    trailing_newline = true;
    return lines;
}

// Alternating whitespace and non-whitespace tokens preserves the original text.
std::vector<std::string> split_words(const std::string& input) {
    std::vector<std::string> words;
    size_t start = 0;

    while (start < input.size()) {
        size_t end = start;
        bool in_space = is_space((unsigned char)input[start]);

        while (end < input.size() && is_space((unsigned char)input[end]) == in_space) {
            ++end;
        }

        words.push_back(input.substr(start, end - start));
        start = end;
    }

    return words;
}

// Continuation bytes remain with their leading byte, producing one token per
// UTF-8 code point.
std::vector<std::string> split_characters(const std::string& input) {
    std::vector<std::string> characters;
    size_t start = 0;

    while (start < input.size()) {
        size_t end = start + 1;

        while (end < input.size() && ((unsigned char)input[end] & 0xC0) == 0x80) {
            ++end;
        }

        characters.push_back(input.substr(start, end - start));
        start = end;
    }

    return characters;
}

std::string normalise(const std::string& token, const tokenize_options& options) {
    std::string key;
    key.reserve(token.size());
    bool pending_space = false;

    for (unsigned char character : token) {
        if (is_space(character)) {
            if (options.ignore_all_space) {
                continue;
            }
            if (options.ignore_space_change) {
                pending_space = true;
                continue;
            }
        } else if (options.ignore_space_change && pending_space) {
            if (!key.empty()) {
                key.push_back(' ');
            }
            pending_space = false;
        }

        if (options.ignore_case) {
            key.push_back((char)std::tolower(character));
        } else {
            key.push_back((char)character);
        }
    }

    return key;
}

}  // namespace

void tokenize_pair(const std::string& input_a, const std::string& input_b,
                   const tokenize_options& options,
                   token_list& tokens_a, token_list& tokens_b) {

    // Each string view refers to an input buffer or to the deque below. Both
    // remain stable for the duration of tokenisation.
    std::unordered_map<std::string_view, int> table;
    std::deque<std::string> normalised_keys;

    const bool needs_normalise = options.ignore_case || options.ignore_all_space || options.ignore_space_change;

    auto intern = [&](std::string_view key) {
        auto found = table.find(key);
        if (found != table.end()) {
            return found->second;
        }
        int id = (int)table.size();
        table.emplace(key, id);
        return id;
    };

    auto process = [&](const std::string& input, token_list& tokens) {
        tokens = token_list{};

        switch (options.unit) {
            case granularity::lines:      tokens.text = split_lines(input, tokens.trailing_newline); break;
            case granularity::words:      tokens.text = split_words(input); break;
            case granularity::characters: tokens.text = split_characters(input); break;
        }

        tokens.symbol_ids.reserve(tokens.text.size());
        table.reserve(table.size() + tokens.text.size());

        size_t input_offset = 0;

        for (size_t index = 0; index < tokens.text.size(); ++index) {
            const std::string& token = tokens.text[index];

            std::string_view key(input.data() + input_offset, token.size());
            input_offset += token.size();
            if (options.unit == granularity::lines) {
                input_offset += 1;
            }

            // An unterminated final line receives a distinct comparison key.
            bool unterminated_last_line = options.unit == granularity::lines &&
                                          !tokens.trailing_newline &&
                                          index + 1 == tokens.text.size();

            if (needs_normalise || unterminated_last_line) {
                std::string normalised = needs_normalise ? normalise(token, options) : token;
                if (unterminated_last_line) {
                    normalised += "\x01<no-newline>";
                }
                normalised_keys.push_back(std::move(normalised));
                key = normalised_keys.back();
            }

            tokens.symbol_ids.push_back(intern(key));
        }
    };

    process(input_a, tokens_a);
    process(input_b, tokens_b);
}

}  // namespace mydiff
