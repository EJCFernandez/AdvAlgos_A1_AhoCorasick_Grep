# bench

Benchmarks for ahogrep against GNU grep, ripgrep and a grep-per-pattern loop.
The scripts are bash plus Python and run on Linux. The reference setup is WSL2
Ubuntu on the development laptop, with the repo cloned to `~/ahogrep`.

```
bench/run_bench.sh quick     # trial run: 1 MB files, 1-1000 patterns; < 1 min
bench/run_bench.sh full      # 1/10/100 MB, 1-100,000 patterns; ~15 min
```

The full mode's ~15 minutes is a projection, not a finished run. Every
command was timed once on the dev laptop (Core Ultra 7 258V, WSL2) and scaled
by the run counts in `hf_opts`, which gave 10 min with 5 runs per 100 MB point.
The script now does 10 runs per point, which gives about 15 min. Allow up to 20.

## Requirements

```
sudo apt install hyperfine ripgrep time python3-matplotlib cmake g++ curl
```

- `time` is GNU time, `/usr/bin/time`. The shell's built-in `time` has no `-v`,
  so peak memory needs this package. The script checks for it and stops with
  this hint if it is missing.
- `python3-matplotlib` is only needed for the plots. Without it the benchmark
  still runs and tells you how to plot afterwards.
- Clone the repo onto the Linux filesystem (`~/...`), not `/mnt/c/...`.
  Cross-filesystem I/O through WSL's 9P bridge would dominate the timings. The
  script warns if it finds itself under `/mnt/`.

## What it does

`run_bench.sh` does everything in order: it builds a Release ahogrep,
generates the data, records the environment, then verifies and times each
configuration, measures construction and memory, runs the locale comparison,
and finally plots.

### Data (`gen_data.sh`, `gen_data.py`, written to `bench/data/`, gitignored)

- **Natural text:** ten public-domain Project Gutenberg books, downloaded
  once with `curl`. The licence header and footer are stripped and CRLF is
  converted to LF. The books (~7 MB together) are concatenated and repeated to
  reach 100 MB. If any download fails, the script falls back to a synthetic
  text with Zipf-distributed made-up words (`natural_source` in `env.txt` says
  which was used).
- **Random text:** uniform lowercase letters with about 8.6% spaces, in lines
  of 40–120 bytes.
- **Sizes:** each smaller file is a prefix of the larger one, cut at a line
  boundary: `<kind>_1MB.txt` ⊂ `_10MB` ⊂ `_100MB`.
- **Patterns** (`patterns/<kind>_<N>.txt`, N = 1, 10, … 100,000): real and
  random entries alternate.
  - Real entries are distinct words of 4–16 letters sampled from the 1 MB
    file. That file is a prefix of every size, so each real entry occurs in
    every file. The 100k sets need more distinct strings than 1 MB of text
    has words, so they are topped up with two-word phrases taken literally
    from the text (e.g. `his hand`).
  - Random entries are random lowercase strings, each the same length as the
    real entry before it. Most don't occur, though short ones sometimes do.
  - The sets are nested (the 10-set is the first 10 lines of the 100-set,
    and so on), and the 1-pattern set is a single real word.
- **Determinism:** all randomness is seeded. Quick and full mode produce
  byte-identical 1 MB files and pattern sets. The Gutenberg downloads are the
  one outside input, and their sha256 sums are recorded in `env.txt`.

### Commands compared

All counts are of matching lines (`-c`), so output cost does not dominate:

| name | command |
| --- | --- |
| `ahogrep` | `ahogrep -c -f PATTERNS FILE` |
| `grep-F` | `grep -F -c -f PATTERNS FILE` with `LC_ALL=C` |
| `rg-F` | `rg --no-config -F -c -f PATTERNS FILE` |
| `grep-loop` | `grep -F -c -e P FILE` once per pattern (`grep_loop.sh`), **only up to 100 patterns** |

