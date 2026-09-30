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

    // Read-only views of one node, for --dump-automaton and the edge tests.
    // Nothing on the search path uses these; they exist so the links can be
    // inspected from outside without making the tests friends of the class.
    int32_t child(int32_t v, unsigned char c) const {
        return next_[static_cast<std::size_t>(v)][c];
    }
    int32_t fail_link(int32_t v) const   { return fail_[static_cast<std::size_t>(v)]; }
    int32_t output_link(int32_t v) const { return out_link_[static_cast<std::size_t>(v)]; }
    int32_t depth(int32_t v) const       { return depth_[static_cast<std::size_t>(v)]; }
    const std::vector<int32_t>& ids_at(int32_t v) const {
        return ids_[static_cast<std::size_t>(v)];
    }

    // ============== STUDENT-OWNED CODE ================

    template <class OnMatch>
    int32_t search_chunk(const unsigned char* data, std::size_t len,
                         int32_t state, std::uint64_t base_offset,
                         OnMatch&& on_match) const {

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
