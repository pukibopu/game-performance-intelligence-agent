# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""生成确定性的合成 run 目录，作为 C++ 测试的输入与 ground truth"""

import argparse
import hashlib
import json
import math
import random
import statistics
from dataclasses import dataclass, field
from pathlib import Path

type Cell = int | float | None
type Row = dict[str, Cell]

MIB = 1024 * 1024
BASE_FRAME_MS = 16.667
FRAME_TIMING_LATENCY = 4
MARKER_CPU = 1
MARKER_ALLOC = 2

COLUMN_UNITS: dict[str, str] = {
    "frame_index": "index",
    "timestamp_ns": "ns",
    "frame_time_ms": "ms",
    "cpu_main_thread_ms": "ms",
    "cpu_render_thread_ms": "ms",
    "cpu_present_wait_ms": "ms",
    "gpu_frame_time_ms": "ms",
    "gc_alloc_bytes": "bytes",
    "gc_used_bytes": "bytes",
    "gc_reserved_bytes": "bytes",
    "gc_collection_count": "count",
    "total_used_bytes": "bytes",
    "total_reserved_bytes": "bytes",
    "system_used_bytes": "bytes",
    "draw_calls": "count",
    "setpass_calls": "count",
    "batches": "count",
    "triangles": "count",
    "vertices": "count",
    "scenario_marker": "id",
}
COLUMNS = list(COLUMN_UNITS)
V11_COLUMN_UNITS: dict[str, str] = {
    "unity_frame_count": "index",
    "cpu_frame_time_ms": "ms",
    "gc_collect_ms": "ms",
}
COLUMNS_V11 = [*COLUMNS[:-1], *V11_COLUMN_UNITS, "scenario_marker"]
ALL_UNITS = {**COLUMN_UNITS, **V11_COLUMN_UNITS}
TIMING_COLUMNS = (
    "cpu_main_thread_ms",
    "cpu_render_thread_ms",
    "cpu_present_wait_ms",
    "gpu_frame_time_ms",
)


@dataclass(frozen=True)
class Workload:
    """一次合成采集中注入的负载，帧号集合即 ground truth"""

    count: int
    cpu_spikes: frozenset[int] = frozenset()
    alloc_spikes: frozenset[int] = frozenset()
    collections: frozenset[int] = frozenset()
    warmup_spike: int | None = None
    retained_bytes_per_frame: int = 0
    timing_gaps: frozenset[int] = frozenset()
    collect_share: tuple[tuple[int, float], ...] = ()


@dataclass
class Case:
    """一个测试用例的全部输出"""

    name: str
    description: str
    csv_text: str
    rows: list[Row]
    warmup_frames: int
    development_build: bool = True
    unsupported: frozenset[str] = frozenset()
    workload: Workload | None = None
    diagnostics: list[dict[str, object]] = field(default_factory=list)
    dropped_frames: frozenset[int] = frozenset()
    fatal_error: str | None = None
    memory_growth: bool = False
    columns: list[str] = field(default_factory=lambda: list(COLUMNS))
    collection_levels: dict[int, str] = field(default_factory=dict)


