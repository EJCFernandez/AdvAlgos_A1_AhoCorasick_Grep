// test_random.cpp -- randomised differential test of the automaton against a
// naive matcher.
//
// The idea: for a random pattern set and a random text, compute the match set
// twice -- once with the automaton, once with an obviously-correct O(n*m) matcher
// that shares no code with it -- and require the two to be identical. Whenever
// they differ, the automaton is wrong (the naive matcher is short enough to read
// and be sure of).
//
// Three things make this catch real bugs rather than just passing:
//
//   1. Tiny alphabets. Over {a,b}, patterns constantly turn out to be suffixes
//      of one another, so failure chains get deep and output links get exercised
//      on nearly every case. Over a 26-letter alphabet these collisions would be
//      rare and the tester would mostly check the easy path.
//   2. The FULL match set is compared, not the count. A missing output-link walk
//      removes specific (id, end) pairs; a count check can hide that behind a
//      compensating error, a set comparison cannot.
//   3. Every text is re-fed split at random chunk boundaries -- including
//      one-byte-at-a-time and including empty chunks -- and must give exactly the
//      same answer. This is the only thing that tests carrying automaton state
//      across a chunk boundary, which is what the streaming CLI depends on.
//
// On failure the case is shrunk to a minimal still-failing one and printed as
// pasteable C++ literals, so it can go straight into tests/test_edge.cpp as a
// permanent regression case.
//
// This tester was itself checked before being trusted, by running it against a
// known-correct automaton (10k cases, 2.8M matches, clean) and against ten
// deliberately broken ones: no output-link walk, output link computed before the
// failure link is final, depth-first instead of breadth-first, failure link
// followed once instead of in a loop, jump straight to the root on a mismatch,
// matches reported before the byte is consumed, a node's ids reported twice, an
// off-by-one end offset, state reset at each chunk start, and -i folding the
// input but not the patterns. All ten were caught. Worth noting for the report:
// "state reset at each chunk start" was caught ONLY by the chunk-split families,
// and a plain single-pass tester would have missed it entirely.
//
// Usage: test_random [num_cases] [first_seed]
//        10000 cases takes about 0.2s in a Release build; pass a larger number
//        when hunting something rare.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "aho_corasick.hpp"

using ahogrep::AhoCorasick;

