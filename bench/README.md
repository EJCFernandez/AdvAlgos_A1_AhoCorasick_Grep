# bench

Data generation and benchmark scripts (bash, run on Linux against a Release
build). Not written yet -- checklist items 9 and 10. See the benchmark plan in
CLAUDE.md for what belongs here:

- gen_data.sh      natural-language and random corpora at ~1 MB / 10 MB / 100 MB
- gen_patterns.sh  pattern sets of 1 / 10 / 100 / 1k / 10k, mixing words sampled
                   from the corpus with random strings
- run_bench.sh     hyperfine comparison of ahogrep, ahogrep --dfa, grep -F -f,
                   and grep in a per-pattern loop; LC_ALL=C for grep, agreement
                   checked via -c before timing, peak RSS from /usr/bin/time -v,
                   results written to CSV

Generated corpora and results are gitignored.
