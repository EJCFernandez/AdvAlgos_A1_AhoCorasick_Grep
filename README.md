# ahogrep

A grep-style command-line tool for searching many fixed patterns at once, built
on an Aho-Corasick automaton. C++17, CMake, no external dependencies; builds with
MSVC on Windows, g++ on Linux and clang on macOS.

Searching for *n* patterns takes one pass over the input, in time proportional to
the input length plus the number of matches — it does not get slower as patterns
are added, which is the point the benchmarks set out to show.

## Status

Scaffold stage. The build, the automaton's trie and the project layout are in
place; the two automaton functions marked `TODO(student)` and the pieces marked
`TODO(claude)` are not written yet.

| Piece | State |
| --- | --- |
| CMake build, layout, trie insertion | done |
| `build_failure_links()` (`src/aho_corasick.cpp`) | **student stub** |
| search loop (`include/aho_corasick.hpp`) | **student stub** |
| naive matcher + random differential tester | done |
| edge-case tests | stub, skipped by ctest |
| CLI parsing, chunked I/O, line tracking, colour | stub |
| `--dfa` mode (stretch) | not started |
| benchmark scripts | not started |

`test_edge` exits with code 77 while it is a stub, so `ctest` reports it as
*Skipped* rather than *Passed* — a green run cannot accidentally claim coverage
that does not exist yet. `test_random` is real, and fails until the two
student-owned functions are written.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`--config` is what MSVC's multi-config generators need; it is harmless
elsewhere. With no build type given, the project defaults to Release. Warnings
are on: `-Wall -Wextra` on GCC/Clang, `/W4` on MSVC.

Run the tests:

```
ctest --test-dir build --output-on-failure -C Release
```

(`--test-dir` needs CMake 3.20+; on older versions `cd build && ctest
--output-on-failure -C Release`.)

`test_random` is a randomised differential test against a naive matcher: it runs
10,000 random cases in about 0.2s and, on a failure, shrinks the case to a
minimal one and prints it as pasteable C++ literals. Run it directly for more
cases or to reproduce one seed:

```
build/test_random 200000      # more cases
build/test_random 1 4711      # just seed 4711
```

The binary lands at `build/ahogrep` (`build/Release/ahogrep.exe` on MSVC).

## Usage

```
ahogrep [OPTIONS] (-e PATTERN | -f FILE)... [FILE...]

  -e PATTERN   add a pattern (repeatable)
  -f FILE      read patterns from FILE, one per line (repeatable)
  -i           case-insensitive matching (ASCII letters only)
  -n           prefix each output line with its line number
  -c           print only a count of matching lines per file
  -o           print only the matched text, one match per line
      --color  highlight matches using ANSI escape codes
      --dfa    use the DFA-completed automaton
  -h, --help   print help and exit
```

With no `FILE`, reads standard input. With several `FILE`s, output lines are
prefixed with the file name. Exit status follows grep: `0` if any line matched,
`1` if none did, `2` on error.

```
ahogrep -n -e error -e warning -e fatal app.log
ahogrep -c -f patterns.txt corpus.txt
```

## Design notes

- **Alphabet:** raw bytes, 256 symbols. UTF-8 patterns work as byte sequences
  with no extra machinery.
- **Nodes:** a dense transition table, `std::vector<std::array<int32_t, 256>>`,
  `-1` meaning "no child", so a transition is one indexed load. That costs 1 KiB
  per node — the memory-versus-pattern-count trade-off the benchmarks measure.
- **Streaming:** input is read in 1 MiB chunks; the automaton state lives outside
  the chunk loop, so a pattern spanning a chunk boundary is still found. Patterns
  never contain `\n`, so a match can never span lines and the automaton never
  needs resetting.
- **Callback, not `std::function`:** the search takes a templated callback so it
  inlines into the caller.
- **Case-insensitive (`-i`)** folds ASCII letters only, applied to patterns at
  insertion and to input bytes during the scan. Bytes 128–255 pass through
  untouched, so non-ASCII letters are not folded. Deliberate limitation.

### Differences from GNU grep

- `-c` counts matching **lines**, like grep.
- `-o` prints **every** match, including overlapping ones. GNU grep prints
  non-overlapping leftmost-longest matches, so its output differs on inputs where
  patterns overlap (`-e he -e she` over `ushers`).
- An empty pattern is an **error** (exit 2), where GNU grep treats it as matching
  every line.
- Duplicate patterns each keep their own id, so a pattern given twice reports two
  matches per occurrence.

### Out of scope

Recursive directory search, regular expressions, `-w`, `-v` and context lines —
listed as future work.
