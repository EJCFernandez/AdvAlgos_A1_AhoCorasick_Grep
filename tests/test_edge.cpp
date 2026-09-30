// test_edge.cpp -- hand-written edge cases, the list from CLAUDE.md.
//
// Each case spells its expected match set out literally, so a failure names the
// case that broke and shows both sets. The random tester already covers the
// general case; these exist to pin down specific situations by name, and to be
// small enough to trace by hand.
//
// Every match-set case is run three ways: in one chunk, one byte per chunk, and
// split in two at EVERY possible cut point. The texts are short, so trying all
// cuts is cheap, and it guarantees each pattern occurrence gets split across a
// chunk boundary at each of its internal positions at least once.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "aho_corasick.hpp"
#include "cli.hpp"

using ahogrep::AhoCorasick;

namespace {

int g_failures = 0;
int g_checks   = 0;

struct M {
    int32_t       id;
    std::uint64_t end;  // one past the last byte
    bool operator==(const M& o) const { return id == o.id && end == o.end; }
    bool operator<(const M& o) const { return end != o.end ? end < o.end : id < o.id; }
};

std::string show(const std::vector<M>& ms) {
    std::string s = "{";
    for (std::size_t i = 0; i < ms.size(); ++i) {
        s += (i ? ", (" : "(") + std::to_string(ms[i].id) + "," + std::to_string(ms[i].end) + ")";
    }
    return s + "}";
}

void expect_true(const char* name, bool cond, const std::string& detail = "") {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : ": ", detail.c_str());
    }
}

AhoCorasick build(const std::vector<std::string>& patterns, bool ci) {
    AhoCorasick ac(ci);
    for (const std::string& p : patterns) ac.add_pattern(p);
    ac.build();
    return ac;
}

// Feeds `text` in the given chunk sizes and returns the matches in REPORT order
// (unsorted), so callers can check ordering too.
std::vector<M> scan(const AhoCorasick& ac, const std::string& text,
                    const std::vector<std::size_t>& chunks) {
    std::vector<M>       out;
    const unsigned char* data  = reinterpret_cast<const unsigned char*>(text.data());
    int32_t              state = AhoCorasick::kRoot;
    std::uint64_t        base  = 0;
    for (const std::size_t n : chunks) {
        state = ac.search_chunk(data + base, n, state, base,
                                [&](int32_t id, std::uint64_t end) { out.push_back(M{id, end}); });
        base += n;
    }
    return out;
}

// The main check: `expected` in any order; compared as sorted sets.
void expect_matches(const char* name, const std::vector<std::string>& patterns,
                    const std::string& text, bool ci, std::vector<M> expected) {
    const AhoCorasick ac = build(patterns, ci);
    std::sort(expected.begin(), expected.end());

    std::vector<std::vector<std::size_t>> splits;
    splits.push_back({text.size()});
    splits.push_back(std::vector<std::size_t>(text.size(), 1));
    for (std::size_t cut = 0; cut <= text.size(); ++cut) {
        splits.push_back({cut, text.size() - cut});
    }

    for (const auto& split : splits) {
        std::vector<M> got = scan(ac, text, split);

        // The CLI's line tracker relies on matches arriving in non-decreasing
        // end order, so that is checked here as part of every case.
        const bool ordered = std::is_sorted(got.begin(), got.end(),
                                            [](const M& a, const M& b) { return a.end < b.end; });
        std::sort(got.begin(), got.end());
        std::string where = "split";
        for (const std::size_t n : split) where += " " + std::to_string(n);

        expect_true(name, got == expected,
                    "expected " + show(expected) + ", got " + show(got) + " (" + where + ")");
        expect_true(name, ordered, "matches not reported in end order (" + where + ")");
        if (got != expected || !ordered) return;  // one report per case is enough
    }
}

// Walks the trie along `s`; kNone if it is not a node.
int32_t node_of(const AhoCorasick& ac, const std::string& s) {
    int32_t v = AhoCorasick::kRoot;
    for (const char ch : s) {
        v = ac.child(v, ac.fold(static_cast<unsigned char>(ch)));
        if (v == AhoCorasick::kNone) return v;
    }
    return v;
}

// ---------------------------------------------------------------------- cases --

void suffix_of_another() {
    // "he" ends inside "she": found only via the output link of node "she".
    expect_matches("suffix: {he, she} over ushers", {"he", "she"}, "ushers", false,
                   {{1, 4}, {0, 4}});
}

void prefix_of_another() {
    // "ab" is a terminal node on the way to "abc": both must fire.
    expect_matches("prefix: {ab, abc} over xabcx", {"ab", "abc"}, "xabcx", false,
                   {{0, 3}, {1, 4}});
    // Prefix pattern present but the longer one never completes.
    expect_matches("prefix: {ab, abc} over xabx", {"ab", "abc"}, "xabx", false, {{0, 3}});
}

