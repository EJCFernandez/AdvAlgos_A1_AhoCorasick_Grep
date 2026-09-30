#!/usr/bin/env bash
# run_bench.sh -- the whole benchmark: build, generate data, verify, time, plot.
#
#   bench/run_bench.sh quick    trial run: 1 MB files, up to 1000 patterns (< 1 min)
#   bench/run_bench.sh full     1/10/100 MB, up to 100,000 patterns (~15 min)
#
# Results go to bench/results/<mode>/ (the previous results of that mode are
# replaced). Run it on Linux (WSL2 is fine), from a clone on the Linux
# filesystem, NOT under /mnt/c: cross-filesystem I/O would distort the timings.
set -euo pipefail

MODE=${1:-}
case $MODE in
    quick)
        SIZES=(1)
        COUNTS=(1 10 100 1000)
        LOCALE_SIZE=1
        ;;
    full)
        SIZES=(1 10 100)
        COUNTS=(1 10 100 1000 10000 100000)
        LOCALE_SIZE=10
        ;;
    *)
        echo "usage: $0 quick|full" >&2
        exit 2
        ;;
esac
LOOP_CAP=100        # grep-per-pattern loop only up to this many patterns
LOCALE_COUNT=1000   # pattern count for the single UTF-8-locale grep run

# Every grep below runs in the C locale unless it says otherwise. UTF-8
# locales make grep decode multibyte characters, which is a different (slower)
# job from the byte matching ahogrep does; see the separate locale run.
export LC_ALL=C

BENCH_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$BENCH_DIR")
DATA=$BENCH_DIR/data
RES=$BENCH_DIR/results/$MODE
AHO=$REPO/build/ahogrep
START=$(date +%s)

die() {
    {
        echo
        echo "################################ BENCHMARK ABORTED ################################"
        printf '  %s\n' "$@"
        echo "###################################################################################"
    } >&2
    exit 1
}

# ------------------------------------------------------------- preflight ------

need() { command -v "$1" >/dev/null || die "$1 not found. Install it: $2"; }
need hyperfine "sudo apt install hyperfine"
need rg        "sudo apt install ripgrep"
need python3   "sudo apt install python3"
need cmake     "sudo apt install cmake"
need g++       "sudo apt install g++"
# The shell keyword `time` has no -v; GNU time is a separate package.
[[ -x /usr/bin/time ]] || die "/usr/bin/time not found (needed for peak memory)." \
                              "Install it with: sudo apt install time"

# hyperfine -N splits commands on whitespace, so paths must not contain any.
[[ $REPO != *" "* ]] || die "The repository path contains a space: $REPO"
case $REPO in
    /mnt/*) echo "WARNING: $REPO is on a Windows drive; timings will include" \
                 "cross-filesystem overhead. Clone to ~/ahogrep instead." >&2 ;;
esac

# ------------------------------------------------------------------ build -----

echo "== building ahogrep (Release)"
cmake -S "$REPO" -B "$REPO/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$REPO/build" --target ahogrep -j "$(nproc)" >/dev/null
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$REPO/build/CMakeCache.txt" \
    || die "build/ is not a Release build; timings would be meaningless."

# ------------------------------------------------------------------- data -----

echo "== generating data"
join() { local IFS=,; echo "$*"; }
bash "$BENCH_DIR/gen_data.sh" "$(join "${SIZES[@]}")" "$(join "${COUNTS[@]}")"
EMPTY=$DATA/empty.txt
: > "$EMPTY"

rm -rf "$RES"
mkdir -p "$RES/json"

# ------------------------------------------------------------ environment -----

first_line() { "$@" 2>&1 | head -n1 || true; }
{
    echo "mode:        $MODE"
    echo "date:        $(date -Is)"
    echo "cpu:         $(lscpu | sed -n 's/^Model name: *//p')"
    echo "cpus:        $(nproc)"
    echo "memory:      $(free -h | awk '/^Mem:/ {print $2}')"
    echo "kernel:      $(uname -r)"
    echo "os:          $(. /etc/os-release && echo "$PRETTY_NAME")"
    echo "gcc:         $(first_line gcc --version)"
    echo "cmake:       $(first_line cmake --version)"
    echo "grep:        $(first_line grep --version)"
    echo "rg:          $(first_line rg --version)"
    echo "hyperfine:   $(first_line hyperfine --version)"
    echo "python:      $(first_line python3 --version)"
    echo "ahogrep git: $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet || echo ' (uncommitted changes)')"
    echo "build type:  Release"
    echo "sizes (MB):  ${SIZES[*]}"
    echo "patterns:    ${COUNTS[*]}"
    echo "grep loop:   up to $LOOP_CAP patterns only"
    echo "grep locale: LC_ALL=C for every grep run except the separate locale comparison"
    cat "$DATA/sources.txt"
} > "$RES/env.txt"
cat "$RES/env.txt"

# ---------------------------------------------------------------- helpers -----