namespace {

// ---------------------------------------------------------------- match set --

struct Match {
    int32_t       id;
    std::uint64_t end;  // one past the last byte, as search_chunk defines it
};

// Sorted by (end, id) to get a canonical order. Duplicate patterns legitimately
// produce two entries with the same end and different ids, and identical entries
// can never occur, so this is a total order on a valid match set.
bool match_less(const Match& a, const Match& b) {
    if (a.end != b.end) return a.end < b.end;
    return a.id < b.id;
}

bool match_equal(const Match& a, const Match& b) {
    return a.id == b.id && a.end == b.end;
}

// --------------------------------------------------------- naive reference --

// Mirrors AhoCorasick's ASCII-only fold rather than calling it: a differential
// test is only meaningful if the reference is independent of the thing under
// test. If this ever disagrees with the automaton's table, that disagreement is
// itself the finding.
unsigned char fold_byte(unsigned char c, bool ci) {
    if (!ci) return c;
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

// Every pattern against every start offset. Deliberately the dumbest correct
// thing: overlaps fall out for free, and there is nothing here to get subtly
// wrong.
std::vector<Match> naive_matches(const std::vector<std::string>& patterns,
                                const std::string& text, bool ci) {
    std::vector<Match> out;
    for (std::size_t p = 0; p < patterns.size(); ++p) {
        const std::string& pat = patterns[p];
        for (std::size_t s = 0; s + pat.size() <= text.size(); ++s) {
            bool equal = true;
            for (std::size_t k = 0; k < pat.size(); ++k) {
                if (fold_byte(static_cast<unsigned char>(text[s + k]), ci) !=
                    fold_byte(static_cast<unsigned char>(pat[k]), ci)) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                out.push_back(Match{static_cast<int32_t>(p),
                                    static_cast<std::uint64_t>(s + pat.size())});
            }
        }
    }
    std::sort(out.begin(), out.end(), match_less);
    return out;
}

// ------------------------------------------------------------- automaton run --

// Feeds `text` through the automaton in the given chunk sizes (which must sum to
// text.size()). State and base offset live outside the loop, exactly as the CLI
// will use them -- that is the property under test.
std::vector<Match> automaton_matches(const AhoCorasick& ac, const std::string& text,
                                     const std::vector<std::size_t>& chunk_sizes,
                                     std::uint64_t* reported_total) {
    std::vector<Match> out;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(text.data());

    int32_t       state = AhoCorasick::kRoot;
    std::uint64_t base  = 0;
    for (const std::size_t len : chunk_sizes) {
        state = ac.search_chunk(data + base, len, state, base,
                                [&out](int32_t id, std::uint64_t end) {
                                    out.push_back(Match{id, end});
                                });
        base += len;
    }

    if (reported_total != nullptr) *reported_total += out.size();
    std::sort(out.begin(), out.end(), match_less);
    return out;
}

// ------------------------------------------------------------- case plumbing --

struct Case {
    std::vector<std::string> patterns;
    std::string              text;
    bool                     ci     = false;
    const char*              family = "";  // which generator family produced it
};

struct Failure {
    const char*              what = "";  // which split exposed it
    std::vector<std::size_t> chunk_sizes;
    std::vector<Match>       expected;
    std::vector<Match>       actual;
    std::string              note;  // set for structural problems, not diffs
};

std::vector<std::size_t> one_chunk(std::size_t n) {
    return std::vector<std::size_t>{n};
}

std::vector<std::size_t> per_byte_chunks(std::size_t n) {
    return std::vector<std::size_t>(n, 1);
}

// Random cut points, with empty chunks allowed on purpose: search_chunk(_, 0, _)
// must be a no-op, and a real stream can hand us a zero-length read.
std::vector<std::size_t> random_chunks(std::size_t n, std::mt19937& rng) {
    std::uniform_int_distribution<int> cuts_dist(0, 6);
    const int                          cuts = cuts_dist(rng);

    std::vector<std::size_t> points;
    std::uniform_int_distribution<std::size_t> pos(0, n);
    for (int i = 0; i < cuts; ++i) points.push_back(pos(rng));
    std::sort(points.begin(), points.end());

    std::vector<std::size_t> sizes;
    std::size_t              prev = 0;
    for (const std::size_t p : points) {
        sizes.push_back(p - prev);
        prev = p;
    }
    sizes.push_back(n - prev);
    return sizes;
}

// Runs one case. Returns true on failure and fills `f`.
//
// Deterministic despite using an RNG for the splits: the RNG is seeded from the
// case itself, so shrinking cannot make a failure disappear by picking different
// chunk boundaries.
bool run_case(const Case& c, Failure* f, std::uint64_t* reported_total) {
    AhoCorasick ac(c.ci);
    for (const std::string& p : c.patterns) {
        if (ac.add_pattern(p) == AhoCorasick::kNone) {
            if (f != nullptr) f->note = "add_pattern rejected a non-empty pattern";
            return true;
        }
    }
    ac.build();

    // Cheap structural checks first: a wrong id-to-length mapping would show up
    // as a confusing diff further down.
    if (ac.pattern_count() != c.patterns.size()) {
        if (f != nullptr) f->note = "pattern_count() disagrees with the number inserted";
        return true;
    }
    for (std::size_t i = 0; i < c.patterns.size(); ++i) {
        if (ac.pattern_length(static_cast<int32_t>(i)) != c.patterns[i].size()) {
            if (f != nullptr) f->note = "pattern_length(id) disagrees with the pattern";
            return true;
        }
    }

    const std::vector<Match> expected = naive_matches(c.patterns, c.text, c.ci);

    // Splits worth trying, cheapest and most diagnostic first. A one-shot failure
    // is a plain matching bug; a failure that only appears once the text is split
    // is a state-carrying bug, and knowing which it is saves a lot of time.
    std::mt19937             rng(static_cast<std::uint32_t>(c.text.size() * 2654435761u +
                                                c.patterns.size() * 40503u + (c.ci ? 7u : 0u)));
    struct Split {
        const char*              what;
        std::vector<std::size_t> sizes;
    };
    std::vector<Split> splits;
    splits.push_back(Split{"single chunk", one_chunk(c.text.size())});
    splits.push_back(Split{"one byte per chunk", per_byte_chunks(c.text.size())});
    for (int i = 0; i < 3; ++i) {
        splits.push_back(Split{"random chunk split", random_chunks(c.text.size(), rng)});
    }

    for (const Split& split : splits) {
        const std::vector<Match> actual =
            automaton_matches(ac, c.text, split.sizes, reported_total);
        if (actual.size() != expected.size() ||
            !std::equal(actual.begin(), actual.end(), expected.begin(), match_equal)) {
            if (f != nullptr) {
                f->what        = split.what;
                f->chunk_sizes = split.sizes;
                f->expected    = expected;
                f->actual      = actual;
            }
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------- generation ----

struct Family {
    const char* alphabet;
    bool        ci;
    const char* description;
};

// All tiny, all chosen to make collisions and folding decisions frequent.
const Family kFamilies[] = {
    {"ab",           false, "binary alphabet"},
    {"abc",          false, "ternary alphabet"},
    {"aAbB",         true,  "mixed case, -i on"},
    {"aAbB",         false, "mixed case, -i off"},
    {"\xc3\xa9x",    false, "high bytes (signed char trap)"},
    {"\xc3\xa9" "A", true,  "high bytes with -i: 0x80-0xff must not fold"},
};

Case random_case(std::mt19937& rng) {
    std::uniform_int_distribution<int> family_dist(
        0, static_cast<int>(sizeof(kFamilies) / sizeof(kFamilies[0])) - 1);
    const Family& fam = kFamilies[family_dist(rng)];

    const std::string alphabet = fam.alphabet;
    std::uniform_int_distribution<std::size_t> letter(0, alphabet.size() - 1);
    std::uniform_int_distribution<int>         count_dist(1, 8);
    std::uniform_int_distribution<int>         len_dist(1, 5);
    std::uniform_int_distribution<int>         text_dist(0, 200);
    std::uniform_int_distribution<int>         percent(0, 99);

    Case c;
    c.ci                = fam.ci;
    c.family            = fam.description;
    const int n_patterns = count_dist(rng);
    for (int i = 0; i < n_patterns; ++i) {
        // Duplicates must each keep their own id, so feed them in deliberately
        // instead of waiting for a collision to happen by chance.
        if (i > 0 && percent(rng) < 15) {
            std::uniform_int_distribution<std::size_t> pick(0, c.patterns.size() - 1);
            c.patterns.push_back(c.patterns[pick(rng)]);
            continue;
        }
        std::string p;
        const int   len = len_dist(rng);
        for (int k = 0; k < len; ++k) p += alphabet[letter(rng)];
        c.patterns.push_back(p);
    }

    const int text_len = text_dist(rng);
    c.text.reserve(static_cast<std::size_t>(text_len));
    for (int i = 0; i < text_len; ++i) c.text += alphabet[letter(rng)];
    return c;
}

// ------------------------------------------------------------- shrinking -----

// Greedy: keep trying to make the case smaller, keeping any change that still
// fails. Not minimal in theory, but in practice it turns a 200-byte text and 8
// patterns into something like {"aa", "a"} over "aaa", which is small enough to
// trace by hand.
Case shrink(const Case& start, std::uint64_t* reported_total) {
    Case       best  = start;
    const int  kMaxAttempts = 4000;
    int        attempts = 0;
    bool       improved = true;

    while (improved && attempts < kMaxAttempts) {
        improved = false;

        // Drop a pattern.
        for (std::size_t i = 0; i < best.patterns.size() && best.patterns.size() > 1; ++i) {
            Case t = best;
            t.patterns.erase(t.patterns.begin() + static_cast<std::ptrdiff_t>(i));
            if (++attempts > kMaxAttempts) break;
            if (run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                break;
            }
        }

        // Cut the text in half, then trim single bytes from each end.
        if (best.text.size() > 1) {
            Case t = best;
            t.text = best.text.substr(0, best.text.size() / 2);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                continue;
            }
            t.text = best.text.substr(best.text.size() / 2);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                continue;
            }
        }
        if (!best.text.empty()) {
            Case t = best;
            t.text = best.text.substr(1);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                continue;
            }
            t.text = best.text.substr(0, best.text.size() - 1);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                continue;
            }
        }

        // Shorten a pattern from either end.
        for (std::size_t i = 0; i < best.patterns.size(); ++i) {
            if (best.patterns[i].size() <= 1) continue;
            Case t = best;
            t.patterns[i] = best.patterns[i].substr(1);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                break;
            }
            t.patterns[i] = best.patterns[i].substr(0, best.patterns[i].size() - 1);
            if (++attempts <= kMaxAttempts && run_case(t, nullptr, reported_total)) {
                best     = t;
                improved = true;
                break;
            }
        }
    }
    return best;
}

