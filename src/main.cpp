// main.cpp -- ahogrep entry point.
//
// This file is only wiring: parse, build the automaton, scan each input, pick an
// exit code. The matching lives in include/aho_corasick.hpp and the I/O in
// src/io.cpp.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "aho_corasick.hpp"
#include "cli.hpp"
#include "io.hpp"

namespace {

constexpr int kExitMatched   = 0;
constexpr int kExitNoMatch   = 1;
constexpr int kExitError     = 2;

void fail(const std::string& message) {
    std::fprintf(stderr, "ahogrep: %s\n", message.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ahogrep;

    // Before any I/O: on Windows the default text mode would rewrite \r\n, which
    // would break byte offsets and corrupt binary input.
    set_binary_mode(stdin);
    set_binary_mode(stdout);

    Options     opts;
    std::string error;
    switch (parse_args(argc, argv, opts, error)) {
        case ParseStatus::HelpRequested:
            std::fputs(usage_text(), stdout);
            return kExitMatched;
        case ParseStatus::Error:
            // A one-line hint rather than the whole usage text, as grep does,
            // so the actual error message is not scrolled away.
            fail(error);
            std::fputs("Try 'ahogrep --help' for more information.\n", stderr);
            return kExitError;
        case ParseStatus::Ok:
            break;
    }

    AhoCorasick ac(opts.case_insensitive);
    for (const std::string& p : opts.patterns) {
        if (ac.add_pattern(p) == AhoCorasick::kNone) {
            // Only reason add_pattern refuses; see the settled decision on empty
            // patterns in CLAUDE.md. parse_args() already rejects empty patterns
            // with a more specific message, so this is the backstop.
            fail("empty pattern is not allowed");
            return kExitError;
        }
    }
    ac.build();

    OutputBuffer out(stdout);

    if (opts.dump_automaton) {
        // A debugging view, not a search: input files are ignored.
        dump_automaton(ac, out);
        out.flush();
        return std::fflush(stdout) == 0 ? kExitMatched : kExitError;
    }

    // grep prefixes output with the file name only when there is more than one
    // input; a single file or stdin prints bare lines.
    const bool    show_names     = opts.input_files.size() > 1;
    std::uint64_t matching_lines = 0;
    bool          had_error      = false;

    if (opts.input_files.empty()) {
        const ScanResult r = scan_stream(stdin, nullptr, ac, opts, out);
        matching_lines += r.matching_lines;
        if (!r.ok) {
            fail("(standard input): read error");
            had_error = true;
        }
    } else {
        for (const std::string& path : opts.input_files) {
            std::FILE* in = std::fopen(path.c_str(), "rb");
            if (in == nullptr) {
                fail(path + ": cannot open file");
                had_error = true;
                continue;  // grep keeps going after an unreadable file
            }
            const ScanResult r =
                scan_stream(in, show_names ? path.c_str() : nullptr, ac, opts, out);
            std::fclose(in);
            matching_lines += r.matching_lines;
            if (!r.ok) {
                // e.g. a directory on Linux: fopen succeeds, fread fails.
                fail(path + ": read error");
                had_error = true;
            }
        }
    }

    out.flush();
    if (std::fflush(stdout) != 0) {
        fail("write error");
        return kExitError;
    }

    // grep's rule: an error wins over "matched", even if other files matched.
    if (had_error) {
        return kExitError;
    }
    return matching_lines > 0 ? kExitMatched : kExitNoMatch;
}
