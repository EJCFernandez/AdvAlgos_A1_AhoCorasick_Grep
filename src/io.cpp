#include "io.hpp"

#include <algorithm>
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

// ------------------------------------------------------------------ scanning --
//
// How the scan works, in one paragraph:
//
// The automaton runs over each chunk exactly once, with its state carried from
// chunk to chunk, and knows nothing about lines. Lines are worked out LAZILY,
// only around matches: when a match lands past the end of the line currently
// being collected, we look backwards for the '\n' that starts its line and
// forwards (memchr) for the '\n' that ends it. Lines with no match are never
// examined at all, which keeps the common case at "one automaton step per byte".
//
// Two facts make this correct:
//   1. No pattern contains '\n' (cli.cpp splits on it), so a match never spans
//      two lines. That is also why the automaton never needs resetting at a
//      line boundary: it would never carry a match across one anyway.
//   2. search_chunk() reports matches in non-decreasing end offset, so all the
//      matches of one line arrive before any match of a later line, and lines
//      come out in order.
//
// All positions are absolute stream offsets; `buf_base_` is the stream offset of
// buf_[0]. The buffer is [carried partial line | newly read chunk], and buf_[0]
// is always the start of a line -- that is the invariant the backward scan
// relies on to stop at index 0.

namespace {

constexpr std::uint64_t kUnknownEnd = UINT64_MAX;  // line's '\n' not read yet

struct Span {
    std::uint64_t start;
    std::uint64_t end;  // one past the last byte
};

class Scanner {
public:
    Scanner(std::FILE* in, const char* name, const AhoCorasick& ac,
            const Options& opts, OutputBuffer& out, std::size_t chunk_size)
        : in_(in), name_(name), ac_(ac), opts_(opts), out_(out),
          chunk_size_(chunk_size == 0 ? 1 : chunk_size),
          // -c never prints a line, so it needs neither the text of a
          // straddling line nor where lines start -- only where they end.
          keep_text_(!opts.count_only),
          need_spans_(!opts.count_only && (opts.only_matching || opts.color)) {}

    ScanResult run();

private:
    void on_match(int32_t id, std::uint64_t end);
    void finish_line();
    void print_line();
    void print_prefix();
    void count_newlines_to(std::uint64_t offset);

    const char* at(std::uint64_t offset) const {
        return buf_.data() + (offset - buf_base_);
    }

    std::FILE*         in_;
    const char*        name_;
    const AhoCorasick& ac_;
    const Options&     opts_;
    OutputBuffer&      out_;
    const std::size_t  chunk_size_;
    const bool         keep_text_;
    const bool         need_spans_;

    std::vector<char> buf_;
    std::uint64_t     buf_base_ = 0;  // stream offset of buf_[0]
    std::size_t       len_      = 0;  // valid bytes in buf_

    // The matched line currently being collected. It stays open until its '\n'
    // has been read, which may be several chunks later.
    bool              active_     = false;
    std::uint64_t     line_start_ = 0;
    // Offset of the line's '\n' (or of end of stream), kUnknownEnd while it has
    // not been read yet. Reset to 0 when no line is open, so that the fast path
    // in on_match() is the single comparison `last < line_end_`.
    std::uint64_t     line_end_   = 0;
    std::vector<Span> spans_;  // only filled for -o / --color

    // -n: '\n' bytes seen in [0, counted_upto_). Counted only up to lines that
    // are printed, and before the bytes are dropped from the buffer.
    std::uint64_t counted_upto_ = 0;
    std::uint64_t newlines_     = 0;

