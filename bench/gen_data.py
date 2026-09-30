#!/usr/bin/env python3
"""gen_data.py -- deterministic corpora and pattern sets for the benchmarks.

Called by gen_data.sh, which does the downloading. It writes:

    <out>/<kind>_<S>MB.txt          one corpus per size (S in --sizes-mb)
    <out>/patterns/<kind>_<N>.txt   one pattern set per count (N in --counts)

for kind = natural (Gutenberg books, or a synthetic Zipf text if no books are
given) and kind = random (uniform lowercase letters and spaces).

Design choices that matter for the numbers:

* Every smaller corpus is a PREFIX of the largest one, cut at a line boundary.
  So the 1 MB file is inside the 10 MB file, which is inside the 100 MB file.
* Real-match patterns are sampled from the smallest corpus. Because of the
  prefix property, each of them occurs in every size, not only in the big file.
* Pattern sets are nested: the N-pattern set is the first N lines of the
  largest set. Lines alternate real, random, real, random, ..., so every prefix
  is half and half, and the 1-pattern set is a single real word.
* All randomness comes from random.Random(seed), so a rerun gives byte-identical
  files. The downloaded Gutenberg text is the one input that is not under our
  control; gen_data.sh records its sha256 so a changed download is noticed.
"""

import argparse
import random
import re
import sys
from pathlib import Path

MB = 1_000_000  # decimal megabytes, so "MB/s" in the plots means 10^6 bytes/s

WORD_MIN, WORD_MAX = 4, 16   # word lengths for real-match patterns
PHRASE_MAX = 24              # two-word phrases, used only if words run out
ALPHA = re.compile(rb"^[A-Za-z]+$")
LOWER = b"abcdefghijklmnopqrstuvwxyz"


# ----------------------------------------------------------------- corpora ---

def strip_gutenberg(raw: bytes) -> bytes:
    """Keep only the book: drop the licence header and footer, and CRLF."""
    text = raw.replace(b"\r\n", b"\n")
    start = re.search(rb"^\*\*\* ?START OF.*$", text, re.M)
    end = re.search(rb"^\*\*\* ?END OF.*$", text, re.M)
    if start and end and start.end() < end.start():
        text = text[start.end():end.start()]
    return text.strip(b"\n") + b"\n"


