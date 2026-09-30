#include "cli.hpp"

#include <cstdio>
#include <utility>

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
        "      --dfa    use the DFA-completed automaton (not built yet)\n"
        "      --dump-automaton\n"
        "               print every node's string, depth, failure link and\n"
        "               output link for the given patterns, then exit\n"
        "  -h, --help   print this help and exit\n"
        "\n"
        "With no FILE, reads standard input. With several FILEs, output lines are\n"
        "prefixed with the file name.\n"
        "\n"
        "Exit status: 0 if any line matched, 1 if none did, 2 on error.\n";
}

namespace {

// Adds the patterns in `text` to `out`, split on '\n'.
//
// Splitting is not optional: the line tracker in io.cpp relies on "no pattern
// contains \n", because that is what guarantees a match never spans two lines.
// grep treats a newline inside -e the same way, so this is also the familiar
// behaviour.
//
// `strip_cr` is set for pattern files: a file saved on Windows ends its lines
// with \r\n, and without stripping, every pattern would silently carry a \r and
// match nothing on LF input. -e arguments are taken literally.
//
// A pattern file's final newline terminates the last line rather than starting
// an empty one, so "a\nb\n" is two patterns, as in grep.
bool add_patterns(const std::string& text, bool strip_cr, bool is_file,
                  const std::string& source, std::vector<std::string>& out,
                  std::string& error) {
    std::size_t start   = 0;
    std::size_t line_no = 1;
    while (start < text.size() || (!is_file && start == text.size())) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        std::string p = text.substr(start, nl - start);
        if (strip_cr && !p.empty() && p.back() == '\r') p.pop_back();

        if (p.empty()) {
            // Rejected by design (CLAUDE.md): keeps the root non-terminal. Caught
            // here rather than in main() only to be able to say where it came from.
            error = is_file ? source + ": line " + std::to_string(line_no) +
                                  " is an empty pattern; empty patterns are not allowed"
                            : "empty pattern given with -e; empty patterns are not allowed";
            return false;
        }
        out.push_back(std::move(p));

        if (nl == text.size()) break;
        start = nl + 1;
        ++line_no;
    }
    return true;
}

bool read_pattern_file(const std::string& path, std::vector<std::string>& out,
                       std::string& error) {
    // "rb": text mode on Windows would eat the \r we strip explicitly, which
    // sounds harmless until the same file is read on Linux, where it is not.
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        error = path + ": cannot open pattern file";
        return false;
    }
    std::string text;
    char        buf[1 << 16];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    const bool read_failed = std::ferror(f) != 0;
    std::fclose(f);
    if (read_failed) {
        error = path + ": read error";
        return false;
    }
    // An empty file is zero patterns, not an error: grep -f /dev/null matches
    // nothing and exits 1, and so does ahogrep.
    return add_patterns(text, /*strip_cr=*/true, /*is_file=*/true, path, out, error);
}

}  // namespace

// Hand-rolled, since MSVC has no getopt. Follows getopt conventions closely
// enough that grep muscle memory works:
//   - short flags bundle (-in), and -e / -f take their value either attached
//     (-efoo) or as the next argument (-e foo), which may itself start with '-';
//   - options and file operands may be interleaved (GNU-style permutation);
//   - "--" ends option parsing, so a file named "-x" can still be searched.
ParseStatus parse_args(int argc, char** argv, Options& out, std::string& error) {
    bool options_done  = false;
    bool pattern_given = false;  // an -e or -f appeared, even an empty -f file

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (options_done || arg.size() < 2 || arg[0] != '-') {
            out.input_files.push_back(arg);  // includes a lone "-" (opened as a file)
            continue;
        }
        if (arg == "--") {
            options_done = true;
            continue;
        }

        if (arg[1] == '-') {
            if (arg == "--help") return ParseStatus::HelpRequested;
            if (arg == "--color") {
                out.color = true;
            } else if (arg == "--dump-automaton") {
                out.dump_automaton = true;
            } else if (arg == "--dfa") {
                // Refused rather than silently ignored: a benchmark that asked
                // for DFA mode and quietly got classic mode would be worse than
                // no number at all.
                error = "--dfa: the DFA-completed automaton is not implemented yet";
                return ParseStatus::Error;
            } else {
                error = "unknown option " + arg;
                return ParseStatus::Error;
            }
            continue;
        }

        // A cluster of short options, e.g. -in or -ieFOO.
        for (std::size_t j = 1; j < arg.size(); ++j) {
            const char c = arg[j];
            if (c == 'e' || c == 'f') {
                std::string value;
                if (j + 1 < arg.size()) {
                    value = arg.substr(j + 1);
                } else if (i + 1 < argc) {
                    value = argv[++i];
                } else {
                    error = std::string("option -") + c + " requires an argument";
                    return ParseStatus::Error;
                }
                pattern_given = true;
                const bool ok = (c == 'e')
                    ? add_patterns(value, /*strip_cr=*/false, /*is_file=*/false, "",
                                   out.patterns, error)
                    : read_pattern_file(value, out.patterns, error);
                if (!ok) return ParseStatus::Error;
                break;  // the value consumed the rest of the cluster
            }
            switch (c) {
                case 'i': out.case_insensitive = true; break;
                case 'n': out.line_numbers     = true; break;
                case 'c': out.count_only       = true; break;
                case 'o': out.only_matching    = true; break;
                case 'h': return ParseStatus::HelpRequested;
                default:
                    error = std::string("unknown option -") + c;
                    return ParseStatus::Error;
            }
        }
    }

    if (!pattern_given) {
        error = "no pattern given; use -e PATTERN or -f FILE";
        return ParseStatus::Error;
    }
    return ParseStatus::Ok;
}

}  // namespace ahogrep