// ------------------------------------------------------------- reporting -----

// Escapes to a pasteable C++ string literal body. A hex escape swallows any
// following hex digit in C++, so the literal is split ("\xc3" "a") when that
// would happen -- otherwise a printed failing case would not compile back to the
// bytes it came from.
std::string escape(const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    std::string       out;
    bool              prev_was_hex_escape = false;
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool is_hex_digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                  (c >= 'A' && c <= 'F');
        if (prev_was_hex_escape && is_hex_digit) out += "\" \"";
        prev_was_hex_escape = false;

        if (c == '\\' || c == '"') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 0x20 && c < 0x7f) {
            out += static_cast<char>(c);
        } else {
            out += "\\x";
            out += kHex[c >> 4];
            out += kHex[c & 0x0f];
            prev_was_hex_escape = true;
        }
    }
    return out;
}

void print_matches(const char* label, const std::vector<Match>& m) {
    const std::size_t kMaxShown = 40;
    std::printf("  %-9s %zu match%s:", label, m.size(), m.size() == 1 ? "" : "es");
    for (std::size_t i = 0; i < m.size() && i < kMaxShown; ++i) {
        std::printf(" (id %d, end %llu)", m[i].id,
                    static_cast<unsigned long long>(m[i].end));
    }
    if (m.size() > kMaxShown) std::printf(" ... (%zu more)", m.size() - kMaxShown);
    std::printf("\n");
}

