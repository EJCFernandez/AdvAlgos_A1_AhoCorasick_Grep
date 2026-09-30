// aho_corasick.hpp -- multi-pattern byte matching automaton.
//
// This header deliberately knows nothing about files, the CLI or terminals: it
// takes bytes in and reports matches through a callback. That is what lets the
// differential tester drive the automaton straight from in-memory buffers, and
// it keeps the surface that has to be explained on video small.
//
// Typical use:
//
//     AhoCorasick ac(/*case_insensitive=*/false);
//     ac.add_pattern("she");
//     ac.add_pattern("he");
//     ac.build();
//
//     int32_t  state = AhoCorasick::kRoot;   // NOTE: lives outside the loop
//     uint64_t base  = 0;
//     while ((n = read_chunk(buf)) > 0) {
//         state = ac.search_chunk(buf, n, state, base,
//                                 [&](int32_t id, uint64_t end) { ... });
//         base += n;
//     }
//
// Holding `state` and `base` outside the chunk loop is the whole reason a
// pattern straddling a chunk boundary is still found.

#ifndef AHOGREP_AHO_CORASICK_HPP
#define AHOGREP_AHO_CORASICK_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ahogrep {

class AhoCorasick {
public:
    static constexpr int32_t     kNone     = -1;  // "no child" / "no output link"
    static constexpr int32_t     kRoot     = 0;   // node 0 is always the root
    static constexpr std::size_t kAlphabet = 256; // raw bytes; UTF-8 falls out for free

    // One row of the dense transition table. Dense (1 KiB per node) rather than a
    // map because the scan is the hot path: a transition becomes one indexed load.
    // The memory cost is exactly the trade-off the benchmark section measures.
    using Row = std::array<int32_t, kAlphabet>;

    explicit AhoCorasick(bool case_insensitive = false);

    // Inserts `pattern` into the trie and returns its pattern id. Ids are handed
    // out in insertion order starting at 0.
    //
    // Returns kNone if `pattern` is empty. The automaton cannot print a
    // diagnostic, so handling that is left to the caller; ahogrep's CLI reports
    // an error and exits 2. Keeping empty patterns out preserves the invariant
    // "every terminal node has depth >= 1", so the root is never terminal and
    // neither build_failure_links() nor the search loop needs a case for it.
    //
    // Duplicate patterns each keep their own id, by design: one node can end
    // several patterns, so `-e ab -e ab` reports two matches at every
    // occurrence. That keeps "report every occurrence" literally true and lets
    // the differential tester compare match sets without deduplicating.
    //
    // Precondition: build() has not been called yet.
    int32_t add_pattern(std::string_view pattern);

    // Computes the failure and output links. Call once, after the last
    // add_pattern() and before the first search_chunk().
    void build();

    bool built() const { return built_; }

    std::size_t pattern_count() const { return pattern_lengths_.size(); }
    std::size_t node_count()    const { return next_.size(); }

    std::size_t pattern_length(int32_t id) const {
        return pattern_lengths_[static_cast<std::size_t>(id)];
    }

    // Every byte passes through this table before being used as a transition
    // index, both when inserting patterns and when scanning input. In
    // case-sensitive mode it is the identity, so the scan pays no branch for a
    // feature it is not using.
    unsigned char fold(unsigned char c) const { return fold_[c]; }

    bool case_insensitive() const { return case_insensitive_; }

    // True if one or more patterns end exactly at node `v`.
    bool is_terminal(int32_t v) const {
        return !ids_[static_cast<std::size_t>(v)].empty();
    }

