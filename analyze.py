#!/usr/bin/env python3

import csv
from collections import defaultdict
from decimal import Decimal, getcontext
from pathlib import Path


getcontext().prec = 28

ROOT = Path(__file__).resolve().parent
INPUT = ROOT / "results" / "main" / "summary.csv"
OUTPUT = ROOT / "results" / "main" / "overview.csv"

LEVELS = (1, 2, 3)
THREADS = (1, 2, 4, 8, 16)
CHUNKS = (1, 2, 4, 8, 16, 32, 64)
STRATEGIES = ("dynamic", "guided", "long-row")

FIELDS = (
    "level",
    "threads",
    "dynamic_best_chunk",
    "dynamic_best_median",
    "dynamic_second_chunk",
    "dynamic_second_median",
    "dynamic_chunk_gap_pct",
    "guided_best_chunk",
    "guided_best_median",
    "guided_second_chunk",
    "guided_second_median",
    "guided_chunk_gap_pct",
    "long_row_median",
    "best_strategy",
    "best_strategy_median",
    "second_strategy",
    "second_strategy_median",
    "strategy_gap_pct",
    "dynamic_speedup_vs_1T",
    "guided_speedup_vs_1T",
    "long_row_speedup_vs_1T",
    "chunk_close",
    "strategy_close",
)


def formatted(value: Decimal) -> str:
    text = format(value, ".12f").rstrip("0").rstrip(".")
    return text if text else "0"


def gap_pct(best: Decimal, second: Decimal) -> Decimal:
    if best <= 0:
        raise ValueError("runtime must be positive")
    return (second - best) / best * Decimal(100)


with INPUT.open(newline="", encoding="utf-8") as handle:
    source_rows = list(csv.DictReader(handle))

if len(source_rows) != 225:
    raise ValueError(f"expected 225 source rows, found {len(source_rows)}")

required = {"strategy", "level", "threads", "chunk", "median_ms"}
if not source_rows or not required.issubset(source_rows[0]):
    raise ValueError("source CSV is missing required columns")

groups = defaultdict(lambda: defaultdict(list))
seen_configs = set()
for row in source_rows:
    strategy = row["strategy"]
    level = int(row["level"])
    threads = int(row["threads"])
    chunk = int(row["chunk"]) if row["chunk"] else None
    median = Decimal(row["median_ms"])
    config = (strategy, level, threads, chunk)
    if config in seen_configs:
        raise ValueError(f"duplicate source configuration: {config}")
    seen_configs.add(config)
    groups[(level, threads)][strategy].append((chunk, median))

expected_pairs = {(level, threads) for level in LEVELS for threads in THREADS}
if set(groups) != expected_pairs:
    raise ValueError("source CSV has missing or unexpected Level x threads groups")

best_by_group = {}
for pair in sorted(expected_pairs):
    strategy_rows = groups[pair]
    if set(strategy_rows) != set(STRATEGIES):
        raise ValueError(f"missing strategy in group {pair}")

    for strategy in ("dynamic", "guided"):
        rows = strategy_rows[strategy]
        if len(rows) != 7 or {chunk for chunk, _ in rows} != set(CHUNKS):
            raise ValueError(f"invalid chunk coverage for {strategy} in group {pair}")
        ranked = sorted(rows, key=lambda item: (item[1], item[0]))
        best_by_group[(pair[0], pair[1], strategy)] = ranked[0]

    long_rows = strategy_rows["long-row"]
    if len(long_rows) != 1 or long_rows[0][0] is not None:
        raise ValueError(f"invalid long-row record in group {pair}")
    best_by_group[(pair[0], pair[1], "long-row")] = long_rows[0]

baseline = {}
for level in LEVELS:
    for strategy in STRATEGIES:
        baseline[(level, strategy)] = best_by_group[(level, 1, strategy)][1]

overview_rows = []
strategy_order = {name: index for index, name in enumerate(STRATEGIES)}
for level in LEVELS:
    for threads in THREADS:
        strategy_rows = groups[(level, threads)]
        dynamic_ranked = sorted(strategy_rows["dynamic"], key=lambda item: (item[1], item[0]))
        guided_ranked = sorted(strategy_rows["guided"], key=lambda item: (item[1], item[0]))
        long_median = strategy_rows["long-row"][0][1]

        dynamic_gap = gap_pct(dynamic_ranked[0][1], dynamic_ranked[1][1])
        guided_gap = gap_pct(guided_ranked[0][1], guided_ranked[1][1])

        strategy_ranked = sorted(
            (
                ("dynamic", dynamic_ranked[0][1]),
                ("guided", guided_ranked[0][1]),
                ("long-row", long_median),
            ),
            key=lambda item: (item[1], strategy_order[item[0]]),
        )
        strategy_gap = gap_pct(strategy_ranked[0][1], strategy_ranked[1][1])

        overview_rows.append(
            {
                "level": level,
                "threads": threads,
                "dynamic_best_chunk": dynamic_ranked[0][0],
                "dynamic_best_median": str(dynamic_ranked[0][1]),
                "dynamic_second_chunk": dynamic_ranked[1][0],
                "dynamic_second_median": str(dynamic_ranked[1][1]),
                "dynamic_chunk_gap_pct": formatted(dynamic_gap),
                "guided_best_chunk": guided_ranked[0][0],
                "guided_best_median": str(guided_ranked[0][1]),
                "guided_second_chunk": guided_ranked[1][0],
                "guided_second_median": str(guided_ranked[1][1]),
                "guided_chunk_gap_pct": formatted(guided_gap),
                "long_row_median": str(long_median),
                "best_strategy": strategy_ranked[0][0],
                "best_strategy_median": str(strategy_ranked[0][1]),
                "second_strategy": strategy_ranked[1][0],
                "second_strategy_median": str(strategy_ranked[1][1]),
                "strategy_gap_pct": formatted(strategy_gap),
                "dynamic_speedup_vs_1T": formatted(
                    baseline[(level, "dynamic")] / dynamic_ranked[0][1]
                ),
                "guided_speedup_vs_1T": formatted(
                    baseline[(level, "guided")] / guided_ranked[0][1]
                ),
                "long_row_speedup_vs_1T": formatted(
                    baseline[(level, "long-row")] / long_median
                ),
                "chunk_close": "yes" if dynamic_gap < 3 or guided_gap < 3 else "no",
                "strategy_close": "yes" if strategy_gap < 3 else "no",
            }
        )

if len(overview_rows) != 15:
    raise ValueError(f"expected 15 overview rows, found {len(overview_rows)}")

temporary = OUTPUT.with_suffix(OUTPUT.suffix + ".tmp")
with temporary.open("w", newline="", encoding="utf-8") as handle:
    writer = csv.DictWriter(handle, fieldnames=FIELDS)
    writer.writeheader()
    writer.writerows(overview_rows)
temporary.replace(OUTPUT)

print(f"rows={len(overview_rows)}")
print(f"chunk_close_yes={sum(row['chunk_close'] == 'yes' for row in overview_rows)}")
print(f"strategy_close_yes={sum(row['strategy_close'] == 'yes' for row in overview_rows)}")
