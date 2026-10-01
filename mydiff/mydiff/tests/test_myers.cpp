// Each variant must produce a valid script whose edit distance matches the
// result derived from a dynamic programming LCS implementation.

#include "mydiff/myers.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace mydiff;

static int lcs_length(const std::vector<int>& sequence_a, const std::vector<int>& sequence_b) {
    std::vector<std::vector<int>> table(sequence_a.size() + 1, std::vector<int>(sequence_b.size() + 1, 0));

    for (size_t i = 1; i <= sequence_a.size(); ++i) {
        for (size_t j = 1; j <= sequence_b.size(); ++j) {
            if (sequence_a[i - 1] == sequence_b[j - 1]) {
                table[i][j] = table[i - 1][j - 1] + 1;
            } else {
                table[i][j] = std::max(table[i - 1][j], table[i][j - 1]);
            }
        }
    }

    return table[sequence_a.size()][sequence_b.size()];
}

static int failures = 0;

static void check_one(const std::vector<int>& sequence_a, const std::vector<int>& sequence_b,
                      algorithm_kind algorithm, bool prune, const char* label) {
    std::vector<edit> script = compute_diff(sequence_a, sequence_b, algorithm, prune);

    int expected = (int)sequence_a.size() + (int)sequence_b.size() - 2 * lcs_length(sequence_a, sequence_b);
    bool valid = script_is_valid(sequence_a, sequence_b, script);
    int distance = edit_distance(script);

    if (!valid || distance != expected) {
        ++failures;
        std::printf("FAIL [%s] valid=%d D=%d expected=%d\n  a =", label, valid, distance, expected);
        for (int value : sequence_a) std::printf(" %d", value);
        std::printf("\n  b =");
        for (int value : sequence_b) std::printf(" %d", value);
        std::printf("\n");
    }
}

static void check_all_variants(const std::vector<int>& sequence_a, const std::vector<int>& sequence_b) {
    check_one(sequence_a, sequence_b, algorithm_kind::linear_space, true, "linear");
    check_one(sequence_a, sequence_b, algorithm_kind::linear_space, false, "linear, no pruning");
    check_one(sequence_a, sequence_b, algorithm_kind::greedy, true, "greedy");
    check_one(sequence_a, sequence_b, algorithm_kind::greedy, false, "greedy, no pruning");
}

int main(int argc, char** argv) {
    int iterations = argc > 1 ? std::atoi(argv[1]) : 2000;
    std::mt19937 generator(12345);

    check_all_variants({}, {});
    check_all_variants({1, 2, 3}, {});
    check_all_variants({}, {1, 2, 3});
    check_all_variants({1, 2, 3}, {1, 2, 3});
    check_all_variants({1, 2, 3}, {4, 5, 6});
    check_all_variants({1}, {2});
    check_all_variants({1, 1, 1, 1}, {1, 1});
    check_all_variants({1, 2}, {2, 1});
    check_all_variants({1, 2, 3, 4, 5}, {3, 4, 5, 1, 2});

    // The paper's running example has an edit distance of five.
    check_all_variants({1, 2, 3, 1, 2, 2, 1}, {3, 2, 1, 2, 1, 3});

    for (int iteration = 0; iteration < iterations; ++iteration) {
        int alphabet_size = 1 + (int)(generator() % 4);
        int length_a = (int)(generator() % 13);
        int length_b = (int)(generator() % 13);

        std::vector<int> sequence_a(length_a);
        std::vector<int> sequence_b(length_b);
        for (int& value : sequence_a) value = (int)(generator() % alphabet_size);
        for (int& value : sequence_b) value = (int)(generator() % alphabet_size);

        check_all_variants(sequence_a, sequence_b);
    }

    for (int iteration = 0; iteration < 50; ++iteration) {
        int alphabet_size = 2 + (int)(generator() % 6);
        int length_a = 50 + (int)(generator() % 150);
        int length_b = 50 + (int)(generator() % 150);

        std::vector<int> sequence_a(length_a);
        std::vector<int> sequence_b(length_b);
        for (int& value : sequence_a) value = (int)(generator() % alphabet_size);
        for (int& value : sequence_b) value = (int)(generator() % alphabet_size);

        check_all_variants(sequence_a, sequence_b);
    }

    if (failures == 0) {
        std::printf("test_myers: %d random cases plus edge cases passed for every variant\n",
                    iterations + 50);
        return 0;
    }

    std::printf("test_myers: %d failures\n", failures);
    return 1;
}
