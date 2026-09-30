// cli.hpp -- argument parsing for ahogrep.
//
// Reading a -f pattern file counts as parsing, so it happens here rather than in
// io.cpp: by the time main() has an Options it needs no further input to build
// the automaton.

#ifndef AHOGREP_CLI_HPP
#define AHOGREP_CLI_HPP

#include <string>
#include <vector>

namespace ahogrep {

struct Options {
    // Patterns in the order they were given, with -e and -f interleaved as they
    // appeared on the command line. Order matters because it fixes the pattern
    // ids, which -o and --color report against.
    std::vector<std::string> patterns;

    // Input files; empty means read stdin.
    std::vector<std::string> input_files;

    bool case_insensitive = false;  // -i, ASCII only
    bool line_numbers     = false;  // -n
    bool count_only       = false;  // -c, counts matching LINES, like grep
    bool only_matching    = false;  // -o, prints every match including overlaps
    bool color            = false;  // --color
    bool dfa              = false;  // --dfa, stretch goal (rejected until built)
    bool dump_automaton   = false;  // --dump-automaton: print nodes and links, exit
};

enum class ParseStatus {
    Ok,
    HelpRequested,  // -h / --help: print usage, exit 0
    Error           // print the message to stderr, exit 2
};

// The usage text, also used by the README.
const char* usage_text();

// Parses argv into `out`. On ParseStatus::Error, `error` holds a message with no
// trailing newline and no program-name prefix (main adds that).
ParseStatus parse_args(int argc, char** argv, Options& out, std::string& error);

}  // namespace ahogrep

#endif  // AHOGREP_CLI_HPP