def generate_rows(rng: random.Random, w: Workload) -> list[Row]:
    """逐帧生成数值，派生量全部由同一帧的帧时间推出"""
    rows: list[Row] = []
    gc_used = 48 * MIB
    collection_count = 10
    timestamp_us = 0
    for i in range(w.count):
        frame_ms = max(5.0, rng.gauss(BASE_FRAME_MS, 0.15))
        cpu_ratio = 0.72
        alloc = 0
        marker = 0
        if i == w.warmup_spike:
            frame_ms *= 5.0
        if i in w.cpu_spikes:
            frame_ms *= 3.0
            cpu_ratio = 0.92
            marker = MARKER_CPU
        if i in w.alloc_spikes:
            frame_ms *= 1.25
            alloc = 8 * MIB
            marker = MARKER_ALLOC
        if i in w.collections:
            frame_ms *= 2.5
            cpu_ratio = 0.9
            collection_count += 1
            gc_used = 48 * MIB
        gc_used += alloc + w.retained_bytes_per_frame

        frame_us = round(frame_ms * 1000)
        timestamp_us += frame_us
        frame_ms = frame_us / 1000
        steady_ms = min(frame_ms, BASE_FRAME_MS * 1.02)
        timing_missing = i >= w.count - FRAME_TIMING_LATENCY or i in w.timing_gaps
        cpu_main = round(frame_ms * cpu_ratio, 3)
        # GC.Collect 耗时按超额帧时间的给定比例生成，没有回收的帧为 0
        share = dict(w.collect_share).get(i, 0.0)
        gc_collect = round(share * (frame_ms - BASE_FRAME_MS), 3) if i in w.collections else 0.0

        rows.append(
            {
                "frame_index": i,
                "timestamp_ns": timestamp_us * 1000,
                "frame_time_ms": frame_ms,
                "cpu_main_thread_ms": None if timing_missing else cpu_main,
                "cpu_render_thread_ms": None if timing_missing else round(steady_ms * 0.38, 3),
                "cpu_present_wait_ms": None
                if timing_missing
                else round(max(0.0, frame_ms - cpu_main - 0.4), 3),
                "gpu_frame_time_ms": None if timing_missing else round(steady_ms * 0.55, 3),
                "gc_alloc_bytes": alloc,
                "gc_used_bytes": gc_used,
                "gc_reserved_bytes": 96 * MIB,
                "gc_collection_count": collection_count,
                "total_used_bytes": 512 * MIB + gc_used,
                "total_reserved_bytes": 900 * MIB,
                "system_used_bytes": 1100 * MIB + gc_used,
                "draw_calls": 148 + rng.randrange(-2, 3),
                "setpass_calls": 42,
                "batches": 120 + rng.randrange(-2, 3),
                "triangles": 250_000 + rng.randrange(-500, 500),
                "vertices": 410_000 + rng.randrange(-800, 800),
                "unity_frame_count": 1000 + i,
                "cpu_frame_time_ms": None if timing_missing else round(frame_ms * 0.99, 3),
                "gc_collect_ms": gc_collect,
                "scenario_marker": marker,
            }
        )
    return rows


def format_cell(value: Cell) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def render_line(
    row: Row, overrides: dict[str, str] | None = None, columns: list[str] = COLUMNS
) -> str:
    overrides = overrides or {}
    return ",".join(overrides.get(c, format_cell(row[c])) for c in columns)


def render_csv(rows: list[Row], columns: list[str] = COLUMNS) -> str:
    lines = [",".join(columns), *(render_line(r, None, columns) for r in rows)]
    return "\n".join(lines) + "\n"


def blank_columns(rows: list[Row], columns: frozenset[str]) -> None:
    """把不支持的列整列置空，模拟构建档位拿不到的计数器"""
    for row in rows:
        for column in columns:
            row[column] = None


def percentile(sorted_values: list[float], q: float) -> float:
    """与 numpy 默认一致的线性插值百分位"""
    h = (len(sorted_values) - 1) * q
    lo = math.floor(h)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (h - lo)


def summarize(values: list[float]) -> dict[str, float | int] | None:
    if not values:
        return None
    ordered = sorted(values)
    summary: dict[str, float | int] = {
        "count": len(values),
        "mean": statistics.fmean(values),
        "min": ordered[0],
        "max": ordered[-1],
        "p50": percentile(ordered, 0.50),
        "p95": percentile(ordered, 0.95),
        "p99": percentile(ordered, 0.99),
    }
    if len(values) >= 2:
        summary["stddev"] = statistics.stdev(values)
    return summary


def case_steady(
    name: str,
    description: str,
    seed: int,
    w: Workload,
    *,
    warmup_frames: int,
    development_build: bool = True,
    unsupported: frozenset[str] = frozenset(),
    memory_growth: bool = False,
    columns: list[str] = COLUMNS,
    collection_levels: dict[int, str] | None = None,
) -> Case:
    rows = generate_rows(random.Random(seed), w)
    blank_columns(rows, unsupported)
    return Case(
        name=name,
        description=description,
        csv_text=render_csv(rows, columns),
        rows=rows,
        warmup_frames=warmup_frames,
        development_build=development_build,
        unsupported=unsupported,
        workload=w,
        memory_growth=memory_growth,
        columns=list(columns),
        collection_levels=collection_levels or {},
    )


def frame_of(row: Row) -> int:
    return int(row["frame_index"] or 0)


