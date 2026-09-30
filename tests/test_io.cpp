// test_io.cpp -- scan_stream() (chunked I/O, line tracking, output formatting)
// against a naive reference.
//
// The reference splits the text into lines up front and matches each line
// naively, so it has no chunks, no carried-over partial line and no lazy line
// finding -- none of the machinery under test. scan_stream() is then run on the
// same bytes at chunk sizes from 1 byte upwards, and its output must be
// byte-identical at every size. At chunk size 1 every line of length >= 2
// straddles a chunk boundary, which is the case the carry logic exists for.
//
// Covers: -n, -c, -o, --color, -i, filename prefixes, CRLF text, a final line
// with no trailing newline, empty lines and an empty file.
//
// Usage: test_io [num_cases] [first_seed]

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "aho_corasick.hpp"
#include "cli.hpp"
#include "io.hpp"

using namespace ahogrep;

namespace {

const char* kIn  = "test_io_input.tmp";
const char* kOut = "test_io_output.tmp";

unsigned char fold_ref(unsigned char c, bool ci) {
    return (ci && c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

struct RefMatch {
    std::size_t start, end;
    int         id;
};

// What ahogrep should print, derived the slow and obvious way.
std::string reference(const std::vector<std::string>& pats, const std::string& text,
                      const Options& o, const char* name, std::uint64_t* matched_lines) {
    std::vector<std::string> lines;
    std::size_t              s = 0;
    while (s < text.size()) {
        std::size_t nl = text.find('\n', s);
        if (nl == std::string::npos) nl = text.size();
        lines.push_back(text.substr(s, nl - s));
        s = nl + 1;
    }

    std::string   out;
    std::uint64_t count = 0;
    for (std::size_t ln = 0; ln < lines.size(); ++ln) {
        const std::string& line = lines[ln];
        std::vector<RefMatch> ms;
        for (std::size_t p = 0; p < pats.size(); ++p) {
            for (std::size_t st = 0; st + pats[p].size() <= line.size(); ++st) {
                bool eq = true;
                for (std::size_t k = 0; k < pats[p].size() && eq; ++k) {
                    eq = fold_ref(static_cast<unsigned char>(line[st + k]), o.case_insensitive) ==
                         fold_ref(static_cast<unsigned char>(pats[p][k]), o.case_insensitive);
                }
                if (eq) ms.push_back(RefMatch{st, st + pats[p].size(), static_cast<int>(p)});
            }
        }
        if (ms.empty()) continue;
        ++count;
        if (o.count_only) continue;

        std::string prefix;
        if (name) prefix += std::string(name) + ":";
        if (o.line_numbers) prefix += std::to_string(ln + 1) + ":";

        if (o.only_matching) {
            // Automaton report order: by end, longest first, then by id.
            std::sort(ms.begin(), ms.end(), [](const RefMatch& a, const RefMatch& b) {
                if (a.end != b.end) return a.end < b.end;
                if (a.start != b.start) return a.start < b.start;
                return a.id < b.id;
            });
            for (const RefMatch& m : ms) {
                out += prefix;
                if (o.color) out += std::string(kColorMatch);
                out += line.substr(m.start, m.end - m.start);
                if (o.color) out += std::string(kColorReset);
                out += '\n';
            }
        } else if (o.color) {
            // Mark covered bytes, then emit maximal runs. Two matches that merely
            // touch ([0,2) and [2,4)) form one run, as in the implementation.
            std::vector<bool> lit(line.size(), false);
            for (const RefMatch& m : ms) {
                for (std::size_t k = m.start; k < m.end; ++k) lit[k] = true;
            }
            out += prefix;
            bool in_run = false;
            for (std::size_t k = 0; k < line.size(); ++k) {
                if (lit[k] && !in_run) { out += std::string(kColorMatch); in_run = true; }
                if (!lit[k] && in_run) { out += std::string(kColorReset); in_run = false; }
                out += line[k];
            }
            if (in_run) out += std::string(kColorReset);
            out += '\n';
        } else {
            out += prefix + line + '\n';
        }
    }
    if (o.count_only) {
        if (name) out += std::string(name) + ":";
        out += std::to_string(count) + '\n';
    }
    *matched_lines = count;
    return out;
}

// Runs scan_stream over `in` (rewound first) with the given chunk size and
// returns what it printed. Output is appended to `out` and read back from where
// this run started: opening files is slow on Windows, so each check opens its
// two scratch files once and reuses them for every chunk size.
std::string run_scan(std::FILE* in, std::FILE* out, const AhoCorasick& ac, const Options& o,
                     const char* name, std::size_t chunk, std::uint64_t* matched_lines) {
    std::rewind(in);
    std::fseek(out, 0, SEEK_END);
    const long from = std::ftell(out);
    {
        OutputBuffer ob(out, 64);  // tiny, so the buffer's own flush path runs too
        const ScanResult r = scan_stream(in, name, ac, o, ob, chunk);
        *matched_lines = r.matching_lines;
    }  // ~OutputBuffer flushes
    const long to = std::ftell(out);

    std::string s(static_cast<std::size_t>(to - from), '\0');
    std::fseek(out, from, SEEK_SET);  // a seek is required between write and read
    if (!s.empty() && std::fread(&s[0], 1, s.size(), out) != s.size()) {
        std::printf("cannot read back scratch output\n");
        std::exit(2);
    }
    return s;
}

std::string visible(const std::string& s) {
    std::string v;
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\n') v += "\\n\n    ";
        else if (c == '\r') v += "\\r";
        else if (c == '\x1b') v += "<ESC>";
        else if (u < 0x20 || u >= 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", u); v += b; }
        else v += c;
    }
    return v;
}

// One case at every chunk size. Returns false (after printing) on a mismatch.
bool check(const char* label, const std::vector<std::string>& pats, const std::string& text,
           const Options& o, const char* name) {
    AhoCorasick ac(o.case_insensitive);
    for (const std::string& p : pats) ac.add_pattern(p);
    ac.build();

    std::uint64_t     want_lines = 0;
    const std::string want       = reference(pats, text, o, name, &want_lines);
    std::FILE* in  = std::fopen(kIn, "wb+");
    std::FILE* out = std::fopen(kOut, "wb+");
    if (!in || !out || std::fwrite(text.data(), 1, text.size(), in) != text.size()) {
        std::printf("cannot create scratch files\n");
        std::exit(2);
    }
    std::fflush(in);

    bool              ok       = true;
    const std::size_t chunks[] = {1, 2, 3, 5, 7, 16, 64, kChunkSize};
    for (const std::size_t chunk : chunks) {
        std::uint64_t     got_lines = 0;
        const std::string got       = run_scan(in, out, ac, o, name, chunk, &got_lines);
        if (got != want || got_lines != want_lines) {
            std::printf("\nFAIL  %s (chunk size %zu)\n", label, chunk);
            std::printf("  options: -n=%d -c=%d -o=%d --color=%d -i=%d name=%s\n", o.line_numbers,
                        o.count_only, o.only_matching, o.color, o.case_insensitive,
                        name ? name : "(none)");
            std::printf("  patterns:");
            for (const std::string& p : pats) std::printf(" \"%s\"", visible(p).c_str());
            std::printf("\n  text:\n    %s\n", visible(text).c_str());
            std::printf("  expected (%llu lines):\n    %s\n", static_cast<unsigned long long>(want_lines),
                        visible(want).c_str());
            std::printf("  got (%llu lines):\n    %s\n", static_cast<unsigned long long>(got_lines),
                        visible(got).c_str());
            ok = false;
            break;
        }
    }
    std::fclose(in);
    std::fclose(out);
    return ok;
}

Options opts(bool n, bool c, bool o, bool color, bool i) {
    Options x;
    x.line_numbers     = n;
    x.count_only       = c;
    x.only_matching    = o;
    x.color            = color;
    x.case_insensitive = i;
    return x;
}

}  // namespace

int main(int argc, char** argv) {
    // Each case costs 16 scans plus scratch-file I/O, which is slow on Windows;
    // 600 keeps ctest to a few seconds. Pass a larger count when hunting.
    unsigned num_cases  = 600;
    unsigned first_seed = 1;
    if (argc > 1) num_cases = static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10));
    if (argc > 2) first_seed = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));

    int failures = 0;

    // ---- named cases first: each one is a situation the line tracker must get right.
    struct Named {
        const char*              label;
        std::vector<std::string> pats;
        std::string              text;
    };
    const std::vector<Named> named = {
        {"final line without trailing newline", {"end"}, "first\nthe end"},
        {"only line, no newline at all", {"b"}, "abc"},
        {"empty file", {"a"}, ""},
        {"file of empty lines", {"a"}, "\n\n\n"},
        {"match on the very first and very last byte", {"x"}, "x\n\nabc\nx"},
        {"CRLF text: \\r kept in printed line", {"lo"}, "hello\r\nworld\r\nlow\r\n"},
        {"several matched lines back to back", {"a"}, "a\na\nba\nab\n"},
        {"overlapping matches in one line", {"he", "she", "hers"}, "ushers\nno\nshe\n"},
        {"touching matches merge in colour", {"ab", "cd"}, "abcd\n"},
        {"long line spanning many small chunks", {"needle"},
         std::string(100, 'x') + "needle" + std::string(100, 'y') + "\nshort needle\n"},
        {"matched line followed by a long unmatched tail", {"q"},
         "q\n" + std::string(300, 'z') + "\n" + std::string(50, 'z')},
    };
    const Options variants[] = {
        opts(false, false, false, false, false), opts(true, false, false, false, false),
        opts(false, true, false, false, false),  opts(false, false, true, false, false),
        opts(false, false, false, true, false),  opts(true, false, true, true, false),
        opts(true, false, false, true, true),
    };
    int named_checks = 0;
    for (const Named& c : named) {
        for (const Options& o : variants) {
            for (const char* name : {static_cast<const char*>(nullptr), "f.txt"}) {
                ++named_checks;
                if (!check(c.label, c.pats, c.text, o, name)) ++failures;
            }
        }
    }

    // ---- random cases: tiny alphabet with \n and \r mixed in, so lines are short
    // and plentiful and matches cluster around line breaks.
    const std::string alphabet = "abAB\n\n\r";
    for (unsigned i = 0; i < num_cases && failures == 0; ++i) {
        const unsigned seed = first_seed + i;
        std::mt19937   rng(seed);
        auto           pick = [&](int lo, int hi) {
            return std::uniform_int_distribution<int>(lo, hi)(rng);
        };

        std::vector<std::string> pats(static_cast<std::size_t>(pick(1, 4)));
        for (std::string& p : pats) {
            const int len = pick(1, 3);
            for (int k = 0; k < len; ++k) p += "abAB"[pick(0, 3)];  // no \n in patterns
        }
        std::string text;
        const int   tlen = pick(0, 120);
        for (int k = 0; k < tlen; ++k) text += alphabet[static_cast<std::size_t>(pick(0, 6))];

        const Options o = opts(pick(0, 1) != 0, pick(0, 4) == 0, pick(0, 2) == 0, pick(0, 1) != 0,
                               pick(0, 1) != 0);
        const char* name = pick(0, 1) ? "in.txt" : nullptr;

        char label[64];
        std::snprintf(label, sizeof label, "random seed %u", seed);
        if (!check(label, pats, text, o, name)) {
            ++failures;
            std::printf("  rerun with: test_io 1 %u\n", seed);
        }
    }

    std::remove(kIn);
    std::remove(kOut);

    if (failures) {
        std::printf("test_io: FAILED\n");
        return 1;
    }
    std::printf("test_io: OK -- %d named checks and %u random cases, each at 8 chunk sizes\n",
                named_checks, num_cases);
    return 0;
}
