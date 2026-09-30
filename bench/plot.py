#!/usr/bin/env python3
"""plot.py -- PNG plots and a Markdown summary from one results directory.

    python3 bench/plot.py bench/results/full

Reads timings.csv, construction.csv, memory.csv and locale.csv (whichever
exist) and writes, into the same directory:

    time_vs_patterns.png        wall time vs pattern count, log-log, one panel
                                per (text type, file size)
    time_<text>_<size>MB.png    the same panels as separate images
    throughput.png              MB/s (10^6 bytes/s) vs pattern count
    construction.png            time on an empty input vs pattern count
    memory.png                  peak RSS vs pattern count, plus ahogrep's
                                transition-table size
    summary.md                  the numbers behind the plots, as tables

Every time shown is the MEDIAN of the hyperfine runs, not the mean. With only
a few runs per point, one slow outlier (a page-cache refill, a WSL hiccup)
drags the mean, and a +-stddev bar can then reach zero on a log axis. The
CSVs keep mean and stddev too.

Only the standard library and matplotlib are needed (no pandas).
"""

import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # no display in WSL; write files only
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter  # noqa: E402

# Colour follows the tool, the same in every figure. The hues are slots 1-4 of
# a colour-blind-validated categorical palette, in its validated order; each
# tool also gets its own marker so identity never rests on colour alone.
STYLE = {
    "ahogrep":   dict(color="#2a78d6", marker="o", label="ahogrep -c"),
    "grep-F":    dict(color="#eb6834", marker="s", label="grep -F -c (LC_ALL=C)"),
    "rg-F":      dict(color="#1baf7a", marker="^", label="rg -F -c"),
    "grep-loop": dict(color="#eda100", marker="D", label="grep once per pattern (<= 100 patterns)"),
}
TOOLS = list(STYLE)

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"

plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID,
    "axes.labelcolor": INK_2,
    "axes.titlecolor": INK,
    "axes.titlesize": 11,
    "axes.labelsize": 9,
    "xtick.color": INK_2,
    "ytick.color": INK_2,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.6,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "legend.frameon": False,
    "legend.fontsize": 9,
    "font.size": 9,
    "text.color": INK,
})


def read_csv(path: Path) -> list:
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def median_s(r) -> float:
    return float(r["median_s"])


def series(rows, tool, value):
    """(pattern counts, values) for one tool, sorted by pattern count."""
    pts = sorted((int(r["patterns"]), value(r)) for r in rows if r["tool"] == tool)
    return [p[0] for p in pts], [p[1] for p in pts]


def style_log_axes(ax, xlabel="patterns"):
    """Log-log axes. Call after plotting: the y ticks depend on the data span."""
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_locator(LogLocator(base=10))
    # Under two decades of y (typical in quick mode) powers of ten alone leave
    # one labelled tick or none, so add 2x and 5x.
    lo, hi = ax.get_ylim()
    subs = (1.0, 2.0, 5.0) if hi / lo < 100 else (1.0,)
    ax.yaxis.set_major_locator(LogLocator(base=10, subs=subs))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.set_xlabel(xlabel)
    ax.tick_params(length=0)


def draw_lines(ax, rows, value):
    for tool in TOOLS:
        xs, ys = series(rows, tool, value)
        if not xs:
            continue
        st = STYLE[tool]
        ax.plot(xs, ys, color=st["color"], marker=st["marker"], markersize=6,
                linewidth=2, label=st["label"],
                markeredgecolor=SURFACE, markeredgewidth=1.2)  # ring where marks overlap


def shared_legend(fig, axes, ncol):
    """One legend above all panels, entries in first-seen order."""
    handles = {}
    for ax in axes:
        for h, lab in zip(*ax.get_legend_handles_labels()):
            handles.setdefault(lab, h)
    fig.legend(list(handles.values()), list(handles), loc="upper center",
               ncol=min(ncol, len(handles)), bbox_to_anchor=(0.5, 1.0))


def footnote(fig, text):
    fig.text(0.5, 0.01, text, ha="center", va="bottom", fontsize=8, color=INK_2)


