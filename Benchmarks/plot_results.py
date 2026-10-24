"""Draws the head-to-head charts from a Google Benchmark JSON file.

    RaptorCompetitorBench --benchmark_out=results.json
    python Benchmarks/plot_results.py results.json Docs/Benchmarks

Writes one PNG per workload (Scene10k.png, Scene100.png, Mesh1M.png). Each has four panels: Write, Read reuse,
Read fresh and output size, all relative to Rapt0R. Bars are medians over the repetitions, whiskers span the fastest
and slowest repetition, so overlapping whiskers mean the difference is within noise. Needs matplotlib.
"""
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator

# Dark theme. One amber accent for Rapt0R, a dimmer amber for its per-field variant, neutral gray for competitors.
# Checked against the surface: every bar >= 3:1 contrast; bar pairs >= 16 OKLab dE apart (>= 13 under protanopia).
SURFACE = "#1a1a19"
INK, INK_2, MUTED = "#f0efec", "#c3c2b7", "#898781"
GRID, AXIS = "#2c2c2a", "#383835"
ACCENT, ACCENT_SOFT, NEUTRAL = "#f5b027", "#b8781c", "#6a6964"

BASELINE = "Rapt0R"
LIBRARIES = {  # Benchmark ID -> display name, in display order
    "Rapt0R": "Rapt0R",
    "Rapt0R_per_field": "Rapt0R per-field",
    "zpp_bits": "zpp_bits",
    "bitsery": "bitsery",
    "alpaca": "alpaca",
    "cereal": "cereal",
    "glaze_beve": "glaze BEVE",
}
WORKLOADS = {  # Benchmark ID -> (title, use a log scale)
    "Scene10k": ("Scene, 10,000 entities (strings, optional, vector, unordered_map)", False),
    "Scene100": ("Scene, 100 entities", False),
    "Mesh1M": ("Mesh, 1,000,000 vertices (12 MB bulk POD array)", True),
}
OPERATIONS = [("Write", "Write"), ("ReadReuse", "Read into existing object"), ("ReadFresh", "Read into new object")]

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["Segoe UI", "Helvetica Neue", "Arial", "DejaVu Sans"],
    "font.size": 8.5,
    "text.color": INK,
    "xtick.color": MUTED,
    "ytick.color": INK,
    "axes.edgecolor": AXIS,
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
})


def load(path):
    """samples[workload][operation][library] -> per-repetition times; sizes[workload][library] -> wire bytes."""
    benchmarks = json.loads(Path(path).read_text(encoding="utf-8"))["benchmarks"]
    samples = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    sizes = defaultdict(dict)
    units = set()
    for b in benchmarks:
        if b.get("run_type") != "iteration" or b.get("error_occurred"):
            continue
        workload, operation, library = b["run_name"].removesuffix("/manual_time").split("/")
        samples[workload][operation][library].append(b["real_time"])
        sizes[workload][library] = int(b.get("wire_bytes", 0))
        units.add(b["time_unit"])
    if not samples:
        sys.exit("No per-repetition results found. Run with --benchmark_out=<file> and without "
                 "--benchmark_report_aggregates_only=true.")
    if len(units) != 1:
        sys.exit(f"Mixed time units in the file: {units}")
    return samples, sizes, units.pop()


def format_time(value, unit):
    unit = {"us": "µs"}.get(unit, unit)
    if value >= 1000:
        return f"{value:,.0f} {unit}"
    return f"{value:.3g} {unit}" if value < 10 else f"{value:.1f} {unit}"


def format_bytes(n):
    for limit, suffix in ((1 << 20, "MiB"), (1 << 10, "KiB")):
        if n >= limit:
            return f"{n / limit:.2f} {suffix}"
    return f"{n} B"


def color_for(library):
    return ACCENT if library == BASELINE else ACCENT_SOFT if library.startswith(BASELINE) else NEUTRAL