# hyperfine settings per file size. hyperfine does at least --min-runs, keeps
# going until about 3 s have passed, and stops at --max-runs. The 100 MB cap
# is what keeps full mode near 15 minutes: those runs take up to ~8 s each.
# (Projected on the dev laptop from one timed run of every command: 10 min
# with 5 runs at 100 MB; 10 runs was chosen for steadier medians.)
hf_opts() {
    if [[ $MODE == quick ]]; then
        echo "--warmup 1 --min-runs 3 --max-runs 5"
        return
    fi
    case $1 in
        1)  echo "--warmup 3 --min-runs 10 --max-runs 30" ;;
        10) echo "--warmup 2 --min-runs 10 --max-runs 15" ;;
        *)  echo "--warmup 1 --min-runs 10 --max-runs 10" ;;
    esac
}

# -N: no intermediate shell, so no shell start-up to subtract.
# -i: exit status 1 ("no line matched") is a normal result, not a failure;
#     real failures (exit 2) are caught by count_of() before anything is timed.
# --output=pipe: NOT hyperfine's default of /dev/null. GNU grep checks whether
#     stdout is /dev/null and, if so, stops at the first match, since only the
#     exit status could matter. Measured here: grep -F -c "scanned" 100 MB in
#     ~1 ms that way, against 1-8 s through a pipe. A pipe that hyperfine
#     drains and discards gives every tool the same, real, workload.
hyperfine_run() {
    local json=$1; shift
    hyperfine -N -i --style basic --output=pipe --export-json "$json" "$@"
}

# Runs a counting command and prints its count. Dies on exit status >= 2 or on
# output that isn't a number. rg -c prints nothing at all when no line
# matches, so empty output counts as 0.
count_of() {
    local out rc
    set +e
    out=$("$@" 2>"$RES/stderr.tmp")
    rc=$?
    set -e
    (( rc <= 1 )) || die "command failed (exit $rc): $*" "$(cat "$RES/stderr.tmp")"
    out=${out:-0}
    [[ $out =~ ^[0-9]+$ ]] || die "unexpected output from: $*" "$out"
    echo "$out"
}

# ---------------------------------------------------------- main timings ------

TIMINGS=$RES/timings.csv
VERIFY=$RES/verify.csv
echo "text,size_mb,size_bytes,patterns,ahogrep,grep_F,rg_F" > "$VERIFY"

