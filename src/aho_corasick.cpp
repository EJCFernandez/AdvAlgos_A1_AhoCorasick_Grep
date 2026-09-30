#include "aho_corasick.hpp"

#include <cassert>
#include <queue>

namespace ahogrep {

namespace {

// ASCII-only fold, as decided: bytes 128-255 are passed through untouched so
// that UTF-8 sequences keep working as plain byte strings. Folding them would
// need to know the encoding, which the byte-level automaton deliberately does
// not. Documented as a known limitation.
unsigned char ascii_lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

}  // namespace

AhoCorasick::AhoCorasick(bool case_insensitive)
    : case_insensitive_(case_insensitive) {
    // Build the fold table once. In case-sensitive mode it is the identity, so
    // the scan can apply it unconditionally and stay branch-free.
    for (std::size_t c = 0; c < kAlphabet; ++c) {
        const unsigned char b = static_cast<unsigned char>(c);
        fold_[c] = case_insensitive_ ? ascii_lower(b) : b;
    }

    const int32_t root = new_node(0);
    (void)root;
    assert(root == kRoot);
}

int32_t AhoCorasick::new_node(int32_t depth) {
    const int32_t id = static_cast<int32_t>(next_.size());
    Row row;
    row.fill(kNone);
    next_.push_back(row);
    fail_.push_back(kRoot);      // provisional; build_failure_links() fixes it
    out_link_.push_back(kNone);
    depth_.push_back(depth);
    ids_.emplace_back();
    return id;
}

int32_t AhoCorasick::add_pattern(std::string_view pattern) {
    assert(!built_ && "all patterns must be added before build()");

    if (pattern.empty()) {
        return kNone;  // caller reports the error; see the header for why
    }

    int32_t state = kRoot;
    for (const char ch : pattern) {
        const unsigned char c = fold_[static_cast<unsigned char>(ch)];
        int32_t child = next_[static_cast<std::size_t>(state)][c];
        if (child == kNone) {
            // new_node() can reallocate next_, so take the id into a local first
            // and only then index next_ again. Writing
            // `next_[state][c] = new_node(...)` would evaluate `next_[state]`
            // before the call and could leave a dangling reference.
            child = new_node(depth_[static_cast<std::size_t>(state)] + 1);
            next_[static_cast<std::size_t>(state)][c] = child;
        }
        state = child;
    }

    const int32_t id = static_cast<int32_t>(pattern_lengths_.size());
    pattern_lengths_.push_back(pattern.size());
    // Duplicates append a second id to the same node rather than being dropped.
    ids_[static_cast<std::size_t>(state)].push_back(id);
    return id;
}

void AhoCorasick::build() {
    build_failure_links();
    built_ = true;
}

// ================= STUDENT-OWNED CODE -- write this yourself =================
//
// Fills fail_ and out_link_ for every node, breadth-first from the root.
//
// What the two arrays mean:
//   fail_[v]     -- the node spelling the longest PROPER suffix of v's string
//                   that is itself a prefix of some pattern (the root if there
//                   is none).
//   out_link_[v] -- the nearest node on v's failure chain that is terminal
//                   (is_terminal()), or kNone. This is a shortcut: the scan
//                   must report patterns that end inside the current one, and
//                   hopping straight between terminal nodes lets it skip the
//                   non-terminal nodes in between. Without it the scan would
//                   walk the whole failure chain at every single input byte.
//
// Preconditions when this runs: the trie is built, node 0 is the root, depth_
// is correct, fail_ is all kRoot and out_link_ all kNone.
//
// THE INVARIANT that makes breadth-first order necessary: fail_[v] always points
// at a node of strictly smaller depth than v. So if you process nodes in
// non-decreasing depth order, fail_ and out_link_ for every node you need to
// read are already final. That single sentence is the "one invariant" the video
// asks for -- and question 2 below is the "what breaks if this line changes"
// example, which is worth writing down as you go.
//
// Guiding questions -- answer these before writing code:
//   1. Depth-1 nodes (the root's children) are the base case. What is fail_ for
//      one of them, and why can it not be worked out by the same rule as the
//      deeper nodes? What would happen if you tried?
//   2. For a node u with child v on byte c: you want the longest proper suffix
//      of u's string plus c that is a prefix of a pattern. You already know
//      fail_[u]. How do you get from fail_[u] to fail_[v], and why might you
//      have to follow the failure chain more than one step? Where does the walk
//      stop? (Try swapping BFS for DFS on the patterns {a, ab, bc} and see which
//      link comes out wrong -- that is the example to show on video.)
//   3. out_link_[v] is expressible in one line from fail_[v], is_terminal() and
//      out_link_[fail_[v]]. Write that line out in words first. Why is it
//      correct to reuse out_link_[fail_[v]] instead of walking the chain?
//   4. Does the order matter -- must fail_[v] be final before you compute
//      out_link_[v]? What does out_link_ end up as if you set it first?
//
// Suggested first test once this compiles: patterns {"he", "she", "his",
// "hers"} over "ushers", the textbook example. Print depth_, fail_ and
// out_link_ per node and check them by hand before running the random tester --
// a wrong link is far easier to see in a 10-node table than in a diff of
// thousands of matches.
//
// std::queue lives in <queue>; add the include when you need it.
void AhoCorasick::build_failure_links() {
    // TODO(student): breadth-first pass computing fail_ and out_link_.
    //
    // Remove this stub. Until it is written, search_chunk() can only ever match
    // patterns starting at the point the automaton happens to be in, so the
    // tests will fail -- that is expected, not a bug in the scaffold.

    std::queue<int32_t> nodeQueue;

    for (std::size_t c = 0; c < kAlphabet; ++c) {
        auto currentChild = next_[0][c];
        if (currentChild != kNone) {
            fail_[currentChild] = kRoot;
            out_link_[currentChild] = kNone;
            nodeQueue.push(currentChild);
        }
    }

    while (!nodeQueue.empty()) {
        int32_t currentNode = nodeQueue.front();

        for (std::size_t c = 0; c < kAlphabet; ++c) {
            int32_t currentChild = next_[currentNode][c];
            if (next_[currentNode][c] != kNone) {
                int32_t currentFailNode = fail_[currentNode];
                while (true) {
                    if (next_[currentFailNode][c] != kNone){
                        fail_[currentChild] = next_[currentFailNode][c];
                        break;
                    }

                    if (currentFailNode == 0) {
                        if (next_[currentFailNode][c] == kNone) {
                            fail_[currentChild] = kRoot;
                            break;
                        }
                    }

                    currentFailNode = fail_[currentFailNode];

                }

                if (is_terminal(fail_[currentChild])) {
                    out_link_[currentChild] = fail_[currentChild];
                } else {
                    out_link_[currentChild] = out_link_[fail_[currentChild]];
                }

                nodeQueue.push(currentChild);
            }
        }

        nodeQueue.pop();
    }

}
// ================= END STUDENT-OWNED CODE ====================================

}  // namespace ahogrep
