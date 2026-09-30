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
- **Search algorithm:** classic Aho-Corasick, which follows failure links at search time on a mismatch.
  A DFA-completion variant (every node has a transition for every byte, filled in during the breadth-first pass) was planned as a stretch goal behind `--dfa`.
  It was **dropped on 2026-09-30 for lack of time** and is listed as future work.
  The flag is still parsed and rejected with exit 2, so nobody silently gets classic mode when asking for DFA mode.
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

## Benchmark environment and workflow (2026-09-30)

- Benchmarks run in **WSL2 Ubuntu** on the same laptop as development: GCC 15.2.0, CMake 4.2.3.
- The repo is cloned to `~/ahogrep` inside WSL, **not** under `/mnt/c`, because cross-filesystem I/O would distort the timings.
- **Workflow:** Claude Code edits on Windows, the student commits, then runs `git pull` in `~/ahogrep`.
  Claude Code may run commands in WSL via `wsl.exe -d Ubuntu` (the default WSL distro is `docker-desktop`, which has no bash).
- **Status:** all tests pass in WSL, and `ahogrep -n` output is identical to `LC_ALL=C grep -n -F` on `examples/`.

## Repository layout

```
include/aho_corasick.hpp   automaton interface (knows nothing about files or the CLI)
src/aho_corasick.cpp       automaton implementation (contains the student-owned functions)
src/main.cpp               CLI entry point
src/cli.{hpp,cpp}          argument parsing
src/io.{hpp,cpp}           chunked reading, line tracking, buffered output, colour
tests/test_random.cpp      differential tester against a naive matcher
tests/test_edge.cpp        hand-written edge cases
tests/test_io.cpp          scan_stream() vs a naive line-by-line reference, at chunk sizes down to 1 byte
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
- `--dfa`: DFA-completed automaton. Dropped (future work); the flag is rejected with exit 2.
- `--dump-automaton`: print each node's string, depth, fail link and output link, then exit (debug/video aid; added 2026-09-30 at the student's request).
- `-h` / `--help`: print usage.

With no input files, read stdin.
With multiple files, prefix output lines with the filename, as grep does.

Out of scope, to be listed as future work: the `--dfa` DFA-completed automaton, recursive directory search, regular expressions, `-w` whole-word matching, `-v` inversion, and context lines.

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

Scripts live in `bench/` (bash, plus Python for data generation and matplotlib plots); `bench/README.md` has the details.
Entry point: `bench/run_bench.sh quick|full`.

Compare these commands, all with `-c` so output cost doesn't dominate:
- `ahogrep -c -f patterns`
- `LC_ALL=C grep -F -c -f patterns`
- `rg -F -c -f patterns` (bonus: an industrial Aho-Corasick/SIMD implementation)
- grep run once per pattern in a loop, **only up to 100 patterns** (impractically slow beyond); the cap is noted in the results
- one extra `grep -F` run under a UTF-8 locale at a single configuration, to show the locale effect

Parameters:
- Pattern counts: 1, 10, 100, 1,000, 10,000, 100,000.
- File sizes: roughly 1 MB, 10 MB, 100 MB.
- Data (deterministic seeds, generated into the gitignored `bench/data/`): natural-language text from a few public-domain Project Gutenberg books, repeated to size, with a synthetic Zipf-distributed fallback if the download fails; plus random lowercase text.
  Pattern sets are half words sampled from the text (so there are real matches) and half random strings of similar length (so there are non-matches).

Measured separately:
- automaton construction time vs pattern count (every tool on an empty input file)
- peak memory (max RSS from `/usr/bin/time -v`) vs pattern count, alongside ahogrep's node count and transition-table size (read from the `--dump-automaton` header)

Fairness rules:
- Run grep with `LC_ALL=C`, because locale drastically changes grep's speed.
- Before timing each configuration, verify that ahogrep, grep -F and rg agree on the `-c` count, and abort loudly if not.
- Time with `hyperfine`, using warmup runs, with output sent to a pipe that is discarded (`--output=pipe`), **not** to `/dev/null`.
  GNU grep detects a `/dev/null` stdout and stops at the first match, which made `grep -F -c` look like it scanned 100 MB in about 1 ms.
- Record the environment (CPU, kernel, compiler and tool versions) next to the results.

Results go to `bench/results/<mode>/` as CSV and hyperfine JSON, and the plots are PNGs.

## Working rules for Claude Code

- Comment non-obvious choices briefly, explaining why, since the student must defend the code.
- When a test reveals a bug in code Claude Code wrote, say so plainly and describe what was wrong.
  The student logs these for the AI-use section of the report.
- Do not add features beyond the scope above without asking.
- Keep the automaton library free of I/O and CLI concerns.

## Progress checklist

- [x] Scaffold: CMake, layout, automaton class with trie insertion, student stubs
- [x] Naive matcher and random differential tester
- [x] Student: `build_failure_links()`
- [x] Student: search loop with output links
- [x] All random and edge tests pass, including chunk-split tests
- [x] CLI parsing, chunked I/O, line tracking, buffered output, colour
- [x] `--dump-automaton` debug view (for the video)
- [x] README
- [-] (Stretch) DFA completion behind `--dfa`: dropped 2026-09-30, future work
- [x] On first Linux configure, check which build type the status line reports with no `-DCMAKE_BUILD_TYPE` given, and record the result for the AI-use log.
  Result (2026-09-30, WSL2, GCC 15.2.0, CMake 4.2.3): `ahogrep: build type is Release`, and the cache holds `CMAKE_BUILD_TYPE=Release`.
- [x] Benchmark data generation and scripts (Linux); quick mode tested in WSL
- [ ] Benchmarks run, CSVs and plots produced
