# ahogrep: a multi-pattern grep built on Aho-Corasick

## Context

This is a university assignment (Programming Assignment 1, Track B: "Implementation and Building Something").
The student implements the Aho-Corasick automaton and builds a grep-style command-line tool around it.
The deadline is 3 days from project start, so scope discipline matters.

Final deliverables (produced outside this repo where noted):
1. This repository, with a README explaining how to build and run.
2. The working CLI tool.
3. A written report (written separately): what was built, the tool and its design, what was learned, and an AI-use disclosure.
4. A 3–5 minute video walkthrough in which the student explains the core code, one tricky part, one invariant, and one "what breaks if this line changes" example.

The student must be able to explain every part of this code on video and possibly in a live follow-up with the lecturer.

## Division of labour (IMPORTANT)

**The student writes these two pieces personally:**
- `build_failure_links()`: breadth-first computation of failure links and output (dictionary suffix) links.
- The search loop: stepping the automaton over input bytes, following failure links on mismatch, and reporting matches via output links.

For these two pieces, Claude Code must NOT write or complete the implementation.
Instead it should give hints, ask guiding questions, review code the student has written, point to the location of bugs, explain why a bug occurs, and suggest test cases that expose it.
Leave them as clearly marked stubs (`// TODO(student): ...`) in the scaffold.
If the student explicitly asks to change this arrangement, confirm once, then follow the instruction.

**Claude Code writes everything else:**
- trie insertion
- the random differential tester
- chunked I/O and line tracking
- the argument parser
- output buffering and colour
- CMake setup
- benchmark scripts
- the README

## Settled design decisions

- **Language and build:** C++17 and CMake, with no external dependencies.
  The code must build with MSVC on Windows, g++ on Linux, and clang on macOS.
- **Alphabet:** raw bytes (256 symbols).
  UTF-8 patterns work automatically as byte sequences.
- **Case-insensitive mode (`-i`):** fold ASCII letters only.
  Apply the fold to patterns when they are inserted and to each input byte during the scan.
  The ASCII-only limitation is deliberate and will be documented.
- **Node representation:** a dense transition table, `std::vector<std::array<int32_t, 256>>`, with -1 meaning "no child".
  Node 0 is the root.
  Each node also stores:
  - its failure link
  - its output link (the nearest node on the failure chain that ends a pattern, or -1)
  - the pattern id(s) ending at that node
  - its depth (useful for computing match start positions)
- **Search algorithm:** start with classic Aho-Corasick, which follows failure links at search time on a mismatch.
  A DFA-completion variant (every node has a transition for every byte, filled in during the breadth-first pass) is a stretch goal behind a `--dfa` flag, used as a second benchmark comparison.
- **Streaming:** read input in large chunks (for example 1 MiB) using `fread` in binary mode.
  The automaton state is held outside the chunk loop, so it carries across chunk boundaries.
  Patterns never contain `\n`, so a match can never span lines and the automaton never needs resetting.
  A line that straddles a chunk boundary must be carried over so it can be printed whole.
- **Output:** write through a large output buffer with `fwrite`.
  Never use `std::endl`, and don't use unsynchronised iostream output in hot paths.
- **Windows specifics:**
  - Open all files with `"rb"`.
  - Switch stdin and stdout to binary mode on Windows via `_setmode(_fileno(...), _O_BINARY)` inside `#ifdef _WIN32`.
  - Strip a trailing `\r` from every pattern read from a pattern file.
- **Match semantics:** the automaton reports all occurrences, including overlapping ones, as (pattern id, end offset).
  - `-c` counts matching lines, not matches, to stay consistent with grep and make benchmark outputs comparable.
  - `-o` prints every match, including overlaps.
    This intentionally differs from GNU grep, which prints non-overlapping leftmost-longest matches; the difference will be documented.
- **Exit codes, following grep:** 0 if any line matched, 1 if none matched, 2 on error.

## Open decisions — now settled (2026-09-27)

- **Empty patterns:** rejected with an error, exit 2, rather than mimicking grep.
  Reason: it preserves the invariant "every pattern-terminal node has depth >= 1",
  so the root is never terminal and neither `build_failure_links()` nor the search
  loop needs a special case.
  `add_pattern()` returns `kNone` for an empty pattern and the CLI prints the
  diagnostic, keeping the automaton free of I/O.
  The difference from grep is documented in the README.
- **Duplicate patterns:** keep every id; a node can end several patterns, so
  `-e ab -e ab` reports two matches per occurrence.
  Reason: "report every occurrence" stays literally true and the differential
  tester can compare match sets without deduplicating on either side.