def grid_figure(timings, value, ylabel, fname, note):
    kinds = [k for k in ("natural", "random") if any(r["text"] == k for r in timings)]
    sizes = sorted({int(r["size_mb"]) for r in timings})
    # At least 9 in wide so the legend and footnote fit even with a single
    # column (quick mode has one file size).
    fig, axes = plt.subplots(len(kinds), len(sizes), squeeze=False,
                             figsize=(max(4.2 * len(sizes), 9), 3.6 * len(kinds) + 1.0))
    for i, kind in enumerate(kinds):
        for j, size in enumerate(sizes):
            ax = axes[i][j]
            rows = [r for r in timings if r["text"] == kind and int(r["size_mb"]) == size]
            draw_lines(ax, rows, value)
            style_log_axes(ax)
            ax.set_title(f"{kind} text, {size} MB", loc="left")
            if j == 0:
                ax.set_ylabel(ylabel)
    shared_legend(fig, axes.flat, ncol=4 if len(sizes) >= 3 else 2)
    footnote(fig, note)
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    fig.savefig(fname, dpi=150)
    plt.close(fig)


def single_panels(timings, out):
    for kind in ("natural", "random"):
        for size in sorted({int(r["size_mb"]) for r in timings}):
            rows = [r for r in timings if r["text"] == kind and int(r["size_mb"]) == size]
            if not rows:
                continue
            fig, ax = plt.subplots(figsize=(6.5, 4.6))
            draw_lines(ax, rows, median_s)
            style_log_axes(ax)
            ax.set_ylabel("wall time, median of runs (s)")
            ax.set_title(f"-c over {kind} text, {size} MB", loc="left")
            shared_legend(fig, [ax], ncol=2)
            fig.tight_layout(rect=(0, 0, 1, 0.88))
            fig.savefig(out / f"time_{kind}_{size}MB.png", dpi=150)
            plt.close(fig)


