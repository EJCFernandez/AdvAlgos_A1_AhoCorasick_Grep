#!/usr/bin/env bash
# gen_data.sh -- download the natural-language sources, then generate corpora
# and pattern sets into bench/data/ (gitignored).
#
#   bench/gen_data.sh SIZES_MB COUNTS      e.g.  bench/gen_data.sh 1,10,100 1,10,100
#
# Usually called by run_bench.sh. Safe to rerun: books are downloaded once and
# cached, and the generated files are skipped when a stamp says they were made
# with the same parameters.
set -euo pipefail

SIZES=${1:?usage: gen_data.sh SIZES_MB COUNTS}
COUNTS=${2:?usage: gen_data.sh SIZES_MB COUNTS}

BENCH_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DATA=$BENCH_DIR/data
BOOKS_DIR=$DATA/gutenberg
mkdir -p "$BOOKS_DIR"

# Public-domain Project Gutenberg ebook ids, in concatenation order: Pride and
# Prejudice, Moby Dick, Frankenstein, Sherlock Holmes, A Tale of Two Cities,
# Dracula, Jane Eyre, Huckleberry Finn, Alice in Wonderland, Tom Sawyer.
# Roughly 7 MB together; the corpus repeats them to reach 100 MB.
BOOK_IDS=(1342 2701 84 1661 98 345 1260 76 11 74)

books=()
download_ok=1
if command -v curl >/dev/null; then
    for id in "${BOOK_IDS[@]}"; do
        f=$BOOKS_DIR/pg$id.txt
        if [[ ! -s $f ]]; then
            echo "downloading Gutenberg #$id"
            if ! curl -fsSL --retry 2 --max-time 60 -o "$f.part" \
                    "https://www.gutenberg.org/cache/epub/$id/pg$id.txt"; then
                rm -f "$f.part"
                download_ok=0
                break
            fi
            mv "$f.part" "$f"
        fi
        books+=("$f")
    done
else
    download_ok=0
fi

if (( download_ok )); then
    NATURAL_SOURCE="gutenberg"
else
    # All-or-nothing: a corpus made of whichever books happened to arrive would
    # differ from run to run, which is worse than a clearly labelled fallback.
    echo "WARNING: Gutenberg download failed; using the synthetic Zipf text instead." >&2
    NATURAL_SOURCE="synthetic-zipf"
    books=()
fi

# Recorded for env.txt: which natural source was used, and the exact bytes.
{
    echo "natural_source: $NATURAL_SOURCE"
    for f in "${books[@]}"; do sha256sum "$f"; done
} > "$DATA/sources.txt"

for kind in natural random; do
    stamp=$DATA/.stamp_$kind
    # Bump the version tag whenever gen_data.py changes what it writes.
    want="sizes=$SIZES counts=$COUNTS source=$NATURAL_SOURCE v2"
    [[ $kind == random ]] && want="sizes=$SIZES counts=$COUNTS v2"
    if [[ -f $stamp && $(cat "$stamp") == "$want" ]]; then
        echo "$kind data up to date ($want)"
        continue
    fi
    echo "generating $kind data ($want)"
    if [[ $kind == natural ]]; then
        python3 "$BENCH_DIR/gen_data.py" --out "$DATA" --kind natural \
            --sizes-mb "$SIZES" --counts "$COUNTS" --books "${books[@]}"
    else
        python3 "$BENCH_DIR/gen_data.py" --out "$DATA" --kind random \
            --sizes-mb "$SIZES" --counts "$COUNTS"
    fi
    echo "$want" > "$stamp"
done