def synthetic_base(rng: random.Random, target: int) -> bytes:
    """Fallback natural-ish text: Zipf-distributed made-up words, wrapped lines.

    Word r (1-based rank) has weight 1/r^1.07, close to what English shows. The
    vocabulary is built from syllables so the words look pronounceable and
    have a realistic length spread; the exact look doesn't matter for matching.
    """
    syllables = [c + v for c in "bcdfghjklmnprstvwz" for v in "aeiou"] + \
                ["th", "st", "er", "an", "in", "on", "re", "ch", "sh"]
    vocab, seen = [], set()
    while len(vocab) < 30_000:
        w = "".join(rng.choice(syllables) for _ in range(rng.choice((1, 2, 2, 3, 3, 4))))
        if w not in seen:
            seen.add(w)
            vocab.append(w)
    weights = [1.0 / (r ** 1.07) for r in range(1, len(vocab) + 1)]

    out, size, line = [], 0, []
    line_len = 0
    words = rng.choices(vocab, weights=weights, k=target // 5)  # ~6 bytes/word
    for i, w in enumerate(words):
        if i % 12 == 0:
            w = w.capitalize()
        if i % 12 == 11:
            w += "."
        if line_len + len(w) + 1 > 72:
            s = " ".join(line) + "\n"
            out.append(s)
            size += len(s)
            if size >= target:
                break
            line, line_len = [], 0
        line.append(w)
        line_len += len(w) + 1
    return "".join(out).encode("ascii")


def repeat_to(base: bytes, size: int) -> bytes:
    """Concatenate copies of `base` until at least `size` bytes."""
    reps = -(-size // len(base))
    return base * reps


def random_text(rng: random.Random, size: int) -> bytes:
    """Lowercase letters with ~8.6% spaces, lines of 40..120 bytes incl. '\\n'.

    Generated line by line, so the first K bytes are the same whatever `size`
    is: quick mode's 1 MB file is then identical to full mode's. (A first
    version drew one randbytes(size) block and placed newlines afterwards;
    the RNG stream then depended on `size`, and the two modes' 1 MB files
    differed.)
    """
    # 256 byte values -> 26 letters x 9 values each (234) + space for the other
    # 22. An exact mapping means no modulo bias to argue about.
    table = bytes(LOWER[i // 9] if i < 234 else 0x20 for i in range(256))
    lines, total = [], 0
    while total < size:
        n = rng.randint(40, 120)
        lines.append(rng.randbytes(n - 1).translate(table) + b"\n")
        total += n
    return b"".join(lines)


def prefix_at_line(data: bytes, size: int) -> bytes:
    """Longest prefix of `data` with at most `size` bytes that ends in a newline."""
    if len(data) <= size:
        return data
    cut = data.rfind(b"\n", 0, size)
    return data[:cut + 1]


# ---------------------------------------------------------------- patterns ---

def real_pool(text: bytes, rng: random.Random, need: int) -> list:
    """Distinct substrings of `text` that look like words, shuffled.

    Words come first; two-word phrases ("of the") are appended only as a top-up
    for the largest sets, when a 1 MB text has too few distinct words. A phrase
    is taken from two adjacent space-separated pieces of one line, so it is a
    literal substring of the text and still a guaranteed match.
    """
    words = set()
    for w in re.findall(rb"[A-Za-z]+", text):
        if WORD_MIN <= len(w) <= WORD_MAX:
            words.add(w)
    words = sorted(words)  # sets iterate in hash order; sort for determinism
    rng.shuffle(words)
    if len(words) >= need:
        return words[:need]

    phrases = set()
    for line in text.split(b"\n"):
        pieces = line.split(b" ")
        for a, b in zip(pieces, pieces[1:]):
            if ALPHA.match(a) and ALPHA.match(b) and len(a) + 1 + len(b) <= PHRASE_MAX:
                phrases.add(a + b" " + b)
    phrases = sorted(phrases)
    rng.shuffle(phrases)
    pool = words + phrases
    if len(pool) < need:
        sys.exit(f"gen_data.py: only {len(pool)} distinct words/phrases, need {need}")
    return pool[:need]


def pattern_list(text: bytes, rng: random.Random, total: int) -> list:
    """`total` patterns alternating real word, random string of the same length."""
    real = real_pool(text, rng, (total + 1) // 2)
    out = []
    for w in real:
        out.append(w)
        # Random lowercase of the same length. "Non-match" is overwhelmingly
        # likely for length >= 5 but not guaranteed for the shortest ones,
        # especially in the random corpus; the benchmark doesn't depend on it.
        out.append(bytes(rng.choice(LOWER) for _ in range(len(w))))
    return out[:total]


# -------------------------------------------------------------------- main ---

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--kind", required=True, choices=("natural", "random"))
    ap.add_argument("--sizes-mb", required=True, help="comma list, e.g. 1,10,100")
    ap.add_argument("--counts", required=True, help="comma list, e.g. 1,10,100")
    ap.add_argument("--books", nargs="*", default=[], type=Path,
                    help="downloaded Gutenberg .txt files; none -> synthetic text")
    ap.add_argument("--seed", type=int, default=20260930)
    args = ap.parse_args()

    sizes = sorted(int(s) for s in args.sizes_mb.split(","))
    counts = sorted(int(c) for c in args.counts.split(","))
    # Separate streams per purpose, so adding a size doesn't change the patterns.
    corpus_rng = random.Random(f"{args.seed}-{args.kind}-corpus")
    pattern_rng = random.Random(f"{args.seed}-{args.kind}-patterns")

    largest = sizes[-1] * MB
    if args.kind == "natural":
        if args.books:
            base = b"".join(strip_gutenberg(p.read_bytes()) for p in args.books)
        else:
            base = synthetic_base(corpus_rng, 8 * MB)
        full = repeat_to(base, largest)
    else:
        full = random_text(corpus_rng, largest)

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "patterns").mkdir(exist_ok=True)
    smallest_text = None
    for s in sizes:
        data = prefix_at_line(full, s * MB)
        (args.out / f"{args.kind}_{s}MB.txt").write_bytes(data)
        if smallest_text is None:
            smallest_text = data
        print(f"  {args.kind}_{s}MB.txt  {len(data):>11,} bytes")

    # Always draw the full 100k list, then write prefixes: that way quick mode's
    # 1000-pattern set is byte-identical to full mode's, not merely similar.
    pats = pattern_list(smallest_text, pattern_rng, max(counts[-1], 100_000))
    for p in pats:
        # ahogrep rejects empty patterns, and a pattern file can't hold "\n".
        assert p and b"\n" not in p and b"\r" not in p, p
    for n in counts:
        (args.out / "patterns" / f"{args.kind}_{n}.txt").write_bytes(
            b"".join(p + b"\n" for p in pats[:n]))
    print(f"  patterns/{args.kind}_{{{','.join(map(str, counts))}}}.txt")


if __name__ == "__main__":
    main()
