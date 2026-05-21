#!/usr/bin/env python3
"""
generate_timing_report.py
Reads timing_log.csv and prints a Markdown table of P50/P95/P99 per
state transition. Exits with code 1 if any P99 >= 500 µs.

CSV columns: state_from, state_to, t_exec_us, t_comm_us, timestamp_ns

Usage:
    python generate_timing_report.py [timing_log.csv]
"""

import sys
import csv
import pathlib
import numpy as np

STATE_NAMES: dict[int, str] = {
    0: "Idle",
    1: "Calibrating",
    2: "Acquiring",
    3: "Processing",
    4: "Fault",
}

DEADLINE_US = 500  # matches imaging::DEADLINE_US in RtConfig.h


def load_csv(path: pathlib.Path) -> list[dict[str, str]]:
    """Load CSV rows from path."""
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def transition_label(row: dict[str, str]) -> str:
    """Return a human-readable transition label for a CSV row."""
    frm = STATE_NAMES.get(int(row["state_from"]), row["state_from"])
    to  = STATE_NAMES.get(int(row["state_to"]),   row["state_to"])
    return f"{frm} → {to}"


def compute_percentiles(rows: list[dict[str, str]]) -> dict[str, dict[str, object]]:
    """
    Group rows by transition label and compute P50/P95/P99 of t_exec_us.
    Returns a dict of {label: {p50, p95, p99, n}}.
    """
    buckets: dict[str, list[float]] = {}
    for row in rows:
        label = transition_label(row)
        buckets.setdefault(label, []).append(float(row["t_exec_us"]))

    result: dict[str, dict[str, object]] = {}
    for label, times in buckets.items():
        arr = np.array(times)
        result[label] = {
            "p50": float(np.percentile(arr, 50)),
            "p95": float(np.percentile(arr, 95)),
            "p99": float(np.percentile(arr, 99)),
            "n":   len(arr),
        }
    return result


def print_markdown_table(stats: dict[str, dict[str, object]]) -> None:
    """Print a Markdown table of transition timing statistics."""
    print("| Transition | N | P50 (µs) | P95 (µs) | P99 (µs) | Pass? |")
    print("|---|---|---|---|---|---|")
    for label, s in sorted(stats.items()):
        ok = "PASS" if s["p99"] < DEADLINE_US else "**FAIL**"
        print(
            f"| {label} | {s['n']} "
            f"| {s['p50']:.1f} | {s['p95']:.1f} | {s['p99']:.1f} | {ok} |"
        )


def main() -> int:
    csv_path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "timing_log.csv")

    if not csv_path.exists():
        print(f"ERROR: {csv_path} not found", file=sys.stderr)
        print(
            "Expected CSV with columns: state_from, state_to, "
            "t_exec_us, t_comm_us, timestamp_ns",
            file=sys.stderr,
        )
        return 1

    rows  = load_csv(csv_path)
    if not rows:
        print("ERROR: CSV is empty", file=sys.stderr)
        return 1

    stats = compute_percentiles(rows)
    print_markdown_table(stats)

    violations = [lbl for lbl, s in stats.items() if s["p99"] >= DEADLINE_US]
    if violations:
        print(
            f"\nFAIL: P99 >= {DEADLINE_US} µs for: {', '.join(violations)}",
            file=sys.stderr,
        )
        return 1

    print(f"\nAll P99 latencies are below {DEADLINE_US} µs — PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