def case_corrupt_rows() -> Case:
    """每种坏数据各放一处，行号和处理方式都写进 expected"""
    w = Workload(count=60)
    rows = generate_rows(random.Random(5), w)
    rows = [r for r in rows if r["frame_index"] not in (50, 51)]
    by_index = {r["frame_index"]: r for r in rows}
    overrides: dict[int, dict[str, str]] = {
        10: {"frame_time_ms": "abc"},
        14: {"draw_calls": "1e3"},
        20: {"gc_used_bytes": "nan"},
        25: {"frame_time_ms": "inf"},
        30: {"frame_time_ms": "-1.000"},
        33: {"frame_time_ms": ""},
        40: {"timestamp_ns": str(by_index[39]["timestamp_ns"])},
    }
    lines = [render_line(r, overrides.get(frame_of(r))) for r in rows]
    position = {frame_of(r): i for i, r in enumerate(rows)}
    lines[position[55]] += ",999"
    lines[-1] = lines[-1][: len(lines[-1]) // 2]
    csv_text = ",".join(COLUMNS) + "\n" + "\n".join(lines)

    def diag(frame: int, code: str, column: str | None, dropped: bool) -> dict[str, object]:
        return {"line": position[frame] + 2, "code": code, "field": column, "row_dropped": dropped}

    diagnostics = [
        diag(10, "CSV_INVALID_NUMBER", "frame_time_ms", True),
        diag(14, "CSV_INVALID_NUMBER", "draw_calls", False),
        diag(20, "CSV_INVALID_NUMBER", "gc_used_bytes", False),
        diag(25, "CSV_INVALID_NUMBER", "frame_time_ms", True),
        diag(30, "CSV_OUT_OF_RANGE", "frame_time_ms", True),
        diag(33, "CSV_MISSING_REQUIRED", "frame_time_ms", True),
        diag(40, "TIMESTAMP_NOT_INCREASING", "timestamp_ns", True),
        diag(52, "FRAME_INDEX_GAP", "frame_index", False),
        diag(55, "CSV_COLUMN_COUNT_MISMATCH", None, True),
        diag(59, "CSV_COLUMN_COUNT_MISMATCH", None, True),
    ]
    return Case(
        name="corrupt_rows",
        description="One of each malformed-row kind; the last line is truncated without a newline.",
        csv_text=csv_text,
        rows=rows,
        warmup_frames=0,
        diagnostics=diagnostics,
        dropped_frames=frozenset({10, 25, 30, 33, 40, 55, 59}),
    )


def build_cases() -> list[Case]:
    return [
        case_steady(
            "normal",
            "Steady 60 Hz frames with no injected workload.",
            1,
            Workload(count=300),
            warmup_frames=20,
        ),
        case_steady(
            "cpu_spikes",
            "CPU-bound spikes every 60 frames; a warm-up spike at frame 5 must be ignored.",
            2,
            Workload(count=300, cpu_spikes=frozenset(range(30, 300, 60)), warmup_spike=5),
            warmup_frames=20,
        ),
        case_steady(
            "gc_spikes",
            "8 MiB allocations without hitches plus two GC collections with hitches.",
            3,
            Workload(
                count=300,
                alloc_spikes=frozenset(range(40, 300, 60)),
                collections=frozenset({70, 190}),
            ),
            warmup_frames=20,
        ),
        case_steady(
            "missing_columns",
            "Non-development build: gc_alloc_bytes unsupported and a mid-run FrameTiming gap.",
            4,
            Workload(
                count=300,
                alloc_spikes=frozenset({60, 180}),
                collections=frozenset({120, 240}),
                timing_gaps=frozenset(range(200, 210)),
            ),
            warmup_frames=20,
            development_build=False,
            unsupported=frozenset({"gc_alloc_bytes"}),
        ),
        case_steady(
            "memory_growth",
            "Managed memory retained every frame with no collections.",
            7,
            Workload(count=900, retained_bytes_per_frame=64 * 1024),
            warmup_frames=20,
            memory_growth=True,
        ),
        case_steady(
            "gc_marker_timing",
            "Schema 1.1 with GC.Collect timing: one hitch is mostly GC, the other is not.",
            8,
            Workload(
                count=300,
                collections=frozenset({90, 210}),
                collect_share=((90, 0.9), (210, 0.1)),
            ),
            warmup_frames=20,
            columns=COLUMNS_V11,
            collection_levels={90: "evidence_backed_diagnosis", 210: "hypothesis"},
        ),
        case_corrupt_rows(),
        Case(
            name="header_only",
            description="A header and no data rows.",
            csv_text=",".join(COLUMNS) + "\n",
            rows=[],
            warmup_frames=0,
        ),
        Case(
            name="empty_file",
            description="A zero-byte frames.csv, which is a fatal input error.",
            csv_text="",
            rows=[],
            warmup_frames=0,
            fatal_error="CSV_EMPTY_FILE",
        ),
    ]


def make_manifest(case: Case, frames_bytes: bytes) -> dict[str, object]:
    data_lines = 0 if not case.csv_text else case.csv_text.rstrip("\n").count("\n")
    return {
        "schema_version": "1.1" if case.columns == COLUMNS_V11 else "1.0",
        "run_id": f"synthetic-{case.name}",
        "status": "completed",
        "abort_reason": None,
        "context": "player",
        "started_at_utc": "2026-10-20T00:00:00Z",
        "ended_at_utc": "2026-10-20T00:00:10Z",
        "frame_count": data_lines,
        "warmup_frames": case.warmup_frames,
        "frames_file": "frames.csv",
        "frames_sha256": hashlib.sha256(frames_bytes).hexdigest(),
        "build": {
            "unity_version": "2022.3.62f3",
            "build_id": "synthetic",
            "development_build": case.development_build,
            "scripting_backend": "mono",
            "graphics_api": "Direct3D11",
            "render_pipeline": "urp",
            "frame_timing_stats": True,
            "incremental_gc": True,
        },
        "device": {
            "os": "synthetic",
            "cpu": "synthetic",
            "cpu_logical_cores": 16,
            "system_memory_mb": 16288,
            "gpu_name": "synthetic",
            "gpu_memory_mb": 8192,
            "gpu_driver": None,
        },
        "display": {
            "width": 1280,
            "height": 720,
            "fullscreen_mode": "Windowed",
            "vsync_count": 0,
            "target_frame_rate": 60,
            "quality_level": "High",
        },
        "scenario": {"name": f"synthetic_{case.name}", "seed": 0, "params": {}},
        "instrumentation": [],
        "columns": [
            {
                "name": name,
                "unit": unit,
                "availability": "unsupported" if name in case.unsupported else "measured",
                "source": "synthetic",
            }
            for name, unit in ((c, ALL_UNITS[c]) for c in case.columns)
        ],
    }


def make_expected(case: Case) -> dict[str, object]:
    w = case.workload
    warmup = case.warmup_frames
    kept = [r for r in case.rows if frame_of(r) not in case.dropped_frames]
    analyzed = [r for r in kept if frame_of(r) >= warmup]

    def after_warmup(frames: frozenset[int]) -> list[int]:
        return sorted(f for f in frames if f >= warmup)

    injected = {
        "cpu_spikes": sorted(w.cpu_spikes) if w else [],
        "alloc_spikes": sorted(w.alloc_spikes) if w else [],
        "collections": sorted(w.collections) if w else [],
        "warmup_spike": w.warmup_spike if w else None,
    }
    alloc_visible = "gc_alloc_bytes" not in case.unsupported
    return {
        "case": case.name,
        "description": case.description,
        "rows_written": len(case.rows),
        "valid_rows": len(kept),
        "warmup_frames": warmup,
        "analyzed_frames": len(analyzed),
        "unsupported_columns": sorted(case.unsupported),
        "fatal_error": case.fatal_error,
        "injected": injected,
        "expected": {
            "hitch_frames": after_warmup(w.cpu_spikes | w.collections) if w else [],
            "gc_alloc_spike_frames": after_warmup(w.alloc_spikes) if w and alloc_visible else [],
            "gc_collection_frames": after_warmup(w.collections) if w else [],
            "gc_collection_levels": {
                str(f): case.collection_levels.get(f, "hypothesis")
                for f in (after_warmup(w.collections) if w else [])
            },
            "memory_growth": case.memory_growth,
            "diagnostics": case.diagnostics,
        },
        "frame_time_ms": summarize([float(r["frame_time_ms"] or 0.0) for r in analyzed]),
    }


def write_case(out_dir: Path, case: Case) -> None:
    case_dir = out_dir / case.name
    case_dir.mkdir(parents=True, exist_ok=True)
    frames_bytes = case.csv_text.encode("utf-8")
    (case_dir / "frames.csv").write_bytes(frames_bytes)
    for file_name, payload in (
        ("manifest.json", make_manifest(case, frames_bytes)),
        ("expected.json", make_expected(case)),
    ):
        text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
        (case_dir / file_name).write_bytes(text.encode("utf-8"))


def main() -> int:
    default_out = Path(__file__).resolve().parents[1] / "core" / "tests" / "testdata"
    parser = argparse.ArgumentParser(description="Generate synthetic GPIA run directories.")
    parser.add_argument("--out", type=Path, default=default_out)
    args = parser.parse_args()
    for case in build_cases():
        write_case(args.out, case)
        print(f"{case.name}: {len(case.rows)} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
