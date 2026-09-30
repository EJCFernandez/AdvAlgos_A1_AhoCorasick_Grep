# ahogrep

A grep-style command-line tool for searching many fixed patterns at once, built
on an Aho-Corasick automaton. C++17, CMake, no external dependencies; builds with
MSVC on Windows, g++ on Linux and clang on macOS.

Searching for *n* patterns takes one pass over the input, in time proportional to
the input length plus the number of matches — it does not get slower as patterns
are added, which is the point the benchmarks set out to show.

## Status

| Piece | State |
| --- | --- |
| CMake build, layout, trie insertion | done |
| `build_failure_links()` (`src/aho_corasick.cpp`) | done (student-written) |
| search loop `search_chunk()` (`include/aho_corasick.hpp`) | done (student-written) |
| random differential tester (`test_random`) | done |
| edge-case tests (`test_edge`) | done |
| CLI parsing, chunked I/O, line tracking, output, colour | done, tested by `test_io` |
| `--dump-automaton` debug view | done |
| `--dfa` mode (stretch) | not started — the flag is rejected with exit 2 |
| benchmark scripts | not started |

## Building

You need CMake 3.16 or newer and a C++17 compiler. With no build type given, the
project defaults to Release. Warnings are on: `/W4` on MSVC, `-Wall -Wextra` on
GCC/Clang.

### Windows (Visual Studio)

From a normal terminal (PowerShell or cmd) with Visual Studio 2019 or 2022
installed, including the "Desktop development with C++" workload:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Use `-G "Visual Studio 16 2019"` for VS 2019. The `-G` flag can be left out if
Visual Studio is the default generator on the machine. Visual Studio is a
*multi-config* generator: it ignores `CMAKE_BUILD_TYPE`, and the configuration is
chosen at build time with `--config Release`. Leaving `--config` out gives a
Debug build, which is several times slower.

The binaries land in `build\Release\`: `ahogrep.exe`, `test_random.exe`,
`test_edge.exe` and `test_io.exe`. To work in the IDE instead, open
`build\ahogrep.sln` and switch the configuration dropdown to Release.

### Linux (and macOS)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binaries land directly in `build/`: `build/ahogrep`, `build/test_random`
and so on. (`--config Release` is harmless here, so the same two commands with
`--config Release` added work on every platform.)

## Running the tests

```
ctest --test-dir build -C Release --output-on-failure
```

`-C Release` is needed with Visual Studio and harmless elsewhere. `--test-dir`
needs CMake 3.20+; on older versions run `ctest` from inside `build/`. The
whole suite takes about 10 seconds on Windows, most of it in `test_io`'s scratch
file I/O.

There are three test programs:

- **`test_random`**: a randomised differential test of the automaton against
  a naive O(n·m) matcher. It uses tiny alphabets (`{a,b}`, `{a,b,c}`, mixed
  case, high bytes) so that patterns overlap constantly. It compares the full
  set of (pattern id, end offset) matches, and re-feeds every text split at
  random chunk boundaries, including one byte at a time. On a failure it
  shrinks the case and prints it as pasteable C++.
- **`test_edge`**: hand-written, named edge cases:
  - a pattern that is a suffix or a prefix of another
  - duplicates
  - single characters
  - a pattern longer than the text
  - an empty text and an empty chunk
  - a deep failure chain (`{aaa, aa, a}`)
  - the textbook `{he, she, his, hers}` links, checked node by node
  - high bytes and UTF-8
  - `-i` folding, including that bytes 128–255 are *not* folded
  - zero patterns
  - CRLF pattern files, read through the real `-f` parser

  Every case is also run at every possible two-chunk split.
- **`test_io`**: `scan_stream()`, meaning the chunked reader, line tracking and
  output formatting, checked against a naive line-by-line reference. It runs
  at chunk sizes 1, 2, 3, 5, 7, 16, 64 bytes and 1 MiB, so every line
  straddles a chunk boundary at some size. It covers `-n`, `-c`, `-o`,
  `--color`, `-i`, filename prefixes, CRLF text, empty lines, an empty file
  and a final line with no trailing newline.

The randomised testers take an optional case count and first seed:

```
build/Release/test_random 200000        # more cases (Linux: build/test_random)
build/Release/test_random 1 4711        # reproduce seed 4711 only
build/Release/test_io 5000              # more random scan_stream cases
```

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
      --dfa    use the DFA-completed automaton (not built yet)
      --dump-automaton
               print every node's string, depth, failure link and
               output link for the given patterns, then exit
  -h, --help   print this help and exit
```

With no `FILE`, ahogrep reads standard input. With several `FILE`s, each output
line is prefixed with its file name. Short options can be bundled (`-in`), and
`-e` and `-f` take their value attached (`-efoo`) or as the next argument. `--`
ends option parsing.

The exit status follows grep:

- `0` if any line matched
- `1` if none did
- `2` on any error, such as a bad option, an unreadable file or an empty
  pattern. An error gives `2` even if other files matched.

### Examples

These use a small file `poem.txt`:

```
she sells sea shells
by the sea shore
ushers and his heirs
HE SAID HERS
no match here? yes: here
last line without newline: she
```