void duplicates() {
    // Two ids on one node; both fire at every occurrence (settled decision).
    expect_matches("duplicates: {ab, ab} over abab", {"ab", "ab"}, "abab", false,
                   {{0, 2}, {1, 2}, {0, 4}, {1, 4}});
    // A duplicate reached only through an output link, not as the current state.
    expect_matches("duplicates via output link: {xab, ab, ab} over xab",
                   {"xab", "ab", "ab"}, "xab", false, {{0, 3}, {1, 3}, {2, 3}});
}

void single_characters() {
    expect_matches("single char matching every byte: {a} over aaa", {"a"}, "aaa", false,
                   {{0, 1}, {0, 2}, {0, 3}});
    expect_matches("single chars: {x, y} over xyz", {"x", "y"}, "xyz", false,
                   {{0, 1}, {1, 2}});
}

void longer_than_text() {
    expect_matches("pattern longer than text (text is its prefix)", {"abcdef"}, "abc",
                   false, {});
    expect_matches("pattern longer than text, plus a short one", {"abcdef", "c"}, "abc",
                   false, {{1, 3}});
}

void empty_text() {
    expect_matches("empty text", {"a", "ab"}, "", false, {});

    // A zero-length chunk must hand the state back unchanged, mid-pattern too:
    // a real stream can deliver an empty read.
    const AhoCorasick ac = build({"abc"}, false);
    const unsigned char ab[] = {'a', 'b'};
    const int32_t mid   = ac.search_chunk(ab, 2, AhoCorasick::kRoot, 0, [](int32_t, std::uint64_t) {});
    int           calls = 0;
    const int32_t after = ac.search_chunk(ab, 0, mid, 2, [&](int32_t, std::uint64_t) { ++calls; });
    expect_true("empty chunk preserves state", after == mid && calls == 0 && mid != AhoCorasick::kRoot);
}

void deep_failure_chain() {
    // Every node on the chain aaa -> aa -> a is terminal: 4 + 3 + 2 = 9 matches.
    // The count alone catches a scan that reports only the current node's ids.
    expect_matches("deep chain: {aaa, aa, a} over aaaa", {"aaa", "aa", "a"}, "aaaa", false,
                   {{2, 1}, {1, 2}, {2, 2}, {0, 3}, {1, 3}, {2, 3}, {0, 4}, {1, 4}, {2, 4}});
}

void textbook_example() {
    // {he, she, his, hers} over "ushers": she and he end at 4, hers at 6.
    expect_matches("textbook: {he, she, his, hers} over ushers", {"he", "she", "his", "hers"},
                   "ushers", false, {{1, 4}, {0, 4}, {3, 6}});

    // The links themselves, checked against the hand-computed table. This is
    // the table --dump-automaton prints, so a mismatch here means the video
    // would show a wrong link.
    const AhoCorasick ac = build({"he", "she", "his", "hers"}, false);
    struct Link {
        const char* node;
        const char* fail;
        const char* output;  // nullptr = no output link
    };
    const Link links[] = {
        {"h", "", nullptr},     {"s", "", nullptr},     {"he", "", nullptr},
        {"hi", "", nullptr},    {"sh", "h", nullptr},   {"her", "", nullptr},
        {"his", "s", nullptr},  {"she", "he", "he"},    {"hers", "s", nullptr},
    };
    expect_true("textbook: node count", ac.node_count() == 10,
                "got " + std::to_string(ac.node_count()));
    for (const Link& l : links) {
        const int32_t v = node_of(ac, l.node);
        const std::string name = std::string("textbook links of \"") + l.node + "\"";
        if (v == AhoCorasick::kNone) {
            expect_true(name.c_str(), false, "node missing from trie");
            continue;
        }
        expect_true(name.c_str(), ac.fail_link(v) == node_of(ac, l.fail), "wrong fail link");
        const int32_t want_out = l.output ? node_of(ac, l.output) : AhoCorasick::kNone;
        expect_true(name.c_str(), ac.output_link(v) == want_out, "wrong output link");
        expect_true(name.c_str(), ac.depth(v) == static_cast<int32_t>(std::string(l.node).size()),
                    "wrong depth");
    }
}

void high_bytes() {
    // 0x80-0xff: if a byte ever goes through a signed char into an index, these
    // go negative. 0xff and 0x80 are the two ends of that trap.
    expect_matches("high bytes: {\\xff\\x80} ", {"\xff\x80"}, std::string("\x00\xff\x80\xff\x80", 5),
                   false, {{0, 3}, {0, 5}});
    // UTF-8 "é" (c3 a9) inside "café", matched as a byte string.
    expect_matches("UTF-8 pattern as bytes: é in café", {"\xc3\xa9"}, "caf\xc3\xa9", false,
                   {{0, 5}});
    // Byte semantics: a lone continuation byte matches inside a character. This
    // is the documented behaviour, not a bug.
    expect_matches("UTF-8: lone continuation byte matches mid-character", {"\xa9"},
                   "caf\xc3\xa9", false, {{0, 5}});
    // A NUL byte is an ordinary symbol.
    expect_matches("NUL byte in pattern", {std::string("a\0b", 3)}, std::string("xa\0b", 4),
                   false, {{0, 4}});
}