The grep loop makes one full pass over the file per pattern. At 1,000
patterns on 100 MB that is well over a minute per run, so it is capped at 100
patterns, and the plots and summary say so. `--no-config` stops a user's
`RIPGREP_CONFIG_PATH` from changing rg's behaviour.

### Method

- **Agreement first.** Before each configuration is timed, ahogrep, grep -F
  and rg must report the same count. On a mismatch the script aborts with a
  banner showing the three counts. The counts are kept in `verify.csv`.
- **hyperfine**, with no intermediate shell (`-N`):
  - 1 MB: 3 warmup runs, then 10–30 timed runs
  - 10 MB: 2 warmup runs, then 10–15 timed runs
  - 100 MB: 1 warmup run, then 10 timed runs
  - quick mode: 1 warmup run, then 3–5 timed runs
- **Output goes to a pipe, not `/dev/null`.** GNU grep notices when stdout is
  `/dev/null` and stops at the first match, because only its exit status
  could matter. With hyperfine's default `/dev/null` output, `grep -F -c`
  appeared to scan 100 MB in about 1 ms. `--output=pipe` gives every tool the
  real workload.
- **Exit status 1 is not a failure** (`-i`): it only means no line matched.
  Real errors (exit 2) are caught by the agreement check before timing.
- **Construction time:** each tool is run on an empty input file, so the time
  is process start-up plus reading the patterns plus building the matcher.
- **Peak memory:** max RSS from `/usr/bin/time -v` on the 1 MB file, for all
  three tools. ahogrep's node count and transition-table size come from the
  first line of `ahogrep --dump-automaton`. No extra flag was added for this.
- **Locale:** `grep -F` runs once more under a UTF-8 locale on one
  configuration: natural text, 1,000 patterns, 10 MB in full mode (1 MB in
  quick mode). It uses `en_US.UTF-8` if installed, else `C.UTF-8`; this WSL
  install only has `C.UTF-8` (`sudo locale-gen en_US.UTF-8` adds the other).
- **Plots use medians.** With few runs per point, one slow outlier moves the
  mean a lot. The CSVs keep mean, stddev, min and max as well.

## Output (`bench/results/<mode>/`)

| file | contents |
| --- | --- |
| `env.txt` | CPU, kernel, OS, gcc/cmake/grep/rg/hyperfine/python versions, git commit, data source and sha256, elapsed time |
| `verify.csv` | the three tools' `-c` counts per configuration |
| `timings.csv` | one row per (text, size, pattern count, tool): mean, stddev, median, min, max, runs |
| `construction.csv` | the same columns for the empty-input runs |
| `memory.csv` | max RSS per tool and pattern count, plus ahogrep's nodes and table bytes |
| `locale.csv` | grep -F under `LC_ALL=C` vs UTF-8 |
| `json/` | raw hyperfine exports, including every individual run time |
| `time_vs_patterns.png` | median time vs pattern count, log-log, one panel per text type and size |
| `time_<text>_<size>MB.png` | the same panels as separate images |
| `throughput.png` | MB/s (10^6 bytes/s), including start-up and construction |
| `construction.png` | empty-input time vs pattern count |
| `memory.png` | peak RSS vs pattern count, with ahogrep's table size as a dashed line |
| `summary.md` | the numbers behind the plots, as Markdown tables |

To redraw plots without re-running anything:
`python3 bench/plot.py bench/results/full`.

`bench/results/quick/` is gitignored. `bench/results/full/` is not, so the
full run's results and plots can be committed with the report.

## Files

| file | role |
| --- | --- |
| `run_bench.sh` | driver: `quick` or `full` |
| `gen_data.sh` | downloads the books, calls `gen_data.py`, skips work already done |
| `gen_data.py` | deterministic corpora and pattern sets |
| `grep_loop.sh` | the one-grep-per-pattern baseline |
| `record.py` | appends a hyperfine JSON export to a CSV |
| `plot.py` | PNGs and `summary.md` from a results directory |
