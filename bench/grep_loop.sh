#!/usr/bin/env bash
# grep_loop.sh PATTERNS FILE -- the "no multi-pattern algorithm" baseline: one
# grep process, and so one full pass over FILE, per pattern.
#
# The cost is (number of patterns) x (one grep pass), which is why run_bench.sh
# caps this at 100 patterns. The per-pattern counts are printed but mean nothing
# summed (a line can match several patterns); it is only timed, never checked.
pats=$1
file=$2
while IFS= read -r p || [[ -n $p ]]; do
    LC_ALL=C grep -F -c -e "$p" -- "$file"
done < "$pats"
exit 0  # grep exits 1 for a pattern with no matching line; that isn't a failure