void report(unsigned seed, const Case& c, const Failure& f) {
    std::printf("\nFAILED on seed %u\n", seed);

    if (!f.note.empty()) {
        std::printf("  %s\n", f.note.c_str());
    } else {
        std::printf("  exposed by: %s", f.what);
        if (f.chunk_sizes.size() > 1) {
            std::printf(" (");
            for (std::size_t i = 0; i < f.chunk_sizes.size() && i < 12; ++i) {
                std::printf("%s%zu", i ? "+" : "", f.chunk_sizes[i]);
            }
            if (f.chunk_sizes.size() > 12) std::printf("+...");
            std::printf(" bytes)");
        }
        std::printf("\n");
        print_matches("expected", f.expected);
        print_matches("got", f.actual);

        // First difference, which is usually enough to name the bug.
        const std::size_t n = std::min(f.expected.size(), f.actual.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (!match_equal(f.expected[i], f.actual[i])) {
                std::printf("  first difference at index %zu: expected (id %d, end %llu), "
                            "got (id %d, end %llu)\n",
                            i, f.expected[i].id,
                            static_cast<unsigned long long>(f.expected[i].end),
                            f.actual[i].id,
                            static_cast<unsigned long long>(f.actual[i].end));
                break;
            }
        }
        if (f.actual.size() < f.expected.size() && n == f.actual.size()) {
            std::printf("  the automaton stopped short: it missed %zu match%s the naive "
                        "matcher found\n",
                        f.expected.size() - f.actual.size(),
                        f.expected.size() - f.actual.size() == 1 ? "" : "es");
        }
    }

    std::printf("\n  Minimal failing case, pasteable into tests/test_edge.cpp:\n\n");
    std::printf("    // shrunk from seed %u\n", seed);
    std::printf("    const bool case_insensitive = %s;\n", c.ci ? "true" : "false");
    std::printf("    const char* patterns[] = {");
    for (std::size_t i = 0; i < c.patterns.size(); ++i) {
        std::printf("%s\"%s\"", i ? ", " : " ", escape(c.patterns[i]).c_str());
    }
    std::printf(" };\n");
    std::printf("    const char* text = \"%s\";\n\n", escape(c.text).c_str());
}

}  // namespace

int main(int argc, char** argv) {
    // Defaults chosen so the whole run stays under a second or two in a Release
    // build; raise it when hunting something rare.
    unsigned num_cases = 10000;
    unsigned first_seed = 1;
    if (argc > 1) num_cases = static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10));
    if (argc > 2) first_seed = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    if (num_cases == 0) num_cases = 1;

    std::printf("test_random: %u cases, seeds %u..%u\n", num_cases, first_seed,
                first_seed + num_cases - 1);

    std::uint64_t reported_total = 0;  // matches the automaton reported, in total

    for (unsigned i = 0; i < num_cases; ++i) {
        const unsigned seed = first_seed + i;
        // One RNG per case, seeded by the case number, so any failure is
        // reproducible from its seed alone: test_random 1 <seed>.
        std::mt19937 rng(seed);
        const Case   c = random_case(rng);

        Failure f;
        if (run_case(c, &f, &reported_total)) {
            const Case small = shrink(c, &reported_total);
            report(seed, small, f);

            // Re-derive the failure on the shrunk case so the printed diff
            // belongs to the printed case rather than to the original.
            Failure sf;
            if (run_case(small, &sf, &reported_total) && sf.note.empty()) {
                std::printf("  On that minimal case:\n");
                print_matches("expected", sf.expected);
                print_matches("got", sf.actual);
                std::printf("  (exposed by: %s)\n", sf.what);
            }

            if (reported_total == 0) {
                std::printf("\n  The automaton reported zero matches across every case so "
                            "far.\n  That is what an unwritten build_failure_links() / "
                            "search_chunk() looks like --\n  expected at this stage, not a "
                            "bug in the tester.\n");
            }
            std::printf("\nRerun just this case with: test_random 1 %u\n", seed);
            return 1;
        }
    }

    std::printf("test_random: OK -- %u cases passed, %llu matches compared\n", num_cases,
                static_cast<unsigned long long>(reported_total));
    return 0;
}