void case_insensitive() {
    expect_matches("-i: pattern and input both folded", {"HeLLo"}, "say hello HELLO hElLo",
                   true, {{0, 9}, {0, 15}, {0, 21}});
    expect_matches("no -i: case matters", {"Hello"}, "hello Hello", false, {{0, 11}});
    // É is c3 89, é is c3 a9: ASCII-only folding must leave them different.
    expect_matches("-i: bytes >= 128 are not folded (É vs é)", {"\xc3\xa9"}, "\xc3\x89", true, {});
    // Non-letters next to letters in ASCII must not fold: '@' (0x40) and '['
    // (0x5b) sit just outside 'A'..'Z'.
    expect_matches("-i: @ and [ are not letters", {"`", "{"}, "@[", true, {});

    const AhoCorasick ac = build({"x"}, true);
    bool high_identity = true;
    for (int c = 128; c < 256; ++c) {
        high_identity = high_identity && ac.fold(static_cast<unsigned char>(c)) == c;
    }
    expect_true("-i: fold() is the identity on 128..255", high_identity);
    expect_true("-i: fold('Z') == 'z'", ac.fold('Z') == 'z');
}

void no_patterns_and_empty_pattern() {
    // -f of an empty file gives zero patterns: a bare root, matching nothing.
    expect_matches("zero patterns", {}, "anything", false, {});

    AhoCorasick ac;
    expect_true("empty pattern rejected", ac.add_pattern("") == AhoCorasick::kNone);
    expect_true("rejected pattern gets no id", ac.pattern_count() == 0);
    expect_true("rejected pattern leaves root non-terminal", !ac.is_terminal(AhoCorasick::kRoot));
}

// ------------------------------------------------------- pattern-file parsing --

// Writes `bytes` to a scratch file in the working directory (ctest runs in the
// build tree) and parses `-f <file>` through the real parse_args().
ahogrep::ParseStatus parse_pattern_file(const std::string& bytes, ahogrep::Options& opts,
                                        std::string& error) {
    const char* path = "test_edge_patterns.tmp";
    std::FILE*  f    = std::fopen(path, "wb");  // binary: keep the \r\n exactly
    if (f == nullptr) {
        error = "could not create scratch file";
        return ahogrep::ParseStatus::Error;
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);

    char        prog[] = "ahogrep";
    char        flag[] = "-f";
    std::string p      = path;
    char*       argv[] = {prog, flag, &p[0]};
    const ahogrep::ParseStatus st = ahogrep::parse_args(3, argv, opts, error);
    std::remove(path);
    return st;
}

void crlf_pattern_file() {
    using ahogrep::Options;
    using ahogrep::ParseStatus;
    const std::vector<std::string> want = {"he", "she", "hers"};

    {
        Options     o;
        std::string err;
        const ParseStatus st = parse_pattern_file("he\r\nshe\r\nhers\r\n", o, err);
        expect_true("CRLF pattern file: \\r stripped", st == ParseStatus::Ok && o.patterns == want, err);
    }
    {
        Options     o;
        std::string err;
        const ParseStatus st = parse_pattern_file("he\nshe\r\nhers", o, err);
        expect_true("mixed endings, no final newline", st == ParseStatus::Ok && o.patterns == want, err);
    }
    {
        // Only ONE trailing \r is a line ending; a second one is pattern data.
        Options     o;
        std::string err;
        const ParseStatus st = parse_pattern_file("a\r\r\n", o, err);
        expect_true("only one \\r stripped", st == ParseStatus::Ok && o.patterns.size() == 1 &&
                                                 o.patterns[0] == "a\r", err);
    }
    {
        Options     o;
        std::string err;
        const ParseStatus st = parse_pattern_file("", o, err);
        expect_true("empty pattern file = zero patterns", st == ParseStatus::Ok && o.patterns.empty(), err);
    }
    {
        // A blank line (CRLF or not) is an empty pattern: rejected, exit 2.
        Options     o;
        std::string err;
        const ParseStatus st = parse_pattern_file("he\r\n\r\nshe\r\n", o, err);
        expect_true("blank CRLF line rejected", st == ParseStatus::Error &&
                                                   err.find("line 2") != std::string::npos,
                    "status/message: " + err);
    }
    {
        // End to end: the patterns parsed from a CRLF file match LF text.
        Options     o;
        std::string err;
        parse_pattern_file("he\r\nshe\r\n", o, err);
        expect_matches("CRLF file patterns match LF text", o.patterns, "ushers\n", false,
                       {{1, 4}, {0, 4}});
    }
}

}  // namespace

int main() {
    suffix_of_another();
    prefix_of_another();
    duplicates();
    single_characters();
    longer_than_text();
    empty_text();
    deep_failure_chain();
    textbook_example();
    high_bytes();
    case_insensitive();
    no_patterns_and_empty_pattern();
    crlf_pattern_file();

    if (g_failures != 0) {
        std::printf("test_edge: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_edge: OK -- %d checks passed\n", g_checks);
    return 0;
}
