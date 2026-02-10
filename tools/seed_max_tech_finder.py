#!/usr/bin/env python3
"""Parallel random-seed sweep for maximum tech reached.

Runs `worldsim_cli` for many seeds, captures `tech_unlocks.csv`, and reports:
- max unlocked tech-count reached in each seed
- highest tech-id reached in each seed
- overall best across all seeds
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
from typing import Iterable, Optional


@dataclass(frozen=True)
class SweepConfig:
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
    include_initial_log_rows: bool = True


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
        seeds: list[int] = []
        for raw in seeds_csv.split(","):
            s = raw.strip()
            if not s:
                continue
            seeds.append(int(s))
        if not seeds:
            raise ValueError("--seeds was provided but no valid values were found")
        return sorted(set(seeds))

    if seed_start is None or seed_end is None:
        raise ValueError(
            "Provide one seed mode: --random-count, or --seeds, or both --seed-start and --seed-end."
        )
    if seed_end < seed_start:
        raise ValueError("--seed-end must be >= --seed-start")
    return list(range(seed_start, seed_end + 1))


def build_cli_cmd(seed: int, cfg: SweepConfig, seed_out_dir: Path, tech_log_path: Path) -> list[str]:
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
        "--techUnlockLog",
        to_cli_path(tech_log_path),
        "--techUnlockLogIncludeInitial",
        "1" if cfg.include_initial_log_rows else "0",
    ]
    if cfg.use_gpu is not None:
        cmd.extend(["--useGPU", "1" if cfg.use_gpu else "0"])
    return cmd


def _load_json(path: Path) -> Optional[dict]:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def can_reuse_existing_run(seed: int, cfg: SweepConfig, seed_dir: Path, tech_log: Path) -> bool:
    if not tech_log.exists():
        return False

    meta_path = seed_dir / "run_meta.json"
    meta = _load_json(meta_path)
    if not isinstance(meta, dict):
        return False

    try:
        meta_seed = int(meta.get("seed"))
        meta_start = int(meta.get("start_year"))
        meta_end = int(meta.get("end_year"))
    except Exception:
        return False

    if meta_seed != seed or meta_start != cfg.start_year or meta_end != cfg.end_year:
        return False

    if cfg.use_gpu is not None:
        backend = str(meta.get("backend", "")).strip().lower()
        want = "gpu" if cfg.use_gpu else "cpu"
        if backend != want:
            return False

    meta_cfg_raw = str(meta.get("config_path", "")).strip()
    if meta_cfg_raw and Path(meta_cfg_raw).name != cfg.config.name:
        return False

    return True


def scan_max_tech(tech_log_path: Path) -> dict:
    if not tech_log_path.exists():
        return {"ok": False, "reason": "tech log missing"}

    max_total = -1
    max_total_row: Optional[dict] = None
    max_tech_id = -1
    max_tech_row: Optional[dict] = None
    row_count = 0

    with tech_log_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            row_count += 1
            try:
                year = int(row.get("year", ""))
                tech_id = int(row.get("tech_id", ""))
                total = int(row.get("total_unlocked_techs", ""))
            except ValueError:
                continue

            if total > max_total:
                max_total = total
                max_total_row = {
                    "year": year,
                    "tech_id": tech_id,
                    "tech_name": row.get("tech_name", ""),
                    "country_index": row.get("country_index", ""),
                    "country_name": row.get("country_name", ""),
                    "event_type": row.get("event_type", ""),
                }

            if tech_id > max_tech_id:
                max_tech_id = tech_id
                max_tech_row = {
                    "year": year,
                    "tech_id": tech_id,
                    "tech_name": row.get("tech_name", ""),
                    "country_index": row.get("country_index", ""),
                    "country_name": row.get("country_name", ""),
                    "event_type": row.get("event_type", ""),
                    "total_unlocked_techs": total,
                }

    if row_count == 0:
        return {"ok": False, "reason": "tech log empty"}
    if max_total_row is None or max_tech_row is None:
        return {"ok": False, "reason": "tech log parse had no valid rows"}

    return {
        "ok": True,
        "reason": "",
        "rows": row_count,
        "max_total_unlocked_techs": max_total,
        "max_total_row": max_total_row,
        "max_tech_row": max_tech_row,
    }


def run_one_seed(seed: int, cfg: SweepConfig) -> dict:
    seed_dir = cfg.out_root / f"seed_{seed}"
    tech_log = seed_dir / "tech_unlocks.csv"
    run_log = seed_dir / "seed_run.log"
    seed_dir.mkdir(parents=True, exist_ok=True)

    started = time.time()
    reused = False
    rc = 0

    if cfg.reuse_existing and can_reuse_existing_run(seed, cfg, seed_dir, tech_log):
        reused = True
    else:
        cmd = build_cli_cmd(seed, cfg, seed_dir, tech_log)
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

    scan = scan_max_tech(tech_log)
    if not scan["ok"]:
        return {
            "seed": seed,
            "ok": False,
            "returncode": 0,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": scan["reason"],
        }

    return {
        "seed": seed,
        "ok": True,
        "returncode": 0,
        "elapsed_sec": elapsed,
        "run_dir": str(seed_dir),
        "reused": reused,
        "reason": "",
        "rows": scan["rows"],
        "max_total_unlocked_techs": scan["max_total_unlocked_techs"],
        "max_total_row": scan["max_total_row"],
        "max_tech_row": scan["max_tech_row"],
    }


def build_summary(cfg: SweepConfig, results: list[dict], elapsed_sec: float) -> dict:
    ok_runs = [r for r in results if r.get("ok")]
    fail_runs = [r for r in results if not r.get("ok")]

    best_total = None
    for r in ok_runs:
        cur = int(r["max_total_unlocked_techs"])
        if best_total is None or cur > int(best_total["max_total_unlocked_techs"]):
            best_total = r

    best_tech_id = None
    for r in ok_runs:
        row = r.get("max_tech_row") or {}
        tid = int(row.get("tech_id", -1))
        if best_tech_id is None or tid > int((best_tech_id.get("max_tech_row") or {}).get("tech_id", -1)):
            best_tech_id = r

    return {
        "elapsed_sec": elapsed_sec,
        "total_seeds": len(results),
        "ok_runs": len(ok_runs),
        "failed_runs": len(fail_runs),
        "run_settings": {
            "start_year": cfg.start_year,
            "end_year": cfg.end_year,
            "checkpoint_every_years": cfg.checkpoint_every_years,
            "use_gpu": cfg.use_gpu,
        },
        "best_by_unlocked_count": None
        if best_total is None
        else {
            "seed": best_total["seed"],
            "max_total_unlocked_techs": best_total["max_total_unlocked_techs"],
            "detail": best_total.get("max_total_row"),
            "run_dir": best_total["run_dir"],
        },
        "best_by_tech_id": None
        if best_tech_id is None
        else {
            "seed": best_tech_id["seed"],
            "detail": best_tech_id.get("max_tech_row"),
            "run_dir": best_tech_id["run_dir"],
        },
    }


def write_results(out_root: Path, results: list[dict], summary: dict) -> None:
    out_root.mkdir(parents=True, exist_ok=True)

    csv_path = out_root / "seed_max_tech_results.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "seed",
                "ok",
                "returncode",
                "elapsed_sec",
                "reused",
                "max_total_unlocked_techs",
                "max_total_year",
                "max_total_country_index",
                "max_total_country_name",
                "max_total_tech_id",
                "max_total_tech_name",
                "max_tech_id",
                "max_tech_name",
                "max_tech_year",
                "max_tech_country_index",
                "max_tech_country_name",
                "rows",
                "run_dir",
                "reason",
            ]
        )
        for r in results:
            max_total = r.get("max_total_row") or {}
            max_tid = r.get("max_tech_row") or {}
            writer.writerow(
                [
                    r.get("seed"),
                    r.get("ok"),
                    r.get("returncode"),
                    f"{r.get('elapsed_sec', 0.0):.3f}",
                    r.get("reused"),
                    r.get("max_total_unlocked_techs", ""),
                    max_total.get("year", ""),
                    max_total.get("country_index", ""),
                    max_total.get("country_name", ""),
                    max_total.get("tech_id", ""),
                    max_total.get("tech_name", ""),
                    max_tid.get("tech_id", ""),
                    max_tid.get("tech_name", ""),
                    max_tid.get("year", ""),
                    max_tid.get("country_index", ""),
                    max_tid.get("country_name", ""),
                    r.get("rows", ""),
                    r.get("run_dir", ""),
                    r.get("reason", ""),
                ]
            )

    with (out_root / "seed_max_tech_summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)


def run_sweep(cfg: SweepConfig, seeds: Iterable[int]) -> tuple[list[dict], dict]:
    seed_list = list(seeds)
    if not seed_list:
        raise ValueError("No seeds to run")

    started = time.time()
    results: list[dict] = []

    print(
        f"Running {len(seed_list)} seeds with workers={cfg.workers}, "
        f"years=[{cfg.start_year},{cfg.end_year}], use_gpu={cfg.use_gpu}",
        flush=True,
    )

    with concurrent.futures.ThreadPoolExecutor(max_workers=cfg.workers) as ex:
        future_to_seed = {ex.submit(run_one_seed, seed, cfg): seed for seed in seed_list}
        done = 0
        for fut in concurrent.futures.as_completed(future_to_seed):
            seed = future_to_seed[fut]
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
                    f"[{done}/{len(seed_list)}] seed={seed} ok "
                    f"max_total={r.get('max_total_unlocked_techs')} "
                    f"max_tid={(r.get('max_tech_row') or {}).get('tech_id')}",
                    flush=True,
                )
            else:
                print(
                    f"[{done}/{len(seed_list)}] seed={seed} fail rc={r.get('returncode')} reason={r.get('reason')}",
                    flush=True,
                )

    results.sort(key=lambda x: int(x.get("seed", 0)))
    summary = build_summary(cfg, results, time.time() - started)
    write_results(cfg.out_root, results, summary)
    return results, summary


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run many seeds and report max tech reached per simulation.")
    p.add_argument("--exe", default="out/cmake/release/bin/worldsim_cli.exe")
    p.add_argument("--config", default="data/sim_config.toml")
    p.add_argument("--out-root", default="out/seed_max_tech_search")

    p.add_argument("--random-count", type=int, help="Generate N unique random seeds")
    p.add_argument("--random-seed", type=int, help="RNG seed for reproducible random seed generation")
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
    p.add_argument(
        "--exclude-initial-log-rows",
        action="store_true",
        help="Exclude initial unlock rows from tech log (included by default for robust max-tech scans)",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent

    seeds = parse_seed_values(
        random_count=args.random_count,
        random_seed=args.random_seed,
        seed_min=args.seed_min,
        seed_max=args.seed_max,
        seed_start=args.seed_start,
        seed_end=args.seed_end,
        seeds_csv=args.seeds,
    )
    if args.end_year < args.start_year:
        raise ValueError("--end-year must be >= --start-year")

    use_gpu: Optional[bool]
    if args.use_gpu is None:
        use_gpu = None
    else:
        use_gpu = args.use_gpu == "1"

    cfg = SweepConfig(
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
        include_initial_log_rows=(not args.exclude_initial_log_rows),
    )

    if not cfg.exe.exists():
        raise FileNotFoundError(f"Executable not found: {cfg.exe}")
    if not cfg.config.exists():
        raise FileNotFoundError(f"Config not found: {cfg.config}")

    _, summary = run_sweep(cfg, seeds)
    print("\nSummary:")
    print(json.dumps(summary, indent=2))
    return 0 if summary["failed_runs"] == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(130)
