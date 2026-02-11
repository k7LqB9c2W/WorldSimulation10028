#!/usr/bin/env python3
"""Parallel runner for state stability/legitimacy diagnostics.

Runs `worldsim_cli` for many seeds with `--stateDiagnostics 1`, then aggregates:
- how many countries are low/critical at checkpoints
- population shares under low state quality
- dominant stability/legitimacy causes among weak states
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import os
import random
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class DiagConfig:
    repo_root: Path
    exe: Path
    config: Path
    out_root: Path
    start_year: int
    end_year: int
    checkpoint_every_years: int
    workers: int
    use_gpu: Optional[bool]
    reuse_existing: bool


def resolve_user_path(path_value: str, repo_root: Path) -> Path:
    p = Path(path_value)
    if p.is_absolute():
        return p
    return (repo_root / p).resolve()


def to_cli_path(path: Path) -> str:
    s = str(path)
    if s.startswith("/mnt/") and len(s) > 7:
        drive = s[5].upper()
        rest = s[7:].replace("/", "\\")
        return f"{drive}:\\{rest}"
    return s


def default_workers() -> int:
    cpu = os.cpu_count() or 4
    return max(1, min(32, cpu))


def parse_seed_values(
    *,
    random_count: Optional[int],
    random_seed: Optional[int],
    seed_min: int,
    seed_max: int,
    seed_start: Optional[int],
    seed_end: Optional[int],
    seeds_csv: Optional[str],
) -> list[int]:
    if random_count is not None:
        if random_count <= 0:
            raise ValueError("--random-count must be > 0")
        if seed_max < seed_min:
            raise ValueError("--seed-max must be >= --seed-min")
        space = seed_max - seed_min + 1
        if random_count > space:
            raise ValueError("--random-count exceeds seed range size")
        rng = random.Random(random_seed)
        return sorted(rng.sample(range(seed_min, seed_max + 1), random_count))

    if seeds_csv:
        values: list[int] = []
        for raw in seeds_csv.split(","):
            s = raw.strip()
            if not s:
                continue
            values.append(int(s))
        if not values:
            raise ValueError("--seeds was provided but no valid values were found")
        return sorted(set(values))

    if seed_start is None or seed_end is None:
        raise ValueError("Provide one seed mode: --random-count, or --seeds, or both --seed-start and --seed-end.")
    if seed_end < seed_start:
        raise ValueError("--seed-end must be >= --seed-start")
    return list(range(seed_start, seed_end + 1))


def _load_json(path: Path) -> Optional[dict]:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def can_reuse_existing(seed: int, cfg: DiagConfig, seed_dir: Path) -> bool:
    summary_csv = seed_dir / "state_diagnostics_summary.csv"
    countries_csv = seed_dir / "state_diagnostics_countries.csv"
    run_meta = seed_dir / "run_meta.json"
    if not summary_csv.exists() or not countries_csv.exists() or not run_meta.exists():
        return False

    meta = _load_json(run_meta)
    if not isinstance(meta, dict):
        return False

    try:
        m_seed = int(meta.get("seed"))
        m_start = int(meta.get("start_year"))
        m_end = int(meta.get("end_year"))
    except Exception:
        return False

    if m_seed != seed or m_start != cfg.start_year or m_end != cfg.end_year:
        return False

    backend = str(meta.get("backend", "")).strip().lower()
    if cfg.use_gpu is not None:
        want = "gpu" if cfg.use_gpu else "cpu"
        if backend != want:
            return False

    cfg_name = str(meta.get("config_path", "")).strip()
    if cfg_name and Path(cfg_name).name != cfg.config.name:
        return False

    if str(meta.get("state_diagnostics", "")).lower() not in ("true", "1"):
        return False

    return True


def build_cmd(seed: int, cfg: DiagConfig, seed_out_dir: Path) -> list[str]:
    cmd = [
        str(cfg.exe),
        "--seed",
        str(seed),
        "--config",
        to_cli_path(cfg.config),
        "--startYear",
        str(cfg.start_year),
        "--endYear",
        str(cfg.end_year),
        "--checkpointEveryYears",
        str(cfg.checkpoint_every_years),
        "--outDir",
        to_cli_path(seed_out_dir),
        "--stateDiagnostics",
        "1",
    ]
    if cfg.use_gpu is not None:
        cmd.extend(["--useGPU", "1" if cfg.use_gpu else "0"])
    return cmd


def _to_int(row: dict, key: str) -> int:
    try:
        return int(float(str(row.get(key, "0"))))
    except Exception:
        return 0


def _to_float(row: dict, key: str) -> float:
    try:
        return float(str(row.get(key, "0")))
    except Exception:
        return 0.0


def analyze_seed(seed_dir: Path) -> dict:
    summary_csv = seed_dir / "state_diagnostics_summary.csv"
    countries_csv = seed_dir / "state_diagnostics_countries.csv"
    if not summary_csv.exists() or not countries_csv.exists():
        return {"ok": False, "reason": "state diagnostics files missing"}

    checkpoints: list[dict] = []
    with summary_csv.open("r", encoding="utf-8", newline="") as f:
        checkpoints = list(csv.DictReader(f))
    if not checkpoints:
        return {"ok": False, "reason": "state diagnostics summary empty"}

    final = checkpoints[-1]
    max_pop_low_stab_share = max(_to_float(r, "pop_low_stability_share") for r in checkpoints)
    max_pop_low_leg_share = max(_to_float(r, "pop_low_legitimacy_share") for r in checkpoints)
    max_pop_critical_share = max(_to_float(r, "pop_critical_share") for r in checkpoints)
    max_low_stab_countries = max(_to_int(r, "low_stability_countries") for r in checkpoints)
    max_low_leg_countries = max(_to_int(r, "low_legitimacy_countries") for r in checkpoints)

    low_stab_cause_counts: dict[str, int] = {}
    low_leg_cause_counts: dict[str, int] = {}
    low_state_country_hits: dict[str, int] = {}
    with countries_csv.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            low_s = _to_int(row, "low_stability") != 0
            low_l = _to_int(row, "low_legitimacy") != 0
            if low_s:
                cause = str(row.get("stab_dominant_cause", "none") or "none")
                low_stab_cause_counts[cause] = low_stab_cause_counts.get(cause, 0) + 1
            if low_l:
                cause = str(row.get("legit_dominant_cause", "none") or "none")
                low_leg_cause_counts[cause] = low_leg_cause_counts.get(cause, 0) + 1
            if low_s or low_l:
                cid = str(row.get("country_index", ""))
                if cid:
                    low_state_country_hits[cid] = low_state_country_hits.get(cid, 0) + 1

    def top_map(m: dict[str, int], n: int = 5) -> list[dict]:
        return [{"name": k, "count": v} for k, v in sorted(m.items(), key=lambda kv: (-kv[1], kv[0]))[:n]]

    top_low_state_countries = [
        {"country_index": k, "hits": v}
        for k, v in sorted(low_state_country_hits.items(), key=lambda kv: (-kv[1], kv[0]))[:8]
    ]

    return {
        "ok": True,
        "reason": "",
        "checkpoints": len(checkpoints),
        "final_year": _to_int(final, "year"),
        "final_live_countries": _to_int(final, "live_countries"),
        "final_stable_countries": _to_int(final, "stable_countries"),
        "final_low_stability_countries": _to_int(final, "low_stability_countries"),
        "final_low_legitimacy_countries": _to_int(final, "low_legitimacy_countries"),
        "final_low_both_countries": _to_int(final, "low_both_countries"),
        "final_critical_countries": _to_int(final, "critical_countries"),
        "final_pop_low_stability_share": _to_float(final, "pop_low_stability_share"),
        "final_pop_low_legitimacy_share": _to_float(final, "pop_low_legitimacy_share"),
        "final_pop_low_both_share": _to_float(final, "pop_low_both_share"),
        "final_pop_critical_share": _to_float(final, "pop_critical_share"),
        "final_stability_mean": _to_float(final, "stability_mean"),
        "final_stability_p10": _to_float(final, "stability_p10"),
        "final_legitimacy_mean": _to_float(final, "legitimacy_mean"),
        "final_legitimacy_p10": _to_float(final, "legitimacy_p10"),
        "max_pop_low_stability_share": max_pop_low_stab_share,
        "max_pop_low_legitimacy_share": max_pop_low_leg_share,
        "max_pop_critical_share": max_pop_critical_share,
        "max_low_stability_countries": max_low_stab_countries,
        "max_low_legitimacy_countries": max_low_leg_countries,
        "top_low_stability_causes": top_map(low_stab_cause_counts),
        "top_low_legitimacy_causes": top_map(low_leg_cause_counts),
        "top_low_state_countries": top_low_state_countries,
    }


def run_one_seed(seed: int, cfg: DiagConfig) -> dict:
    seed_dir = cfg.out_root / f"seed_{seed}"
    run_log = seed_dir / "seed_run.log"
    seed_dir.mkdir(parents=True, exist_ok=True)

    started = time.time()
    reused = False
    rc = 0
    if cfg.reuse_existing and can_reuse_existing(seed, cfg, seed_dir):
        reused = True
    else:
        cmd = build_cmd(seed, cfg, seed_dir)
        with run_log.open("w", encoding="utf-8") as logf:
            logf.write("COMMAND:\n")
            logf.write(" ".join(cmd) + "\n\n")
            rc = subprocess.call(cmd, stdout=logf, stderr=subprocess.STDOUT, cwd=str(cfg.repo_root))

    elapsed = time.time() - started
    if rc != 0:
        return {
            "seed": seed,
            "ok": False,
            "returncode": rc,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": "cli run failed",
        }

    analyzed = analyze_seed(seed_dir)
    if not analyzed.get("ok"):
        return {
            "seed": seed,
            "ok": False,
            "returncode": 0,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": analyzed.get("reason", "diagnostic parse failed"),
        }

    return {
        "seed": seed,
        "ok": True,
        "returncode": 0,
        "elapsed_sec": elapsed,
        "run_dir": str(seed_dir),
        "reused": reused,
        "reason": "",
        **analyzed,
    }


def build_summary(results: list[dict], elapsed_sec: float) -> dict:
    ok_runs = [r for r in results if r.get("ok")]
    fail_runs = [r for r in results if not r.get("ok")]

    def fmean(key: str) -> float:
        if not ok_runs:
            return 0.0
        return sum(float(r.get(key, 0.0)) for r in ok_runs) / float(len(ok_runs))

    agg_stab_causes: dict[str, int] = {}
    agg_leg_causes: dict[str, int] = {}
    for r in ok_runs:
        for item in r.get("top_low_stability_causes", []):
            n = str(item.get("name", "none"))
            c = int(item.get("count", 0))
            agg_stab_causes[n] = agg_stab_causes.get(n, 0) + c
        for item in r.get("top_low_legitimacy_causes", []):
            n = str(item.get("name", "none"))
            c = int(item.get("count", 0))
            agg_leg_causes[n] = agg_leg_causes.get(n, 0) + c

    top_stab = [{"name": k, "count": v} for k, v in sorted(agg_stab_causes.items(), key=lambda kv: (-kv[1], kv[0]))[:10]]
    top_leg = [{"name": k, "count": v} for k, v in sorted(agg_leg_causes.items(), key=lambda kv: (-kv[1], kv[0]))[:10]]

    return {
        "elapsed_sec": elapsed_sec,
        "total_seeds": len(results),
        "ok_runs": len(ok_runs),
        "failed_runs": len(fail_runs),
        "final_low_state_means": {
            "final_low_stability_countries": fmean("final_low_stability_countries"),
            "final_low_legitimacy_countries": fmean("final_low_legitimacy_countries"),
            "final_low_both_countries": fmean("final_low_both_countries"),
            "final_pop_low_stability_share": fmean("final_pop_low_stability_share"),
            "final_pop_low_legitimacy_share": fmean("final_pop_low_legitimacy_share"),
            "final_pop_critical_share": fmean("final_pop_critical_share"),
            "final_stability_mean": fmean("final_stability_mean"),
            "final_legitimacy_mean": fmean("final_legitimacy_mean"),
        },
        "peak_low_state_means": {
            "max_pop_low_stability_share": fmean("max_pop_low_stability_share"),
            "max_pop_low_legitimacy_share": fmean("max_pop_low_legitimacy_share"),
            "max_pop_critical_share": fmean("max_pop_critical_share"),
            "max_low_stability_countries": fmean("max_low_stability_countries"),
            "max_low_legitimacy_countries": fmean("max_low_legitimacy_countries"),
        },
        "aggregate_top_low_stability_causes": top_stab,
        "aggregate_top_low_legitimacy_causes": top_leg,
    }


def write_outputs(out_root: Path, results: list[dict], summary: dict) -> None:
    out_root.mkdir(parents=True, exist_ok=True)

    rows_csv = out_root / "state_diag_seed_results.csv"
    with rows_csv.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "seed",
                "ok",
                "returncode",
                "elapsed_sec",
                "reused",
                "final_year",
                "final_live_countries",
                "final_stable_countries",
                "final_low_stability_countries",
                "final_low_legitimacy_countries",
                "final_low_both_countries",
                "final_critical_countries",
                "final_pop_low_stability_share",
                "final_pop_low_legitimacy_share",
                "final_pop_low_both_share",
                "final_pop_critical_share",
                "final_stability_mean",
                "final_stability_p10",
                "final_legitimacy_mean",
                "final_legitimacy_p10",
                "max_pop_low_stability_share",
                "max_pop_low_legitimacy_share",
                "max_pop_critical_share",
                "max_low_stability_countries",
                "max_low_legitimacy_countries",
                "top_low_stability_causes",
                "top_low_legitimacy_causes",
                "top_low_state_countries",
                "run_dir",
                "reason",
            ]
        )
        for r in results:
            w.writerow(
                [
                    r.get("seed"),
                    r.get("ok"),
                    r.get("returncode"),
                    f"{r.get('elapsed_sec', 0.0):.3f}",
                    r.get("reused"),
                    r.get("final_year", ""),
                    r.get("final_live_countries", ""),
                    r.get("final_stable_countries", ""),
                    r.get("final_low_stability_countries", ""),
                    r.get("final_low_legitimacy_countries", ""),
                    r.get("final_low_both_countries", ""),
                    r.get("final_critical_countries", ""),
                    r.get("final_pop_low_stability_share", ""),
                    r.get("final_pop_low_legitimacy_share", ""),
                    r.get("final_pop_low_both_share", ""),
                    r.get("final_pop_critical_share", ""),
                    r.get("final_stability_mean", ""),
                    r.get("final_stability_p10", ""),
                    r.get("final_legitimacy_mean", ""),
                    r.get("final_legitimacy_p10", ""),
                    r.get("max_pop_low_stability_share", ""),
                    r.get("max_pop_low_legitimacy_share", ""),
                    r.get("max_pop_critical_share", ""),
                    r.get("max_low_stability_countries", ""),
                    r.get("max_low_legitimacy_countries", ""),
                    json.dumps(r.get("top_low_stability_causes", [])),
                    json.dumps(r.get("top_low_legitimacy_causes", [])),
                    json.dumps(r.get("top_low_state_countries", [])),
                    r.get("run_dir", ""),
                    r.get("reason", ""),
                ]
            )

    with (out_root / "state_diag_batch_summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)


def run_batch(cfg: DiagConfig, seeds: list[int]) -> tuple[list[dict], dict]:
    started = time.time()
    results: list[dict] = []
    total = len(seeds)
    if total == 0:
        raise ValueError("No seeds to run")

    print(
        f"Running diagnostics for {total} seeds with workers={cfg.workers}, years=[{cfg.start_year},{cfg.end_year}], use_gpu={cfg.use_gpu}",
        flush=True,
    )

    with concurrent.futures.ThreadPoolExecutor(max_workers=cfg.workers) as ex:
        futs = {ex.submit(run_one_seed, seed, cfg): seed for seed in seeds}
        done = 0
        for fut in concurrent.futures.as_completed(futs):
            seed = futs[fut]
            done += 1
            try:
                r = fut.result()
            except Exception as exc:
                r = {
                    "seed": seed,
                    "ok": False,
                    "returncode": -1,
                    "elapsed_sec": 0.0,
                    "run_dir": str(cfg.out_root / f"seed_{seed}"),
                    "reused": False,
                    "reason": f"exception: {exc}",
                }
            results.append(r)
            if r.get("ok"):
                print(
                    f"[{done}/{total}] seed={seed} ok final_low_s={r.get('final_low_stability_countries')} "
                    f"final_low_l={r.get('final_low_legitimacy_countries')}",
                    flush=True,
                )
            else:
                print(f"[{done}/{total}] seed={seed} fail rc={r.get('returncode')} reason={r.get('reason')}", flush=True)

    results.sort(key=lambda x: int(x.get("seed", 0)))
    summary = build_summary(results, time.time() - started)
    write_outputs(cfg.out_root, results, summary)
    return results, summary


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run multi-seed state diagnostics and aggregate low-state causes.")
    p.add_argument("--exe", default="out/cmake/release/bin/worldsim_cli.exe")
    p.add_argument("--config", default="data/sim_config.toml")
    p.add_argument("--out-root", default="out/state_diag_batch")

    p.add_argument("--random-count", type=int, help="Generate N unique random seeds")
    p.add_argument("--random-seed", type=int, help="RNG seed for deterministic seed generation")
    p.add_argument("--seed-min", type=int, default=1)
    p.add_argument("--seed-max", type=int, default=2_000_000_000)
    p.add_argument("--seed-start", type=int)
    p.add_argument("--seed-end", type=int)
    p.add_argument("--seeds", help="Comma-separated explicit seeds")

    p.add_argument("--start-year", type=int, default=-5000)
    p.add_argument("--end-year", type=int, default=2025)
    p.add_argument("--checkpoint-every-years", type=int, default=50)
    p.add_argument("--workers", type=int, default=default_workers())
    p.add_argument("--use-gpu", choices=["0", "1"], default="0")
    p.add_argument("--no-reuse", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent

    if args.end_year < args.start_year:
        raise ValueError("--end-year must be >= --start-year")

    seeds = parse_seed_values(
        random_count=args.random_count,
        random_seed=args.random_seed,
        seed_min=args.seed_min,
        seed_max=args.seed_max,
        seed_start=args.seed_start,
        seed_end=args.seed_end,
        seeds_csv=args.seeds,
    )

    use_gpu: Optional[bool] = None if args.use_gpu is None else (args.use_gpu == "1")
    cfg = DiagConfig(
        repo_root=repo_root,
        exe=resolve_user_path(args.exe, repo_root),
        config=resolve_user_path(args.config, repo_root),
        out_root=resolve_user_path(args.out_root, repo_root),
        start_year=args.start_year,
        end_year=args.end_year,
        checkpoint_every_years=max(1, args.checkpoint_every_years),
        workers=max(1, args.workers),
        use_gpu=use_gpu,
        reuse_existing=(not args.no_reuse),
    )

    if not cfg.exe.exists():
        raise FileNotFoundError(f"Executable not found: {cfg.exe}")
    if not cfg.config.exists():
        raise FileNotFoundError(f"Config not found: {cfg.config}")

    _, summary = run_batch(cfg, seeds)
    print("\nSummary:")
    print(json.dumps(summary, indent=2))
    return 0 if summary.get("failed_runs", 0) == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(130)
