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
            fail(error);
            std::fputs(usage_text(), stderr);
            return kExitError;
        case ParseStatus::Ok:
            break;
    }

    AhoCorasick ac(opts.case_insensitive);
    for (const std::string& p : opts.patterns) {
        if (ac.add_pattern(p) == AhoCorasick::kNone) {
            // Only reason add_pattern refuses; see the settled decision on empty
            // patterns in CLAUDE.md.
            fail("empty pattern is not allowed");
            return kExitError;
        }
    }
    ac.build();

    OutputBuffer out(stdout);
    // grep prefixes output with the file name only when there is more than one
    // input; a single file or stdin prints bare lines.
    const bool    show_names   = opts.input_files.size() > 1;
    std::uint64_t total_matches = 0;
    bool          had_error     = false;

    if (opts.input_files.empty()) {
        const ScanResult r = scan_stream(stdin, nullptr, ac, opts, out);
        total_matches += r.matching_lines;
        had_error = had_error || !r.ok;
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
            total_matches += r.matching_lines;
            had_error = had_error || !r.ok;
        }
    }

    out.flush();
    if (std::fflush(stdout) != 0) {
        fail("write error");
        return kExitError;
    }

    if (had_error) {
        return kExitError;
    }
    return total_matches > 0 ? kExitMatched : kExitNoMatch;
}