total=$(( 2 * ${#SIZES[@]} * ${#COUNTS[@]} ))
k=0
for kind in natural random; do
    for size in "${SIZES[@]}"; do
        file=$DATA/${kind}_${size}MB.txt
        bytes=$(stat -c %s "$file")
        for n in "${COUNTS[@]}"; do
            k=$((k + 1))
            pats=$DATA/patterns/${kind}_${n}.txt
            echo
            echo "== [$k/$total] $kind ${size}MB, $n patterns"

            # Correctness first: all three must report the same number of
            # matching lines, or the timings below compare different work.
            a=$(count_of "$AHO" -c -f "$pats" "$file")
            g=$(count_of grep -F -c -f "$pats" "$file")
            r=$(count_of rg --no-config -F -c -f "$pats" "$file")
            echo "   matching lines: ahogrep=$a grep-F=$g rg-F=$r"
            echo "$kind,$size,$bytes,$n,$a,$g,$r" >> "$VERIFY"
            if [[ $a != "$g" || $a != "$r" ]]; then
                die "COUNT MISMATCH on $kind ${size}MB with $n patterns" \
                    "ahogrep -c: $a" "grep -F -c: $g" "rg -F -c: $r" \
                    "patterns: $pats" "file: $file"
            fi

            cmds=(-n ahogrep "$AHO -c -f $pats $file"
                  -n grep-F  "grep -F -c -f $pats $file"
                  -n rg-F    "rg --no-config -F -c -f $pats $file")
            if (( n <= LOOP_CAP )); then
                cmds+=(-n grep-loop "bash $BENCH_DIR/grep_loop.sh $pats $file")
            fi
            json=$RES/json/${kind}_${size}MB_${n}.json
            # shellcheck disable=SC2046  # hf_opts is meant to word-split
            hyperfine_run "$json" $(hf_opts "$size") "${cmds[@]}"
            python3 "$BENCH_DIR/record.py" "$json" "$TIMINGS" \
                text="$kind" size_mb="$size" size_bytes="$bytes" patterns="$n"
        done
    done
done

# ------------------------------------------------------------ construction ----
# On an empty input file the run is process start-up + reading the pattern file
# + building the matcher, with no scanning at all. The same is measured for
# grep and rg so ahogrep's number has a reference point.

echo
echo "== construction time (empty input file)"
for kind in natural random; do
    for n in "${COUNTS[@]}"; do
        pats=$DATA/patterns/${kind}_${n}.txt
        json=$RES/json/construct_${kind}_${n}.json
        # shellcheck disable=SC2046
        hyperfine_run "$json" $(hf_opts 1) \
            -n ahogrep "$AHO -c -f $pats $EMPTY" \
            -n grep-F  "grep -F -c -f $pats $EMPTY" \
            -n rg-F    "rg --no-config -F -c -f $pats $EMPTY"
        python3 "$BENCH_DIR/record.py" "$json" "$RES/construction.csv" \
            text="$kind" patterns="$n"
    done
done

# ------------------------------------------------------------------ memory ----
# Peak RSS on the 1 MB file, so the input contributes little (ahogrep streams
# in 1 MiB chunks; rg may mmap the file, which counts towards its RSS).

echo
echo "== peak memory"
MEM=$RES/memory.csv
echo "text,patterns,tool,max_rss_kb,nodes,table_bytes" > "$MEM"
max_rss() {
    # Through `cat`, not straight to /dev/null, for the grep reason given at
    # hyperfine_run: grep would stop at the first match.
    { /usr/bin/time -v -o "$RES/time.tmp" "$@" 2>/dev/null || true; } | cat >/dev/null
    sed -n 's/^\s*Maximum resident set size (kbytes): //p' "$RES/time.tmp"
}
for kind in natural random; do
    file=$DATA/${kind}_1MB.txt
    for n in "${COUNTS[@]}"; do
        pats=$DATA/patterns/${kind}_${n}.txt
        # The first line of --dump-automaton is
        #   patterns: N, nodes: M (transition table B bytes)
        # head closes the pipe after one line; ahogrep's SIGPIPE is expected.
        hdr=$( ("$AHO" --dump-automaton -f "$pats" 2>/dev/null || true) | head -n1)
        nodes=$(sed -n 's/.*nodes: \([0-9]*\).*/\1/p' <<< "$hdr")
        table=$(sed -n 's/.*transition table \([0-9]*\) bytes.*/\1/p' <<< "$hdr")
        [[ -n $nodes && -n $table ]] || die "could not read the --dump-automaton header" "$hdr"

        m_a=$(max_rss "$AHO" -c -f "$pats" "$file")
        m_g=$(max_rss grep -F -c -f "$pats" "$file")
        m_r=$(max_rss rg --no-config -F -c -f "$pats" "$file")
        echo "$kind,$n,ahogrep,$m_a,$nodes,$table" >> "$MEM"
        echo "$kind,$n,grep-F,$m_g,," >> "$MEM"
        echo "$kind,$n,rg-F,$m_r,," >> "$MEM"
        printf '   %-7s %6d patterns: %8d nodes, table %8s MiB | max RSS KiB: ahogrep %d, grep %d, rg %d\n' \
            "$kind" "$n" "$nodes" "$(awk -v b="$table" 'BEGIN { printf "%.1f", b / 1048576 }')" \
            "$m_a" "$m_g" "$m_r"
    done
done

# ------------------------------------------------------------------ locale ----
# One configuration, grep -F in the C locale vs a UTF-8 locale. Prefer
# en_US.UTF-8 (the usual desktop default); C.UTF-8 ships with every Ubuntu.

echo
echo "== locale effect on grep -F"
utf8=$(locale -a | grep -ix 'en_US.utf8' | head -n1 || true)
[[ -n $utf8 ]] || utf8=$(locale -a | grep -ix 'C.utf8' | head -n1 || true)
if [[ -z $utf8 ]]; then
    echo "WARNING: no UTF-8 locale installed; skipping the locale run." >&2
else
    file=$DATA/natural_${LOCALE_SIZE}MB.txt
    pats=$DATA/patterns/natural_${LOCALE_COUNT}.txt
    c1=$(count_of grep -F -c -f "$pats" "$file")
    c2=$(count_of env LC_ALL="$utf8" grep -F -c -f "$pats" "$file")
    echo "   matching lines: LC_ALL=C $c1, LC_ALL=$utf8 $c2"
    # Not fatal: the point of this run is the time, and a UTF-8 grep may treat
    # an invalid byte sequence differently. But say so if it happens.
    [[ $c1 == "$c2" ]] || echo "WARNING: the two locales disagree on the count" >&2
    json=$RES/json/locale.json
    # shellcheck disable=SC2046
    hyperfine_run "$json" $(hf_opts "$LOCALE_SIZE") \
        -n "grep-F LC_ALL=C" "grep -F -c -f $pats $file" \
        -n "grep-F LC_ALL=$utf8" "env LC_ALL=$utf8 grep -F -c -f $pats $file"
    python3 "$BENCH_DIR/record.py" "$json" "$RES/locale.csv" \
        text=natural size_mb="$LOCALE_SIZE" patterns="$LOCALE_COUNT"
fi

rm -f "$RES/stderr.tmp" "$RES/time.tmp"
elapsed=$(( $(date +%s) - START ))
echo "elapsed:     $((elapsed / 60)) min $((elapsed % 60)) s" >> "$RES/env.txt"

# ------------------------------------------------------------------- plots ----

echo
if python3 -c "import matplotlib" 2>/dev/null; then
    python3 "$BENCH_DIR/plot.py" "$RES"
else
    echo "matplotlib is not installed, so no plots yet. Install it and plot with:"
    echo "    sudo apt install python3-matplotlib"
    echo "    python3 bench/plot.py $RES"
fi
echo
echo "done in $((elapsed / 60)) min $((elapsed % 60)) s; results in $RES"
