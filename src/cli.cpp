#include "cli.hpp"

namespace ahogrep {

const char* usage_text() {
    return
        "Usage: ahogrep [OPTIONS] (-e PATTERN | -f FILE)... [FILE...]\n"
        "\n"
        "Multi-pattern fixed-string search built on an Aho-Corasick automaton.\n"
        "\n"
        "Patterns:\n"
        "  -e PATTERN   add a pattern (repeatable)\n"
        "  -f FILE      read patterns from FILE, one per line (repeatable)\n"
        "\n"
        "Options:\n"
        "  -i           case-insensitive matching (ASCII letters only)\n"
        "  -n           prefix each output line with its line number\n"
        "  -c           print only a count of matching lines per file\n"
        "  -o           print only the matched text, one match per line\n"
        "      --color  highlight matches using ANSI escape codes\n"
        "      --dfa    use the DFA-completed automaton\n"
        "  -h, --help   print this help and exit\n"
        "\n"
        "With no FILE, reads standard input. With several FILEs, output lines are\n"
        "prefixed with the file name.\n"
        "\n"
        "Exit status: 0 if any line matched, 1 if none did, 2 on error.\n";
}

// TODO(claude): checklist item 6. Hand-rolled parser, no getopt (MSVC has none).
// Points to settle while writing it:
//   - bundled short flags (-in) and attached values (-ePATTERN), or not;
//   - "--" ends option parsing;
//   - a pattern file is opened "rb" and every trailing \r is stripped, so a CRLF
//     pattern file written on Windows works on Linux too;
//   - an empty pattern is an error (exit 2), per the settled decision;
//   - no -e and no -f at all is an error, not an empty pattern set.
ParseStatus parse_args(int argc, char** argv, Options& out, std::string& error) {
    (void)argc;
    (void)argv;
    (void)out;
    error = "argument parsing is not implemented yet (scaffold)";
    return ParseStatus::Error;
}

}  // namespace ahogrep