(The last line has no trailing newline.)

```
$ ahogrep -n -e she -e his poem.txt
1:she sells sea shells
3:ushers and his heirs
6:last line without newline: she

$ ahogrep -c -e she -e his poem.txt
3

$ ahogrep -i -n -e hers poem.txt
3:ushers and his heirs
4:HE SAID HERS

$ ahogrep -o -e he -e she -e hers poem.txt | head -4
she
he
she
he

$ ahogrep -n -e she poem.txt other.txt
poem.txt:1:she sells sea shells
poem.txt:3:ushers and his heirs
poem.txt:6:last line without newline: she
other.txt:3:gamma she

$ ahogrep -c -f patterns.txt poem.txt        # one pattern per line; CRLF is fine
$ cat poem.txt | ahogrep -e sea              # standard input
$ ahogrep --color -e he -e she -e hers poem.txt
```

On Windows, run `build\Release\ahogrep.exe`, with the same arguments.

### `--dump-automaton`

This option prints the automaton for the given patterns and exits, without
reading any input. It shows one row per node, in breadth-first order, which is
the order `build_failure_links()` computes them in. After the table, it
re-checks both link invariants from their definitions.

```
$ ahogrep --dump-automaton -e he -e she -e his -e hers
patterns: 4, nodes: 10 (transition table 10240 bytes)

node  depth  string  fail    output  pattern ids
0     0      ""      -       -
1     1      "h"     0 ""    -
3     1      "s"     0 ""    -
2     2      "he"    0 ""    -       0
6     2      "hi"    0 ""    -
4     2      "sh"    1 "h"   -
8     3      "her"   0 ""    -
7     3      "his"   3 "s"   -       2
5     3      "she"   2 "he"  2 "he"  1
9     4      "hers"  3 "s"   -       3

invariant  depth(fail(v)) < depth(v) for every non-root v: holds (9/9)
invariant  output(v) = nearest terminal node on v's fail chain: holds (9/9)
```

## Design notes

- **Alphabet:** raw bytes, 256 symbols. UTF-8 patterns work as byte sequences
  with no extra machinery.
- **Nodes:** a dense transition table, `std::vector<std::array<int32_t, 256>>`,
  where `-1` means "no child". A transition is one indexed load, at a cost of
  1 KiB per node. This memory-versus-pattern-count trade-off is what the
  benchmarks measure.
- **Streaming:** input is read with `fread` in 1 MiB chunks. The automaton
  state and the stream offset live outside the chunk loop, so a pattern that
  spans a chunk boundary is still found.
- **Line tracking is lazy:** the automaton runs over the chunk without knowing
  about lines. Only when a match is reported does the scanner look for the
  `\n` on either side of it, so unmatched lines cost nothing beyond the
  automaton step. This works for two reasons:
  - No pattern contains `\n`, so no match spans two lines. `-e` values and
    pattern files are split on newlines to guarantee it.
  - Matches arrive in end-offset order.

  A matched line whose end has not been read yet stays open across chunks. The
  partial last line of each chunk is carried to the front of the buffer, so it
  can be printed whole.
- **Output** goes through one 1 MiB buffer written with `fwrite`. There is no
  iostream and no `std::endl`.
- **Callback, not `std::function`:** the search takes a templated callback, so
  it inlines into the caller.
- **Case-insensitive (`-i`)** folds ASCII letters only. The fold is applied to
  patterns at insertion and to input bytes during the scan. Bytes 128–255 pass
  through untouched, so non-ASCII letters are not folded. This is a deliberate
  limitation.
- **Windows:** all files are opened `"rb"`, and stdin and stdout are switched
  to binary mode, so `\r\n` is never rewritten. A trailing `\r` is stripped
  from each line of a pattern file. `\r` in the searched text is kept, so
  CRLF lines print back unchanged.

### Differences from GNU grep

- `-c` counts matching **lines**, like grep.
- `-o` prints **every** match, including overlapping ones, in the order the
  automaton finds them: by end position, longest first. GNU grep prints
  non-overlapping, leftmost-longest matches instead, so the output differs
  where patterns overlap. For example, `-e he -e she -e hers` over `ushers`
  gives `she`, `he`, `hers` here.
- `--color` merges overlapping or touching matches into one highlighted run.
  It always colours, even when the output is not a terminal, and only matches
  are coloured, not file names or line numbers.
- An empty pattern is an **error** (exit 2), whether it comes from `-e ""` or
  from a blank line in a `-f` file. GNU grep treats an empty pattern as
  matching every line. Rejecting it keeps the root node non-terminal, so
  neither the automaton build nor the search needs a special case.
- Duplicate patterns each keep their own id, so a pattern given twice reports
  two matches per occurrence. This shows up in `-o`.
- `-` is not a synonym for standard input. Binary files are searched and
  printed like text, with no "Binary file matches" message.
- On Windows, command-line arguments reach the program in the ANSI code page,
  so non-ASCII `-e` patterns may be mangled. Put UTF-8 patterns in a file and
  use `-f`, which is read as raw bytes.

### Out of scope

The following are listed as future work:

- recursive directory search
- regular expressions
- `-w` whole-word matching
- `-v` inversion
- context lines
