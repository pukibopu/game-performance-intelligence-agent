# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""运行 Phase 0 的真实 Player 实验矩阵，用 gpia-core 分析并与注入的 ground truth 对比"""

import argparse
import csv
import json
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILDS = REPO / "unity" / "GpiaBenchmark" / "Builds"
CLI = REPO / "core" / "build" / "windows-msvc-release" / "gpia-core.exe"

GC_FORCED = {"retained_objects": "5000000", "force_collect": "1"}
GC_INCREMENTAL = {"retained_objects": "300000"}


@dataclass(frozen=True)
class Experiment:
    """一组相同配置的重复运行"""

    name: str
    variant: str
    scenario: str
    params: dict[str, str] = field(default_factory=dict)
    repeats: int = 1
    injected_hitches: bool = True


MATRIX = [
    Experiment("idle_mono_release", "mono-release", "idle", injected_hitches=False),
    Experiment("idle_mono_dev", "mono-dev", "idle", injected_hitches=False),
    Experiment("cpu_mono_release", "mono-release", "cpu_spike", repeats=3),
    Experiment("cpu_il2cpp_release", "il2cpp-release", "cpu_spike", repeats=3),
    Experiment("gc_forced_mono_dev", "mono-dev", "gc_pressure", GC_FORCED, repeats=3),
    Experiment("gc_forced_mono_release", "mono-release", "gc_pressure", GC_FORCED, repeats=3),
    Experiment("gc_forced_il2cpp_release", "il2cpp-release", "gc_pressure", GC_FORCED, repeats=3),
    Experiment(
        "gc_incremental_mono_dev", "mono-dev", "gc_pressure", GC_INCREMENTAL, injected_hitches=False
    ),
]


def run_player(exp: Experiment, run_id: str, out: Path, warmup: float, duration: float) -> int:
    exe = BUILDS / exp.variant / "GpiaBenchmark.exe"
    args = [
        str(exe),
        "-screen-fullscreen", "0", "-screen-width", "1280", "-screen-height", "720",
        "-logFile", str(out / f"{run_id}.player.log"),
        "-gpia-scenario", exp.scenario,
        "-gpia-warmup", str(warmup),
        "-gpia-duration", str(duration),
        "-gpia-out", str(out),
        "-gpia-run-id", run_id,
    ]  # fmt: skip
    for key, value in exp.params.items():
        args += ["-gpia-param", f"{key}={value}"]
    return subprocess.run(args, timeout=warmup + duration + 120, check=False).returncode


def analyze(run_dir: Path) -> tuple[int, dict]:
    proc = subprocess.run(
        [str(CLI), "analyze", "--run", str(run_dir), "--out", str(run_dir / "report.json"),
         "--md-out", str(run_dir / "report.md")],
        capture_output=True, text=True, timeout=120, check=False,
    )  # fmt: skip
    report = json.loads((run_dir / "report.json").read_text(encoding="utf-8"))
    return proc.returncode, report


def finding_frames(report: dict, kind: str) -> set[int]:
    frames: set[int] = set()
    for f in report["findings"]:
        if f["kind"] == kind and "frame_range" in f:
            frames.update(range(f["frame_range"]["first"], f["frame_range"]["last"] + 1))
    return frames


def evaluate(exp: Experiment, run_dir: Path, report: dict) -> dict:
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    warmup = manifest["warmup_frames"]
    with (run_dir / "frames.csv").open(encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    markers = {
        int(r["frame_index"])
        for r in rows
        if r["scenario_marker"] != "0" and int(r["frame_index"]) >= warmup
    }
    hitches = finding_frames(report, "frame_hitch")
    truth = markers if exp.injected_hitches else set()
    hit = hitches & truth
    alloc = [
        int(r["gc_alloc_bytes"])
        for r in rows
        if r["gc_alloc_bytes"] and r["scenario_marker"] == "0"
    ]
    levels = [f["evidence_level"] for f in report["findings"] if f["kind"] == "gc_collection_hitch"]
    collector = json.loads((run_dir / "collector.json").read_text(encoding="utf-8"))
    s = report["summary"]["frame_time_ms"]
    return {
        "run_id": manifest["run_id"],
        "backend": manifest["build"]["scripting_backend"],
        "development": manifest["build"]["development_build"],
        "frames": report["input"]["frames_analyzed"],
        "p50": s["p50"],
        "p95": s["p95"],
        "p99": s["p99"],
        "max": s["max"],
        "markers": len(markers),
        "hitches": len(hitches),
        "true_positive": len(hit),
        "precision": len(hit) / len(hitches) if hitches else None,
        "recall": len(hit) / len(truth) if truth else None,
        "gc_hitch_levels": {lvl: levels.count(lvl) for lvl in sorted(set(levels))},
        "steady_alloc_bytes_mean": statistics.fmean(alloc) if alloc else None,
        "callback_mean_us": collector["callback_mean_us"],
        "callback_max_us": collector["callback_max_us"],
        "outcome": report["outcome"],
    }


def write_summary(results: dict[str, list[dict]], path: Path) -> None:
    lines = [
        "| Experiment | Runs | P50 ms | P95 ms | P99 ms | Max ms | Markers | Hitches | "
        "Precision | Recall | GC hitch levels | Steady alloc B/frame | Callback mean / max us |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|",
    ]
    for name, runs in results.items():
        levels: dict[str, int] = {}
        for r in runs:
            for k, v in r["gc_hitch_levels"].items():
                levels[k] = levels.get(k, 0) + v
        recalls = [r["recall"] for r in runs if r["recall"] is not None]
        allocs = [
            r["steady_alloc_bytes_mean"] for r in runs if r["steady_alloc_bytes_mean"] is not None
        ]
        cells = [
            name,
            str(len(runs)),
            *(f"{statistics.fmean(r[k] for r in runs):.3f}" for k in ("p50", "p95", "p99", "max")),
            str(sum(r["markers"] for r in runs)),
            str(sum(r["hitches"] for r in runs)),
            f"{statistics.fmean(ps):.2f}"
            if (ps := [r["precision"] for r in runs if r["precision"] is not None])
            else "n/a",
            f"{statistics.fmean(recalls):.2f}" if recalls else "n/a",
            ", ".join(f"{k}: {v}" for k, v in levels.items()) or "-",
            f"{statistics.fmean(allocs):.1f}" if allocs else "n/a",
            f"{statistics.fmean(r['callback_mean_us'] for r in runs):.1f} / "
            f"{max(r['callback_max_us'] for r in runs):.1f}",
        ]
        lines.append("| " + " | ".join(cells) + " |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the Phase 0 player experiment matrix.")
    parser.add_argument("--out", type=Path, default=REPO / "runs" / "phase0")
    parser.add_argument("--warmup", type=float, default=3.0)
    parser.add_argument("--duration", type=float, default=20.0)
    parser.add_argument("--only", nargs="*", help="experiment names to run")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    results: dict[str, list[dict]] = {}
    for exp in MATRIX:
        if args.only and exp.name not in args.only:
            continue
        for i in range(1, exp.repeats + 1):
            run_id = f"{exp.name}-{i}"
            code = run_player(exp, run_id, args.out, args.warmup, args.duration)
            if code != 0:
                print(f"{run_id}: player exit {code}", file=sys.stderr)
                return 1
            cli_code, report = analyze(args.out / run_id)
            result = evaluate(exp, args.out / run_id, report)
            result["cli_exit"] = cli_code
            results.setdefault(exp.name, []).append(result)
            print(json.dumps(result), flush=True)

    (args.out / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    write_summary(results, args.out / "summary.md")
    print((args.out / "summary.md").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
