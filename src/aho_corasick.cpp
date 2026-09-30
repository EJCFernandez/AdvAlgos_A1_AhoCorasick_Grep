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

// ================= STUDENT-OWNED CODE =================

void AhoCorasick::build_failure_links() {

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

}
