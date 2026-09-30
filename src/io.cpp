#include "io.hpp"

#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace ahogrep {

void set_binary_mode(std::FILE* stream) {
#ifdef _WIN32
    _setmode(_fileno(stream), _O_BINARY);
#else
    (void)stream;  // POSIX streams are already byte-exact
#endif
}

OutputBuffer::OutputBuffer(std::FILE* out, std::size_t capacity)
    : out_(out), buf_(capacity) {}

OutputBuffer::~OutputBuffer() {
    // Best effort: a destructor cannot report an error, so main() calls flush()
    // explicitly and checks the stream before returning its exit code.
    flush();
}

void OutputBuffer::write(const char* data, std::size_t len) {
    if (len >= buf_.size()) {
        // Larger than the buffer: flush what is pending and hand it straight to
        // stdio rather than looping, so a very long line costs one write.
        flush();
        std::fwrite(data, 1, len, out_);
        return;
    }
    if (used_ + len > buf_.size()) {
        flush();
    }
    std::memcpy(buf_.data() + used_, data, len);
    used_ += len;
}

void OutputBuffer::put(char c) {
    if (used_ == buf_.size()) {
        flush();
    }
    buf_[used_++] = c;
}

void OutputBuffer::write_uint(std::uint64_t value) {
    char        tmp[20];  // 2^64-1 is 20 digits
    std::size_t n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        put(tmp[--n]);
    }
}

void OutputBuffer::flush() {
    if (used_ != 0) {
        std::fwrite(buf_.data(), 1, used_, out_);
        used_ = 0;
    }
}

// TODO(claude): checklist item 6. The scan loop itself.
//
// Shape it will take, so the interface above does not have to change later:
//   - read kChunkSize bytes with fread into a buffer; keep the automaton state
//     and the absolute stream offset OUTSIDE that loop and pass them to
//     ac.search_chunk();
//   - track line starts while walking the chunk, and carry a straddling partial
//     line over into the next iteration so a matching line can still be printed
//     whole;
//   - patterns never contain \n, so a match can never span a line and the
//     automaton never needs resetting at a line boundary -- worth a comment in
//     the final version, it is the reason this stays simple;
//   - -c only needs "did this line match at all", so it can skip the printing
//     path entirely;
//   - -o and --color need match starts, which come from
//     end_offset - ac.pattern_length(id).
ScanResult scan_stream(std::FILE* in, const char* display_name,
                       const AhoCorasick& ac, const Options& opts,
                       OutputBuffer& out) {
    (void)in;
    (void)display_name;
    (void)ac;
    (void)opts;
    (void)out;
    return ScanResult{0, false};
}

}  // namespace ahogrep
