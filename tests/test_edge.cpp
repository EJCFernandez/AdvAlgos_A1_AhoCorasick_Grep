// test_edge.cpp -- hand-written edge cases.
//
// Scaffold status: not implemented yet (checklist item 2/5). Reports "skipped"
// to ctest via exit code 77 for now.
//
// TODO(claude): the list from CLAUDE.md, each as a named case with the expected
// match set spelled out literally so a failure says which case broke:
//   - a pattern that is a suffix of another: {"he", "she"} over "ushers"
//   - a pattern that is a prefix of another: {"ab", "abc"} over "xabcx"
//   - duplicate patterns: {"ab", "ab"} -- two ids must both fire
//   - single-character patterns, including one matching every byte of the text
//   - a pattern longer than the text
//   - an empty text (zero-length chunk must be a no-op, not a crash)
//   - a pattern set where the failure chain is several nodes long, e.g.
//     {"aaa", "aa", "a"} over "aaaa" -- 9 matches, and the count alone catches a
//     missing output-link walk
//   - high bytes 128-255, and a UTF-8 pattern matched as bytes
//   - -i cases: fold applied to patterns and input, and a check that bytes >=128
//     are NOT folded
//   - CRLF pattern files (belongs with the -f parser, once that exists)

#include <cstdio>

#include "aho_corasick.hpp"

int main() {
    ahogrep::AhoCorasick ac;
    ac.add_pattern("a");
    ac.build();
    std::printf("test_edge: not implemented yet (%zu nodes)\n", ac.node_count());
    return 77;  // ctest SKIP_RETURN_CODE
}