def construction_figure(rows, out):
    kinds = [k for k in ("natural", "random") if any(r["text"] == k for r in rows)]
    fig, axes = plt.subplots(1, len(kinds), squeeze=False, figsize=(5 * len(kinds), 4.5))
    for ax, kind in zip(axes[0], kinds):
        draw_lines(ax, [r for r in rows if r["text"] == kind],
                   lambda r: median_s(r) * 1000)
        style_log_axes(ax)
        ax.set_title(f"{kind}-text pattern sets", loc="left")
        ax.set_ylabel("time on an empty input (ms)")
    shared_legend(fig, axes[0], ncol=3)
    footnote(fig, "Median of runs. Includes process start-up and reading the "
                  "pattern file; no input is scanned.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    fig.savefig(out / "construction.png", dpi=150)
    plt.close(fig)


def memory_figure(rows, out):
    kinds = [k for k in ("natural", "random") if any(r["text"] == k for r in rows)]
    fig, axes = plt.subplots(1, len(kinds), squeeze=False, figsize=(5 * len(kinds), 4.8))
    mib = 1024 * 1024
    for ax, kind in zip(axes[0], kinds):
        sub = [r for r in rows if r["text"] == kind]
        draw_lines(ax, sub, lambda r: float(r["max_rss_kb"]) * 1024 / mib)
        # The table on its own: nodes x 256 x 4 bytes. Same colour as ahogrep,
        # dashed and unmarked, so it reads as "part of the blue line".
        xs, ys = series(sub, "ahogrep", lambda r: float(r["table_bytes"]) / mib)
        ax.plot(xs, ys, color=STYLE["ahogrep"]["color"], linestyle="--", linewidth=1.5,
                label="ahogrep transition table alone (nodes x 1 KiB)")
        style_log_axes(ax)
        ax.set_title(f"{kind}-text pattern sets", loc="left")
        ax.set_ylabel("peak memory (MiB)")
    shared_legend(fig, axes[0], ncol=2)
    footnote(fig, "Max RSS from /usr/bin/time -v on the 1 MB file.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.86))
    fig.savefig(out / "memory.png", dpi=150)
    plt.close(fig)


def fmt_s(x: float) -> str:
    return f"{x * 1000:.1f} ms" if x < 1 else f"{x:.2f} s"


def summary(out, timings, construction, memory, locale):
    lines = ["# Benchmark summary", ""]
    env = out / "env.txt"
    if env.exists():
        lines += ["```", env.read_text().rstrip(), "```", ""]

    if timings:
        lines += ["## Wall time (median of runs) and throughput",
                  "",
                  "grep-loop is only run up to 100 patterns.",
                  "",
                  "| text | size | patterns | " + " | ".join(TOOLS) + " |",
                  "|---|---:|---:|" + "---:|" * len(TOOLS)]
        by_cfg = defaultdict(dict)
        for r in timings:
            by_cfg[(r["text"], int(r["size_mb"]), int(r["patterns"]))][r["tool"]] = r
        for (kind, size, n), tools in sorted(by_cfg.items()):
            cells = []
            for t in TOOLS:
                r = tools.get(t)
                if r is None:
                    cells.append("-")
                else:
                    med = median_s(r)
                    mbps = int(r["size_bytes"]) / 1e6 / med
                    cells.append(f"{fmt_s(med)} ({mbps:,.0f} MB/s)")
            lines.append(f"| {kind} | {size} MB | {n:,} | " + " | ".join(cells) + " |")
        lines.append("")

    if construction:
        lines += ["## Construction (empty input, median of runs)", "",
                  "| text | patterns | ahogrep | grep-F | rg-F |", "|---|---:|---:|---:|---:|"]
        by = defaultdict(dict)
        for r in construction:
            by[(r["text"], int(r["patterns"]))][r["tool"]] = fmt_s(median_s(r))
        for (kind, n), t in sorted(by.items()):
            lines.append(f"| {kind} | {n:,} | {t.get('ahogrep', '-')} | "
                         f"{t.get('grep-F', '-')} | {t.get('rg-F', '-')} |")
        lines.append("")

    if memory:
        def mib(kb):
            return f"{int(kb) / 1024:,.1f} MiB"

        lines += ["## Peak memory", "",
                  "| text | patterns | nodes | table | ahogrep RSS | grep-F RSS | rg-F RSS |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        by = defaultdict(dict)
        for r in memory:
            by[(r["text"], int(r["patterns"]))][r["tool"]] = r
        for (kind, n), t in sorted(by.items()):
            a = t["ahogrep"]
            lines.append(f"| {kind} | {n:,} | {int(a['nodes']):,} | "
                         f"{int(a['table_bytes']) / 2**20:,.1f} MiB | {mib(a['max_rss_kb'])} | "
                         f"{mib(t['grep-F']['max_rss_kb'])} | {mib(t['rg-F']['max_rss_kb'])} |")
        lines.append("")

    if locale:
        r0 = locale[0]
        lines += [f"## Locale effect (grep -F, natural text, {r0['size_mb']} MB, "
                  f"{int(r0['patterns']):,} patterns)", "",
                  "| locale | median |", "|---|---:|"]
        for r in locale:
            lines.append(f"| {r['tool']} | {fmt_s(median_s(r))} |")
        lines.append("")

    (out / "summary.md").write_text("\n".join(lines))


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: plot.py RESULTS_DIR")
    out = Path(sys.argv[1])
    timings = read_csv(out / "timings.csv")
    construction = read_csv(out / "construction.csv")
    memory = read_csv(out / "memory.csv")
    locale = read_csv(out / "locale.csv")

    cap = "grep once per pattern is capped at 100 patterns."
    if timings:
        grid_figure(timings, median_s, "wall time (s)", out / "time_vs_patterns.png",
                    "Median of the hyperfine runs; " + cap)
        grid_figure(timings, lambda r: int(r["size_bytes"]) / 1e6 / median_s(r),
                    "throughput (MB/s)", out / "throughput.png",
                    "File size / median wall time, so start-up and construction "
                    "are included; " + cap)
        single_panels(timings, out)
    if construction:
        construction_figure(construction, out)
    if memory:
        memory_figure(memory, out)
    summary(out, timings, construction, memory, locale)
    print(f"plots and summary.md written to {out}")


if __name__ == "__main__":
    main()