    std::uint64_t matching_lines_ = 0;
};

ScanResult Scanner::run() {
    std::size_t carry = 0;  // bytes of the partial last line kept at buf_[0..carry)
    int32_t     state = AhoCorasick::kRoot;  // lives across chunks, like the base offset

    for (;;) {
        if (buf_.size() < carry + chunk_size_) buf_.resize(carry + chunk_size_);
        const std::size_t n = std::fread(buf_.data() + carry, 1, chunk_size_, in_);
        if (n == 0) break;
        len_ = carry + n;
        const std::uint64_t chunk_base = buf_base_ + carry;
        const char*         fresh      = buf_.data() + carry;

        // A matched line left open by the previous chunk may end in these bytes.
        // It must be closed before scanning, or a match after its '\n' would be
        // taken as part of it.
        if (active_ && line_end_ == kUnknownEnd) {
            const void* nl = std::memchr(fresh, '\n', n);
            if (nl != nullptr) {
                line_end_ = chunk_base + static_cast<std::uint64_t>(
                                             static_cast<const char*>(nl) - fresh);
            }
        }

        state = ac_.search_chunk(reinterpret_cast<const unsigned char*>(fresh), n, state,
                                 chunk_base, [this](int32_t id, std::uint64_t end) {
                                     on_match(id, end);
                                 });

        if (active_ && line_end_ != kUnknownEnd) finish_line();

        if (!keep_text_) {
            buf_base_ += len_;
            carry = 0;
            continue;
        }

        // Keep everything after the last '\n' for the next round. Only the fresh
        // bytes are searched: the carried part is known to contain no '\n', and
        // re-scanning it every chunk would go quadratic on a file with one huge
        // line.
        std::size_t k = len_;
        while (k > carry && buf_[k - 1] != '\n') --k;
        const std::size_t keep_from = (k > carry) ? k : 0;

        if (opts_.line_numbers) count_newlines_to(buf_base_ + keep_from);
        if (keep_from > 0) {
            std::memmove(buf_.data(), buf_.data() + keep_from, len_ - keep_from);
        }
        carry = len_ - keep_from;
        buf_base_ += keep_from;
        len_ = carry;
    }

    ScanResult result;
    result.ok = std::ferror(in_) == 0;

    // A final line with no trailing '\n' ends at end of stream.
    if (active_) {
        line_end_ = buf_base_ + carry;
        finish_line();
    }

    if (opts_.count_only) {
        print_prefix();
        out_.write_uint(matching_lines_);
        out_.put('\n');
    }
    result.matching_lines = matching_lines_;
    return result;
}

void Scanner::on_match(int32_t id, std::uint64_t end) {
    const std::uint64_t len  = ac_.pattern_length(id);
    const std::uint64_t last = end - 1;  // offset of the match's final byte

    // Fast path: another match on the line already open. Also the whole cost of
    // a match in plain and -c mode once its line has been found.
    if (last < line_end_) {
        if (need_spans_) spans_.push_back(Span{end - len, end});
        return;
    }

    // The match is past the open line's '\n', so that line is complete.
    if (active_) finish_line();

    active_ = true;
    ++matching_lines_;

    const std::size_t i  = static_cast<std::size_t>(last - buf_base_);
    const void*       nl = std::memchr(buf_.data() + i, '\n', len_ - i);
    line_end_ = (nl != nullptr)
        ? buf_base_ + static_cast<std::uint64_t>(static_cast<const char*>(nl) - buf_.data())
        : kUnknownEnd;

    if (keep_text_) {
        // Stops at index 0 at the latest, which is always a line start.
        std::size_t k = i;
        while (k > 0 && buf_[k - 1] != '\n') --k;
        line_start_ = buf_base_ + k;
    }
    if (need_spans_) spans_.push_back(Span{end - len, end});
}

void Scanner::finish_line() {
    if (!opts_.count_only) print_line();
    active_   = false;
    line_end_ = 0;
    spans_.clear();
}

void Scanner::count_newlines_to(std::uint64_t offset) {
    if (offset <= counted_upto_) return;
    newlines_ += static_cast<std::uint64_t>(
        std::count(at(counted_upto_), at(offset), '\n'));
    counted_upto_ = offset;
}

void Scanner::print_prefix() {
    if (name_ != nullptr) {
        out_.write(name_, std::strlen(name_));
        out_.put(':');
    }
    if (opts_.line_numbers && !opts_.count_only) {
        out_.write_uint(newlines_ + 1);  // valid after count_newlines_to(line_start_)
        out_.put(':');
    }
}

void Scanner::print_line() {
    if (opts_.line_numbers) count_newlines_to(line_start_);

    if (opts_.only_matching) {
        // One output line per match, in the order the automaton reported them:
        // by end offset, longest first at a shared end. So "ushers" with
        // {he, she, hers} gives she, he, hers -- "he" right after "she" is the
        // output link firing, visibly.
        for (const Span& s : spans_) {
            print_prefix();
            if (opts_.color) out_.write(kColorMatch);
            out_.write(at(s.start), static_cast<std::size_t>(s.end - s.start));
            if (opts_.color) out_.write(kColorReset);
            out_.put('\n');
        }
        return;
    }

    print_prefix();
    if (!opts_.color) {
        out_.write(at(line_start_), static_cast<std::size_t>(line_end_ - line_start_));
        out_.put('\n');
        return;
    }

    // --color: overlapping matches are merged into one highlighted run, so
    // "ushers" with {she, hers} highlights "shers" once rather than emitting
    // nested escape codes.
    std::sort(spans_.begin(), spans_.end(),
              [](const Span& a, const Span& b) { return a.start < b.start; });
    std::uint64_t pos = line_start_;
    std::size_t   k   = 0;
    while (k < spans_.size()) {
        const std::uint64_t run_start = spans_[k].start;
        std::uint64_t       run_end   = spans_[k].end;
        for (++k; k < spans_.size() && spans_[k].start <= run_end; ++k) {
            run_end = std::max(run_end, spans_[k].end);
        }
        out_.write(at(pos), static_cast<std::size_t>(run_start - pos));
        out_.write(kColorMatch);
        out_.write(at(run_start), static_cast<std::size_t>(run_end - run_start));
        out_.write(kColorReset);
        pos = run_end;
    }
    out_.write(at(pos), static_cast<std::size_t>(line_end_ - pos));
    out_.put('\n');
}

}  // namespace

ScanResult scan_stream(std::FILE* in, const char* display_name,
                       const AhoCorasick& ac, const Options& opts,
                       OutputBuffer& out, std::size_t chunk_size) {
    Scanner scanner(in, display_name, ac, opts, out, chunk_size);
    return scanner.run();
}