    // ============== STUDENT-OWNED CODE -- write this yourself ================
    //
    // Scans `len` bytes of `data` starting from automaton state `state`, and
    // reports every occurrence -- overlapping ones included -- by calling
    //
    //     on_match(int32_t pattern_id, uint64_t end_offset)
    //
    // where `end_offset` is ONE PAST the final byte of the match, counted from
    // the start of the whole stream. `base_offset` is the stream offset of
    // data[0], so the matched text is the half-open range
    // [end_offset - pattern_length(id), end_offset). The tester and the -o /
    // --color output both depend on that definition, so pin it down before
    // writing a line.
    //
    // Returns the state after the last byte, which the caller feeds back in for
    // the next chunk.
    //
    // Preconditions: build() has been called; `state` is a valid node id.
    //
    // THE INVARIANT to hold on to: after consuming input byte i, `state` is the
    // node spelling the longest suffix of everything read so far that is also a
    // prefix of some pattern. Everything else follows from it. If you can state
    // that on video and point at the line that maintains it, this function is
    // done.
    //
    // Guiding questions -- these are the whole design, answer them first:
    //   1. On a byte with no child from `state`, where do you go, and why does
    //      that walk always terminate? Why is the root the only safe place to
    //      stop? If you write `while (state != kRoot)`, what must you re-check
    //      after the loop ends, and what goes wrong if you forget?
    //   2. Landing on a node tells you a pattern ended there -- so why is that
    //      not all the matches ending at this position? Which chain do you walk
    //      to find the rest, and why out_link_ rather than fail_?
    //   3. `state` arrives already non-root when a chunk boundary falls inside a
    //      pattern. Does your loop treat byte 0 of a chunk differently from byte
    //      50? It must not -- that is precisely what the chunk-split test checks.
    //   4. Where does fold() belong, and what breaks under -i if you fold the
    //      input bytes but not the patterns (or the other way round)?
    //
    // Pitfalls that show up in most first drafts: reporting a node's own ids but
    // never walking the output-link chain (so "he" is missed inside "she"); an
    // off-by-one in end_offset; and folding after the transition lookup instead
    // of before it.
    //
    // It is a template so the callback inlines: the tester collects into a
    // vector, the CLI bumps a line counter, and neither pays a std::function
    // call per match. Being a template is also why it lives in this header
    // instead of next to build_failure_links() in aho_corasick.cpp -- a test
    // file has to instantiate it.
    template <class OnMatch>
    int32_t search_chunk(const unsigned char* data, std::size_t len,
                         int32_t state, std::uint64_t base_offset,
                         OnMatch&& on_match) const {
        // TODO(student): step the automaton over data[0..len), following failure
        // links on a mismatch and reporting matches via the output links.
        // Replace this stub. The void casts only exist to keep the scaffold
        // warning-free until you do.

        for (std::size_t i = 0; i < len; ++i) {
            unsigned char folded = fold(data[i]);

            while (state != kRoot && next_[state][folded] == kNone) {
                state = fail_[state];
            }

            if (next_[state][folded] != kNone) {
                state = next_[state][folded];
            } else {
                state = kRoot;
            }

            std::size_t end_offset = base_offset + i + 1;

            for (int32_t id : ids_[state]) {
                on_match(id, end_offset);
            }

            int32_t dictionaryLink = out_link_[state];

            while (dictionaryLink != kNone) {
                for (int32_t id : ids_[dictionaryLink]) {
                    on_match(id, end_offset);
                }
                dictionaryLink = out_link_[dictionaryLink];
            }

        }

        return state;

    }
    // ============== END STUDENT-OWNED CODE ===================================

private:
    // The breadth-first pass that fills fail_ and out_link_. Student-owned as
    // well; its (stubbed) definition is in src/aho_corasick.cpp.
    void build_failure_links();

    // Appends a fresh node at the given depth and returns its id.
    int32_t new_node(int32_t depth);

    // Structure of arrays, one entry per node, all indexed by node id. Laid out
    // this way because the scan touches next_ constantly and the other arrays
    // only on a mismatch or a match.
    std::vector<Row>                  next_;     // transitions, kNone = no child
    std::vector<int32_t>              fail_;     // longest proper suffix that is also a prefix
    std::vector<int32_t>              out_link_; // nearest terminal node up the failure chain, or kNone
    std::vector<int32_t>              depth_;    // length of the string this node spells
    std::vector<std::vector<int32_t>> ids_;      // pattern ids ending here (usually empty or size 1)

    std::vector<std::size_t>          pattern_lengths_;  // indexed by pattern id

    std::array<unsigned char, kAlphabet> fold_{};
    bool case_insensitive_ = false;
    bool built_            = false;
};

}  // namespace ahogrep

#endif  // AHOGREP_AHO_CORASICK_HPP
