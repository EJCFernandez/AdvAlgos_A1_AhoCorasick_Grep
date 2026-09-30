#!/usr/bin/env python3
"""record.py -- append one hyperfine JSON export to a CSV, one row per command.

    record.py RESULT.json OUT.csv key=value [key=value ...]

The key=value pairs become leading columns (text type, size, pattern count...),
so the same helper serves the main timings, construction and locale CSVs. The
header is written when OUT.csv does not exist yet.
"""

import csv
import json
import sys
from pathlib import Path


def main() -> None:
    src, dst, *pairs = sys.argv[1:]
    extra = dict(p.split("=", 1) for p in pairs)
    results = json.loads(Path(src).read_text())["results"]

    fields = list(extra) + ["tool", "mean_s", "stddev_s", "median_s",
                            "min_s", "max_s", "runs"]
    new = not Path(dst).exists()
    with open(dst, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if new:
            w.writeheader()
        for r in results:
            w.writerow({**extra,
                        "tool": r["command"],  # the -n name given to hyperfine
                        "mean_s": f"{r['mean']:.6f}",
                        # stddev is null when hyperfine did a single run
                        "stddev_s": f"{r['stddev'] or 0:.6f}",
                        "median_s": f"{r['median']:.6f}",
                        "min_s": f"{r['min']:.6f}",
                        "max_s": f"{r['max']:.6f}",
                        "runs": len(r["times"])})


if __name__ == "__main__":
    main()