## Repository layout

```
include/aho_corasick.hpp   automaton interface (knows nothing about files or the CLI)
src/aho_corasick.cpp       automaton implementation (contains the student-owned functions)
src/main.cpp               CLI entry point
src/cli.{hpp,cpp}          argument parsing
src/io.{hpp,cpp}           chunked reading, line tracking, buffered output, colour
tests/test_random.cpp      differential tester against a naive matcher
tests/test_edge.cpp        hand-written edge cases
bench/                     data generation and benchmark scripts (Linux, bash)
README.md
CLAUDE.md
```

The API shape is up to Claude Code when scaffolding, but the search must be callable chunk by chunk with externally held state, and must report matches through a callback.
Use a templated callback in the header rather than `std::function` in the hot path.

## CLI specification

```
ahogrep [OPTIONS] (-e PATTERN | -f FILE)... [FILE...]
```

- `-e PATTERN`: add a pattern (repeatable).
- `-f FILE`: read patterns from a file, one per line (repeatable).
- `-i`: ASCII case-insensitive matching.
- `-n`: prefix each output line with its line number.
- `-c`: print the count of matching lines per file.
- `-o`: print only the matched text, one match per line.
- `--color`: highlight matches (ANSI codes).
- `--dfa`: use the DFA-completed automaton (stretch goal).
- `-h` / `--help`: print usage.

With no input files, read stdin.
With multiple files, prefix output lines with the filename, as grep does.

Out of scope, to be listed as future work: recursive directory search, regular expressions, `-w` whole-word matching, `-v` inversion, and context lines.

## Testing

- **Differential random tester.** Use a tiny alphabet (`{a,b}` or `{a,b,c}`) to force heavy overlaps, random pattern sets of length 1–5, and random texts of length 0–200.
  Compare the full set of (pattern id, end offset) matches against a naive matcher, across thousands of seeds.
  Also feed each text split into random chunk boundaries and check that the results are identical.
- **Edge cases:**
  - a pattern that is a suffix of another (e.g. "he" and "she")
  - a pattern that is a prefix of another
  - duplicate patterns
  - single-character patterns
  - a pattern longer than the text
  - an empty text
  - CRLF pattern files
  - high bytes (128–255)
- The student's own functions are tested by the same harness.
  Their failures are the student's to debug, with Claude Code in a hints-and-review role.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`--config` is needed for MSVC's multi-config generators.
Default to Release when no build type is given.
Enable warnings: `-Wall -Wextra` on GCC/Clang, `/W4` on MSVC.

## Benchmark plan (run on Linux with the release build)

Compare these commands:
- `ahogrep`, classic mode
- `ahogrep --dfa`, if built
- `grep -F -f patterns.txt`
- grep run once per pattern in a loop

Parameters:
- Pattern counts: 1, 10, 100, 1,000, 10,000 (100,000 if memory allows).
- File sizes: roughly 1 MB, 10 MB, 100 MB.
- Data: natural-language text, plus random text.
  Build pattern sets as a mix of words sampled from the text (so there are real matches) and random strings (so there are non-matches).

Fairness rules:
- Run grep with `LC_ALL=C`, because locale drastically changes grep's speed.
- Before timing, verify that the tools agree, for example by comparing `-c` outputs.
- Time with `hyperfine`, using warmup runs and sending output to `/dev/null`.
- Record peak memory with `/usr/bin/time -v`.
  Memory versus pattern count for the dense table is expected to be an interesting plot.

Write results to CSV for plotting.

## Working rules for Claude Code

- Comment non-obvious choices briefly, explaining why, since the student must defend the code.
- When a test reveals a bug in code Claude Code wrote, say so plainly and describe what was wrong.
  The student logs these for the AI-use section of the report.
- Do not add features beyond the scope above without asking.
- Keep the automaton library free of I/O and CLI concerns.

## Progress checklist

- [x] Scaffold: CMake, layout, automaton class with trie insertion, student stubs
- [x] Naive matcher and random differential tester
- [ ] Student: `build_failure_links()`
- [ ] Student: search loop with output links
- [ ] All random and edge tests pass, including chunk-split tests
- [ ] CLI parsing, chunked I/O, line tracking, buffered output, colour
- [ ] README
- [ ] (Stretch) DFA completion behind `--dfa`
- [ ] On first Linux configure, check which build type the status line reports with no `-DCMAKE_BUILD_TYPE` given, and record the result for the AI-use log
- [ ] Benchmark data generation and scripts (Linux)
- [ ] Benchmarks run, CSVs and plots produced