// ------------------------------------------------------------ dump-automaton --

namespace {

// Quoted, with anything outside printable ASCII as \xHH so the table stays
// aligned and a high byte cannot upset the terminal.
std::string quote(const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    std::string       q      = "\"";
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
            q += static_cast<char>(c);
        } else {
            q += "\\x";
            q += kHex[c >> 4];
            q += kHex[c & 0x0f];
        }
    }
    return q + "\"";
}

void write_padded(OutputBuffer& out, const std::string& s, std::size_t width) {
    out.write(s);
    for (std::size_t i = s.size(); i < width; ++i) out.put(' ');
}

}  // namespace

void dump_automaton(const AhoCorasick& ac, OutputBuffer& out) {
    const int32_t kRoot = AhoCorasick::kRoot;
    const int32_t kNone = AhoCorasick::kNone;

    // Breadth-first from the root -- the same order build_failure_links()
    // processes nodes in, so the table reads top to bottom the way the links
    // were computed. A node's string is its parent's plus the edge byte.
    std::vector<std::string> str(ac.node_count());
    std::vector<int32_t>     order{kRoot};
    for (std::size_t h = 0; h < order.size(); ++h) {
        const int32_t v = order[h];
        for (std::size_t c = 0; c < AhoCorasick::kAlphabet; ++c) {
            const int32_t w = ac.child(v, static_cast<unsigned char>(c));
            if (w != kNone) {
                str[static_cast<std::size_t>(w)] =
                    str[static_cast<std::size_t>(v)] + static_cast<char>(c);
                order.push_back(w);
            }
        }
    }

    auto node_label = [&](int32_t v) {
        return v == kNone ? std::string("-")
                          : std::to_string(v) + " " + quote(str[static_cast<std::size_t>(v)]);
    };

    std::vector<std::string> strings, fails, outs;
    std::size_t w_str = 6, w_fail = 4, w_out = 6;
    for (const int32_t v : order) {
        strings.push_back(quote(str[static_cast<std::size_t>(v)]));
        fails.push_back(v == kRoot ? std::string("-") : node_label(ac.fail_link(v)));
        outs.push_back(node_label(ac.output_link(v)));
        w_str  = std::max(w_str, strings.back().size());
        w_fail = std::max(w_fail, fails.back().size());
        w_out  = std::max(w_out, outs.back().size());
    }

    out.write("patterns: ");
    out.write_uint(ac.pattern_count());
    out.write(", nodes: ");
    out.write_uint(ac.node_count());
    out.write(" (transition table ");
    out.write_uint(ac.node_count() * sizeof(AhoCorasick::Row));
    out.write(" bytes)");
    if (ac.case_insensitive()) out.write(", case-insensitive (strings shown folded)");
    out.write("\n\n");

    write_padded(out, "node", 6);
    write_padded(out, "depth", 7);
    write_padded(out, "string", w_str + 2);
    write_padded(out, "fail", w_fail + 2);
    write_padded(out, "output", w_out + 2);
    out.write("pattern ids\n");

    for (std::size_t r = 0; r < order.size(); ++r) {
        const int32_t v = order[r];
        write_padded(out, std::to_string(v), 6);
        write_padded(out, std::to_string(ac.depth(v)), 7);
        write_padded(out, strings[r], w_str + 2);
        write_padded(out, fails[r], w_fail + 2);
        write_padded(out, outs[r], w_out + 2);
        const std::vector<int32_t>& ids = ac.ids_at(v);
        for (std::size_t k = 0; k < ids.size(); ++k) {
            if (k) out.put(',');
            out.write_uint(static_cast<std::uint64_t>(ids[k]));
        }
        out.put('\n');
    }

    // Recheck both invariants from their definitions, independently of how
    // build_failure_links() computed them, so the dump doubles as a proof on
    // screen rather than just a listing.
    std::size_t depth_ok = 0, output_ok = 0;
    const std::size_t non_root = order.size() - 1;
    for (const int32_t v : order) {
        if (v == kRoot) continue;
        const int32_t f = ac.fail_link(v);
        if (ac.depth(f) < ac.depth(v)) ++depth_ok;

        int32_t expect = f;  // nearest terminal node on the failure chain
        while (expect != kRoot && !ac.is_terminal(expect)) expect = ac.fail_link(expect);
        if (expect == kRoot) expect = kNone;  // the root is never terminal
        if (ac.output_link(v) == expect) ++output_ok;
    }
    out.write("\ninvariant  depth(fail(v)) < depth(v) for every non-root v: ");
    out.write(depth_ok == non_root ? "holds (" : "VIOLATED (");
    out.write_uint(depth_ok);
    out.put('/');
    out.write_uint(non_root);
    out.write(")\ninvariant  output(v) = nearest terminal node on v's fail chain: ");
    out.write(output_ok == non_root ? "holds (" : "VIOLATED (");
    out.write_uint(output_ok);
    out.put('/');
    out.write_uint(non_root);
    out.write(")\n");
}

}  // namespace ahogrep
