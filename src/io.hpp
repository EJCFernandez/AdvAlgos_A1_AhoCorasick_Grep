// io.hpp -- chunked reading, line tracking and buffered output.
//
// Everything that touches a FILE* lives here; the automaton in
// include/aho_corasick.hpp stays free of it.

#ifndef AHOGREP_IO_HPP
#define AHOGREP_IO_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "aho_corasick.hpp"
#include "cli.hpp"

namespace ahogrep {

// 1 MiB. Big enough that read syscalls stop mattering, small enough to stay in
// cache-friendly territory and to keep the carried-over partial line cheap.
constexpr std::size_t kChunkSize = 1u << 20;

// Puts a stream into binary mode on Windows, where the default text mode would
// silently turn \r\n into \n on the way in and \n into \r\n on the way out --
// both of which would corrupt byte offsets and break matching on high bytes.
// A no-op everywhere else.
void set_binary_mode(std::FILE* stream);

// Output goes through one big buffer and lands with fwrite. grep-style tools are
// output-bound on dense matches, and per-line stdio calls (or worse, std::endl)
// dominate the runtime otherwise.
class OutputBuffer {
public:
    explicit OutputBuffer(std::FILE* out, std::size_t capacity = kChunkSize);
    ~OutputBuffer();

    OutputBuffer(const OutputBuffer&)            = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;

    void write(const char* data, std::size_t len);
    void write(std::string_view s) { write(s.data(), s.size()); }
    void put(char c);

    // Appends the decimal form of `value`. Avoids a printf per line number.
    void write_uint(std::uint64_t value);

    void flush();

private:
    std::FILE*        out_;
    std::vector<char> buf_;
    std::size_t       used_ = 0;
};

// ANSI escapes used by --color. Chosen to match GNU grep defaults so that a
// side-by-side diff of coloured output is readable.
constexpr std::string_view kColorMatch = "\x1b[01;31m";
constexpr std::string_view kColorReset = "\x1b[0m";

struct ScanResult {
    std::uint64_t matching_lines = 0;
    bool          ok             = true;  // false if reading failed (exit 2)
};

// Scans one already-open stream to completion and writes whatever the options
// call for. `display_name` is the prefix for output lines, or nullptr for none
// (single-input case). The automaton must already be built.
//
// `chunk_size` is a parameter only so tests/test_io.cpp can shrink it to a few
// bytes and push every line across a chunk boundary; the CLI uses the default.
ScanResult scan_stream(std::FILE* in, const char* display_name,
                       const AhoCorasick& ac, const Options& opts,
                       OutputBuffer& out, std::size_t chunk_size = kChunkSize);

// --dump-automaton: one row per node in breadth-first order (string, depth,
// failure link, output link, pattern ids), followed by a check of the two link
// invariants. Lives here rather than in the automaton so that the automaton
// stays free of I/O.
void dump_automaton(const AhoCorasick& ac, OutputBuffer& out);

}  // namespace ahogrep

#endif  // AHOGREP_IO_HPP