def style_axis(ax, log, top):
    if log:
        ax.set_xscale("log")
        ticks = [t for t in (0.5, 1, 2, 5, 10, 20, 50, 100, 200) if t <= top]
        ax.set_xlim(0.5, top)
        ax.xaxis.set_major_locator(FixedLocator(ticks))
        ax.xaxis.set_minor_locator(NullLocator())
    else:
        ax.set_xlim(0, top)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}×"))
    ax.grid(axis="x", color=GRID, linewidth=0.6, zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(AXIS)
    ax.tick_params(axis="y", length=0)
    ax.tick_params(axis="x", length=0, labelsize=7.5)


def label_x(value, whisker_end, log, top):
    """Labels start after the bar, its whisker or the 1.0 line, whichever is furthest right."""
    x = max(value, whisker_end, 1.0)
    return x * 1.08 if log else x + top * 0.02


def plot_workload(workload, ops, sizes, unit, out_path):
    title, log = WORKLOADS.get(workload, (workload, False))
    present = set().union(*(ops[op].keys() for op, _ in OPERATIONS if op in ops))
    libraries = [lib for lib in LIBRARIES if lib in present] + sorted(present - LIBRARIES.keys())
    if BASELINE not in libraries:
        print(f"skipping {workload}: no {BASELINE} results")
        return
    y = list(range(len(libraries)))[::-1]

    fig, axes = plt.subplots(1, 4, figsize=(13.5, 0.42 * len(libraries) + 1.2), sharey=True,
                             gridspec_kw={"width_ratios": [1, 1, 1, 0.8]})

    for ax, (op, op_title) in zip(axes, OPERATIONS):
        runs = ops.get(op, {})
        if BASELINE not in runs:
            ax.set_visible(False)
            continue
        base = statistics.median(runs[BASELINE])
        medians, lows, highs = [], [], []
        for lib in libraries:
            times = runs.get(lib)
            m = statistics.median(times) / base if times else 0.0
            medians.append(m)
            lows.append(m - min(times) / base if times else 0.0)
            highs.append(max(times) / base - m if times else 0.0)

        whisker_ends = [m + h for m, h in zip(medians, highs)]
        top = max(whisker_ends) * (3.5 if log else 1.55)
        style_axis(ax, log, top)
        ax.barh(y, medians, height=0.58, color=[color_for(lib) for lib in libraries], zorder=3)
        ax.errorbar(medians, y, xerr=[lows, highs], fmt="none", ecolor=INK_2, elinewidth=0.8, capsize=2, zorder=4)
        ax.axvline(1.0, color=INK_2, linewidth=0.9, zorder=5)
        for yi, lib, m, end in zip(y, libraries, medians, whisker_ends):
            if m > 0:
                absolute = statistics.median(runs[lib])
                ax.text(label_x(m, end, log, top), yi, f"{m:.2f}×  {format_time(absolute, unit)}",
                        va="center", ha="left", fontsize=7.2, color=INK_2, zorder=6)
        ax.set_title(op_title, fontsize=9, loc="left", fontweight="semibold", pad=6)

    # Output size, relative to Rapt0R (bitsery's compact sizes and glaze's field names show up here)
    ax = axes[3]
    base_size = sizes[BASELINE]
    rel = [sizes.get(lib, 0) / base_size for lib in libraries]
    top = max(rel) * 1.9
    style_axis(ax, False, top)
    ax.barh(y, rel, height=0.58, color=[color_for(lib) for lib in libraries], zorder=3)
    ax.axvline(1.0, color=INK_2, linewidth=0.9, zorder=5)
    for yi, lib, r in zip(y, libraries, rel):
        if r > 0:
            ax.text(max(r, 1.0) + top * 0.02, yi, format_bytes(sizes[lib]), va="center", ha="left", fontsize=7.2, color=INK_2)
    ax.set_title("Output size", fontsize=9, loc="left", fontweight="semibold", pad=6)

    axes[0].set_yticks(y)
    axes[0].set_yticklabels([LIBRARIES.get(lib, lib) for lib in libraries])
    fig.suptitle(title, x=0.01, ha="left", fontsize=10.5, fontweight="bold", color=INK)
    reps = len(ops[OPERATIONS[0][0]][BASELINE])
    fig.text(0.01, 0.01, f"Relative to Rapt0R (1.00×); right of the line is slower or larger. Bars: median of {reps} "
             f"repetitions, whiskers: fastest to slowest repetition. Labels: ratio and median time."
             + ("  Log scale." if log else ""), fontsize=7, color=MUTED)
    fig.tight_layout(rect=(0, 0.04, 1, 0.97))
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    print("wrote", out_path)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    samples, sizes, unit = load(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for workload in [w for w in WORKLOADS if w in samples] + sorted(samples.keys() - WORKLOADS.keys()):
        plot_workload(workload, samples[workload], sizes[workload], unit, out_dir / f"{workload}.png")


if __name__ == "__main__":
    main()
