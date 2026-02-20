#!/usr/bin/env python3
"""Parallel seed sweep for maximum tech reached.

Runs `worldsim_cli` for many seeds, captures `tech_unlocks.csv`, and reports:
- max unlocked tech-count reached in each seed
- highest tech-id reached in each seed
- overall best across all seeds

Supports both CLI mode and a Tkinter GUI (`--gui`) with live per-seed year updates.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import os
import queue
import random
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional


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


ProgressCallback = Callable[[str], None]
SeedEventCallback = Callable[[dict], None]


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


def tech_log_has_column(tech_log: Path, column_name: str) -> bool:
    try:
        with tech_log.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            fieldnames = reader.fieldnames or []
            return column_name in fieldnames
    except Exception:
        return False


def can_reuse_existing_run(seed: int, cfg: SweepConfig, seed_dir: Path, tech_log: Path) -> bool:
    if not tech_log.exists():
        return False
    # Require culture column so the sweep can report top-country culture consistently.
    if not tech_log_has_column(tech_log, "country_culture"):
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


def read_latest_year_from_tech_log(tech_log_path: Path, bytes_from_end: int = 8192) -> Optional[int]:
    if not tech_log_path.exists():
        return None
    try:
        with tech_log_path.open("rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            if size <= 0:
                return None
            start = max(0, size - bytes_from_end)
            f.seek(start)
            chunk = f.read().decode("utf-8", errors="ignore")
    except OSError:
        return None

    for line in reversed(chunk.splitlines()):
        row = line.strip()
        if not row or row.startswith("year,"):
            continue
        first = row.split(",", 1)[0].strip()
        try:
            return int(first)
        except ValueError:
            continue
    return None


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
                    "country_culture": row.get("country_culture", ""),
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
                    "country_culture": row.get("country_culture", ""),
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


def run_one_seed(
    seed: int,
    cfg: SweepConfig,
    cancel_event: Optional[threading.Event] = None,
    seed_event_cb: Optional[SeedEventCallback] = None,
) -> dict:
    seed_dir = cfg.out_root / f"seed_{seed}"
    tech_log = seed_dir / "tech_unlocks.csv"
    run_log = seed_dir / "seed_run.log"
    seed_dir.mkdir(parents=True, exist_ok=True)

    def emit_event(**fields: object) -> None:
        if seed_event_cb is None:
            return
        payload = {"seed": seed}
        payload.update(fields)
        try:
            seed_event_cb(payload)
        except Exception:
            pass

    started = time.time()
    reused = False
    rc = 0
    emit_event(state="starting", current_year=None, elapsed_sec=0.0, note="")

    if cancel_event is not None and cancel_event.is_set():
        emit_event(state="canceled", current_year=None, elapsed_sec=0.0, note="canceled before start")
        return {
            "seed": seed,
            "ok": False,
            "returncode": -2,
            "elapsed_sec": 0.0,
            "run_dir": str(seed_dir),
            "reused": False,
            "reason": "canceled",
            "canceled": True,
        }

    if cfg.reuse_existing and can_reuse_existing_run(seed, cfg, seed_dir, tech_log):
        reused = True
        current_year = read_latest_year_from_tech_log(tech_log)
        emit_event(
            state="reused",
            current_year=current_year,
            elapsed_sec=(time.time() - started),
            note="reused existing run",
        )
    else:
        cmd = build_cli_cmd(seed, cfg, seed_dir, tech_log)
        with run_log.open("w", encoding="utf-8") as logf:
            logf.write("COMMAND:\n")
            logf.write(" ".join(cmd) + "\n\n")
            proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, cwd=str(cfg.repo_root))
            last_year: Optional[int] = None
            last_probe = 0.0
            canceled = False

            while True:
                polled = proc.poll()
                if polled is not None:
                    rc = polled
                    break

                if cancel_event is not None and cancel_event.is_set():
                    canceled = True
                    try:
                        proc.terminate()
                        proc.wait(timeout=3.0)
                    except Exception:
                        try:
                            proc.kill()
                            proc.wait(timeout=3.0)
                        except Exception:
                            pass
                    rc = proc.poll()
                    if rc is None:
                        rc = -2
                    break

                now = time.monotonic()
                if now - last_probe >= 0.35:
                    year = read_latest_year_from_tech_log(tech_log)
                    last_probe = now
                    if year is not None and year != last_year:
                        last_year = year
                        emit_event(
                            state="running",
                            current_year=year,
                            elapsed_sec=(time.time() - started),
                            note="",
                        )

                time.sleep(0.10)

            if canceled:
                elapsed = time.time() - started
                emit_event(
                    state="canceled",
                    current_year=read_latest_year_from_tech_log(tech_log),
                    elapsed_sec=elapsed,
                    note="canceled by user",
                )
                return {
                    "seed": seed,
                    "ok": False,
                    "returncode": rc,
                    "elapsed_sec": elapsed,
                    "run_dir": str(seed_dir),
                    "reused": reused,
                    "reason": "canceled",
                    "canceled": True,
                }

    elapsed = time.time() - started
    if rc != 0:
        emit_event(
            state="failed",
            current_year=read_latest_year_from_tech_log(tech_log),
            elapsed_sec=elapsed,
            note="cli run failed",
        )
        return {
            "seed": seed,
            "ok": False,
            "returncode": rc,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": "cli run failed",
            "canceled": False,
        }

    scan = scan_max_tech(tech_log)
    if not scan["ok"]:
        emit_event(
            state="failed",
            current_year=read_latest_year_from_tech_log(tech_log),
            elapsed_sec=elapsed,
            note=str(scan["reason"]),
        )
        return {
            "seed": seed,
            "ok": False,
            "returncode": 0,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": scan["reason"],
            "canceled": False,
        }

    emit_event(
        state="done",
        current_year=(scan.get("max_total_row") or {}).get("year"),
        max_total_unlocked_techs=scan.get("max_total_unlocked_techs"),
        max_tech_id=(scan.get("max_tech_row") or {}).get("tech_id"),
        elapsed_sec=elapsed,
        note="ok",
    )
    return {
        "seed": seed,
        "ok": True,
        "returncode": 0,
        "elapsed_sec": elapsed,
        "run_dir": str(seed_dir),
        "reused": reused,
        "reason": "",
        "canceled": False,
        "rows": scan["rows"],
        "max_total_unlocked_techs": scan["max_total_unlocked_techs"],
        "max_total_row": scan["max_total_row"],
        "max_tech_row": scan["max_tech_row"],
    }


def build_summary(
    cfg: SweepConfig,
    results: list[dict],
    elapsed_sec: float,
    *,
    total_requested: Optional[int] = None,
    stopped_early: bool = False,
) -> dict:
    ok_runs = [r for r in results if r.get("ok")]
    canceled_runs = [r for r in results if r.get("canceled")]
    fail_runs = [r for r in results if (not r.get("ok")) and (not r.get("canceled"))]

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

    total = len(results) if total_requested is None else total_requested
    return {
        "elapsed_sec": elapsed_sec,
        "total_seeds": total,
        "completed_seeds": len(results),
        "ok_runs": len(ok_runs),
        "failed_runs": len(fail_runs),
        "canceled_runs": len(canceled_runs),
        "stopped_early": stopped_early,
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
                "canceled",
                "max_total_unlocked_techs",
                "max_total_year",
                "max_total_country_index",
                "max_total_country_name",
                "max_total_country_culture",
                "max_total_tech_id",
                "max_total_tech_name",
                "max_tech_id",
                "max_tech_name",
                "max_tech_year",
                "max_tech_country_index",
                "max_tech_country_name",
                "max_tech_country_culture",
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
                    r.get("canceled", False),
                    r.get("max_total_unlocked_techs", ""),
                    max_total.get("year", ""),
                    max_total.get("country_index", ""),
                    max_total.get("country_name", ""),
                    max_total.get("country_culture", ""),
                    max_total.get("tech_id", ""),
                    max_total.get("tech_name", ""),
                    max_tid.get("tech_id", ""),
                    max_tid.get("tech_name", ""),
                    max_tid.get("year", ""),
                    max_tid.get("country_index", ""),
                    max_tid.get("country_name", ""),
                    max_tid.get("country_culture", ""),
                    r.get("rows", ""),
                    r.get("run_dir", ""),
                    r.get("reason", ""),
                ]
            )

    with (out_root / "seed_max_tech_summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)


def run_sweep(
    cfg: SweepConfig,
    seeds: Iterable[int],
    *,
    progress_cb: Optional[ProgressCallback] = None,
    seed_event_cb: Optional[SeedEventCallback] = None,
    cancel_event: Optional[threading.Event] = None,
) -> tuple[list[dict], dict]:
    seed_list = list(seeds)
    total = len(seed_list)
    if total == 0:
        raise ValueError("No seeds to run")

    def emit(msg: str) -> None:
        if progress_cb is not None:
            progress_cb(msg)

    def emit_seed(event: dict) -> None:
        if seed_event_cb is not None:
            seed_event_cb(event)

    started = time.time()
    results: list[dict] = []
    done = 0

    emit(
        f"Running {total} seeds with workers={cfg.workers}, "
        f"years=[{cfg.start_year},{cfg.end_year}], use_gpu={cfg.use_gpu}"
    )

    def make_exception_result(seed: int, exc: Exception) -> dict:
        return {
            "seed": seed,
            "ok": False,
            "returncode": -1,
            "elapsed_sec": 0.0,
            "run_dir": str(cfg.out_root / f"seed_{seed}"),
            "reused": False,
            "reason": f"exception: {exc}",
            "canceled": False,
        }

    def process_result(seed: int, result: dict) -> None:
        nonlocal done
        done += 1
        results.append(result)

        if result.get("ok"):
            emit(
                f"[{done}/{total}] seed={seed} ok max_total={result.get('max_total_unlocked_techs')} "
                f"max_tid={(result.get('max_tech_row') or {}).get('tech_id')}"
            )
        elif result.get("canceled"):
            emit(f"[{done}/{total}] seed={seed} canceled")
        else:
            emit(
                f"[{done}/{total}] seed={seed} fail rc={result.get('returncode')} "
                f"reason={result.get('reason')}"
            )

        emit_seed(
            {
                "seed": seed,
                "state": "finished",
                "current_year": (result.get("max_total_row") or {}).get("year"),
                "max_total_unlocked_techs": result.get("max_total_unlocked_techs"),
                "max_tech_id": (result.get("max_tech_row") or {}).get("tech_id"),
                "elapsed_sec": result.get("elapsed_sec"),
                "note": result.get("reason") or ("ok" if result.get("ok") else ""),
            }
        )

        partial_results = sorted(results, key=lambda r: int(r.get("seed", 0)))
        partial = build_summary(
            cfg,
            partial_results,
            time.time() - started,
            total_requested=total,
            stopped_early=(cancel_event.is_set() if cancel_event is not None else False),
        )
        write_results(cfg.out_root, partial_results, partial)

    with concurrent.futures.ThreadPoolExecutor(max_workers=cfg.workers) as ex:
        in_flight: dict[concurrent.futures.Future, int] = {}
        seed_iter = iter(seed_list)

        def submit_next() -> bool:
            if cancel_event is not None and cancel_event.is_set():
                return False
            try:
                seed = next(seed_iter)
            except StopIteration:
                return False
            emit_seed({"seed": seed, "state": "queued", "current_year": None, "note": ""})
            fut = ex.submit(run_one_seed, seed, cfg, cancel_event, seed_event_cb)
            in_flight[fut] = seed
            return True

        for _ in range(min(cfg.workers, total)):
            if not submit_next():
                break

        while in_flight:
            done_set, _ = concurrent.futures.wait(
                set(in_flight.keys()),
                timeout=0.2,
                return_when=concurrent.futures.FIRST_COMPLETED,
            )
            if not done_set:
                continue
            for fut in done_set:
                seed = in_flight.pop(fut)
                try:
                    result = fut.result()
                except Exception as exc:  # pragma: no cover
                    result = make_exception_result(seed, exc)
                process_result(seed, result)
                if cancel_event is None or not cancel_event.is_set():
                    submit_next()

    results.sort(key=lambda x: int(x.get("seed", 0)))
    elapsed = time.time() - started
    stopped_early = (cancel_event.is_set() and len(results) < total) if cancel_event is not None else False
    summary = build_summary(
        cfg,
        results,
        elapsed,
        total_requested=total,
        stopped_early=stopped_early,
    )
    write_results(cfg.out_root, results, summary)

    emit(
        f"Done in {elapsed:.2f}s. completed={summary['completed_seeds']}/{summary['total_seeds']}, "
        f"ok={summary['ok_runs']}, failed={summary['failed_runs']}, canceled={summary['canceled_runs']}. "
        f"Results in {cfg.out_root}"
    )
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

    p.add_argument("--gui", action="store_true", help="Launch a GUI")
    return p.parse_args(argv)


def run_cli_mode(args: argparse.Namespace) -> int:
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

    def printer(msg: str) -> None:
        print(msg, flush=True)

    _, summary = run_sweep(cfg, seeds, progress_cb=printer)
    print("\nSummary:")
    print(json.dumps(summary, indent=2))
    best_total = summary.get("best_by_unlocked_count") or {}
    best_total_detail = best_total.get("detail") or {}
    if best_total:
        print(
            "Best by unlocked count: "
            f"seed={best_total.get('seed')} "
            f"country={best_total_detail.get('country_name', '')} "
            f"culture={best_total_detail.get('country_culture', '')} "
            f"tech_id={best_total_detail.get('tech_id', '')} "
            f"tech_name={best_total_detail.get('tech_name', '')}"
        )
    best_tid = summary.get("best_by_tech_id") or {}
    best_tid_detail = best_tid.get("detail") or {}
    if best_tid:
        print(
            "Best by tech id: "
            f"seed={best_tid.get('seed')} "
            f"country={best_tid_detail.get('country_name', '')} "
            f"culture={best_tid_detail.get('country_culture', '')} "
            f"tech_id={best_tid_detail.get('tech_id', '')} "
            f"tech_name={best_tid_detail.get('tech_name', '')}"
        )
    return 0 if summary["failed_runs"] == 0 else 1


def launch_gui(defaults: argparse.Namespace) -> int:
    repo_root = Path(__file__).resolve().parent.parent
    try:
        import tkinter as tk
        from tkinter import messagebox, ttk
        from tkinter.scrolledtext import ScrolledText
    except Exception as exc:  # pragma: no cover
        print(f"GUI unavailable: {exc}", file=sys.stderr)
        return 2

    root = tk.Tk()
    root.title("WorldSim Seed Max Tech Finder")
    root.geometry("1400x900")
    root.minsize(1120, 760)

    exe_var = tk.StringVar(value=str(resolve_user_path(str(defaults.exe), repo_root)))
    config_var = tk.StringVar(value=str(resolve_user_path(str(defaults.config), repo_root)))
    out_var = tk.StringVar(value=str(resolve_user_path(str(defaults.out_root), repo_root)))

    sim_count_var = tk.StringVar(value=str(defaults.random_count) if defaults.random_count is not None else "100")
    random_seed_var = tk.StringVar(value="" if defaults.random_seed is None else str(defaults.random_seed))
    seed_min_var = tk.StringVar(value=str(defaults.seed_min))
    seed_max_var = tk.StringVar(value=str(defaults.seed_max))
    seed_start_var = tk.StringVar(value="" if defaults.seed_start is None else str(defaults.seed_start))
    seed_end_var = tk.StringVar(value="" if defaults.seed_end is None else str(defaults.seed_end))
    seeds_var = tk.StringVar(value=defaults.seeds or "")

    start_year_var = tk.StringVar(value=str(defaults.start_year))
    end_year_var = tk.StringVar(value=str(defaults.end_year))
    checkpoint_var = tk.StringVar(value=str(defaults.checkpoint_every_years))
    workers_var = tk.StringVar(value=str(defaults.workers))
    use_gpu_var = tk.StringVar(value="" if defaults.use_gpu is None else str(defaults.use_gpu))
    reuse_var = tk.BooleanVar(value=not defaults.no_reuse)
    include_initial_var = tk.BooleanVar(value=not defaults.exclude_initial_log_rows)

    status_var = tk.StringVar(value="Idle")
    run_info_var = tk.StringVar(value="Ready")

    quick = ttk.LabelFrame(root, text="Quick Start")
    quick.pack(fill="x", padx=10, pady=(10, 6))
    tk.Label(quick, text="Number of simulations (random):").grid(row=0, column=0, sticky="w", padx=(8, 6), pady=4)
    tk.Entry(quick, textvariable=sim_count_var, width=10).grid(row=0, column=1, sticky="w", pady=4)
    tk.Label(quick, text="Random seed (optional):").grid(row=0, column=2, sticky="w", padx=(18, 6), pady=4)
    tk.Entry(quick, textvariable=random_seed_var, width=10).grid(row=0, column=3, sticky="w", pady=4)
    tk.Label(quick, text="Seed min:").grid(row=0, column=4, sticky="w", padx=(18, 6), pady=4)
    tk.Entry(quick, textvariable=seed_min_var, width=12).grid(row=0, column=5, sticky="w", pady=4)
    tk.Label(quick, text="Seed max:").grid(row=0, column=6, sticky="w", padx=(18, 6), pady=4)
    tk.Entry(quick, textvariable=seed_max_var, width=12).grid(row=0, column=7, sticky="w", pady=4)
    tk.Label(quick, text="Workers:").grid(row=1, column=0, sticky="w", padx=(8, 6), pady=4)
    tk.Entry(quick, textvariable=workers_var, width=10).grid(row=1, column=1, sticky="w", pady=4)
    tk.Label(quick, text="Use GPU (blank/0/1):").grid(row=1, column=2, sticky="w", padx=(18, 6), pady=4)
    tk.Entry(quick, textvariable=use_gpu_var, width=10).grid(row=1, column=3, sticky="w", pady=4)
    tk.Label(
        quick,
        text="Tip: set simulations count, then click Start Sweep. You can stop anytime and partial results autosave.",
        anchor="w",
    ).grid(row=2, column=0, columnspan=8, sticky="w", padx=8, pady=(2, 8))

    ctrl = tk.Frame(root)
    ctrl.pack(fill="x", padx=10, pady=(0, 6))
    run_btn = tk.Button(ctrl, text="Start Sweep", command=lambda: on_run(), width=16)
    run_btn.pack(side="left")
    stop_btn = tk.Button(ctrl, text="Stop", command=lambda: on_stop(), state="disabled", width=10)
    stop_btn.pack(side="left", padx=(8, 0))
    tk.Label(ctrl, textvariable=status_var, anchor="w").pack(side="left", padx=(18, 10))
    tk.Label(ctrl, textvariable=run_info_var, anchor="w").pack(side="left")

    advanced = ttk.LabelFrame(root, text="Paths and Advanced Settings")
    advanced.pack(fill="x", padx=10, pady=(0, 8))

    def add_row(label: str, var: tk.StringVar, row: int) -> None:
        tk.Label(advanced, text=label, anchor="w").grid(row=row, column=0, sticky="w", padx=(8, 8), pady=2)
        tk.Entry(advanced, textvariable=var, width=95).grid(row=row, column=1, sticky="we", pady=2)

    add_row("CLI exe", exe_var, 0)
    add_row("Config", config_var, 1)
    add_row("Out root", out_var, 2)
    add_row("Manual seeds csv (optional, overrides random count)", seeds_var, 3)
    add_row("Manual seed start (optional)", seed_start_var, 4)
    add_row("Manual seed end (optional)", seed_end_var, 5)
    add_row("Start year", start_year_var, 6)
    add_row("End year", end_year_var, 7)
    add_row("Checkpoint every years", checkpoint_var, 8)
    tk.Checkbutton(advanced, text="Reuse existing runs", variable=reuse_var).grid(row=9, column=1, sticky="w", pady=2)
    tk.Checkbutton(advanced, text="Include initial tech log rows", variable=include_initial_var).grid(
        row=10, column=1, sticky="w", pady=2
    )
    advanced.grid_columnconfigure(1, weight=1)

    columns = ("seed", "status", "year", "max_total", "max_tid", "elapsed", "note")
    tree_frame = tk.Frame(root)
    tree_frame.pack(fill="both", expand=True, padx=10, pady=(0, 8))
    tree = ttk.Treeview(tree_frame, columns=columns, show="headings", height=18)
    tree.heading("seed", text="Seed")
    tree.heading("status", text="Status")
    tree.heading("year", text="Current Year")
    tree.heading("max_total", text="Max Unlocked")
    tree.heading("max_tid", text="Max Tech ID")
    tree.heading("elapsed", text="Elapsed (s)")
    tree.heading("note", text="Note")
    tree.column("seed", width=90, anchor="e")
    tree.column("status", width=130, anchor="w")
    tree.column("year", width=120, anchor="e")
    tree.column("max_total", width=120, anchor="e")
    tree.column("max_tid", width=110, anchor="e")
    tree.column("elapsed", width=100, anchor="e")
    tree.column("note", width=520, anchor="w")
    scroll = ttk.Scrollbar(tree_frame, orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.pack(side="left", fill="both", expand=True)
    scroll.pack(side="right", fill="y")

    log = ScrolledText(root, width=140, height=12)
    log.pack(fill="both", expand=False, padx=10, pady=(0, 10))

    work_thread: Optional[threading.Thread] = None
    cancel_event: Optional[threading.Event] = None
    ui_queue: "queue.SimpleQueue[tuple[str, object]]" = queue.SimpleQueue()
    row_by_seed: dict[int, str] = {}
    seed_view: dict[int, dict[str, str]] = {}

    def post(kind: str, payload: object) -> None:
        ui_queue.put((kind, payload))

    def append_log(message: str) -> None:
        log.insert("end", message + "\n")
        log.see("end")

    def set_running_state(running: bool) -> None:
        run_btn.config(state="disabled" if running else "normal")
        stop_btn.config(state="normal" if running else "disabled")
        if not running:
            run_info_var.set("Ready")

    def ensure_row(seed: int) -> None:
        if seed in row_by_seed:
            return
        state = {
            "seed": str(seed),
            "status": "pending",
            "year": "",
            "max_total": "",
            "max_tid": "",
            "elapsed": "",
            "note": "",
        }
        seed_view[seed] = state
        item_id = tree.insert(
            "",
            "end",
            values=(
                state["seed"],
                state["status"],
                state["year"],
                state["max_total"],
                state["max_tid"],
                state["elapsed"],
                state["note"],
            ),
        )
        row_by_seed[seed] = item_id

    def apply_seed_event(event: dict) -> None:
        seed = int(event["seed"])
        ensure_row(seed)
        state = seed_view[seed]
        if "state" in event and event["state"] is not None:
            state["status"] = str(event["state"])
        if event.get("current_year") is not None:
            state["year"] = str(event["current_year"])
        if event.get("max_total_unlocked_techs") is not None:
            state["max_total"] = str(event["max_total_unlocked_techs"])
        if event.get("max_tech_id") is not None:
            state["max_tid"] = str(event["max_tech_id"])
        if event.get("elapsed_sec") is not None:
            state["elapsed"] = f"{float(event['elapsed_sec']):.2f}"
        if "note" in event and event["note"] is not None:
            state["note"] = str(event["note"])
        tree.item(
            row_by_seed[seed],
            values=(
                state["seed"],
                state["status"],
                state["year"],
                state["max_total"],
                state["max_tid"],
                state["elapsed"],
                state["note"],
            ),
        )

    def reset_rows(seeds: list[int]) -> None:
        for item_id in tree.get_children():
            tree.delete(item_id)
        row_by_seed.clear()
        seed_view.clear()
        for seed in seeds:
            ensure_row(seed)
            apply_seed_event({"seed": seed, "state": "queued", "note": ""})

    def parse_optional_int(text: str) -> Optional[int]:
        raw = text.strip()
        return None if raw == "" else int(raw)

    def on_run() -> None:
        nonlocal work_thread, cancel_event
        if work_thread is not None and work_thread.is_alive():
            return
        try:
            sim_count = parse_optional_int(sim_count_var.get())
            random_seed = parse_optional_int(random_seed_var.get())
            seed_min = int(seed_min_var.get())
            seed_max = int(seed_max_var.get())
            seed_start = parse_optional_int(seed_start_var.get())
            seed_end = parse_optional_int(seed_end_var.get())
            seeds_csv = seeds_var.get().strip() or None
            random_count: Optional[int] = None
            if seeds_csv is None:
                random_count = sim_count
            seeds = parse_seed_values(
                random_count=random_count,
                random_seed=random_seed,
                seed_min=seed_min,
                seed_max=seed_max,
                seed_start=seed_start,
                seed_end=seed_end,
                seeds_csv=seeds_csv,
            )
            use_gpu_text = use_gpu_var.get().strip()
            use_gpu: Optional[bool]
            if use_gpu_text == "":
                use_gpu = None
            elif use_gpu_text in ("0", "1"):
                use_gpu = use_gpu_text == "1"
            else:
                raise ValueError("Use GPU must be blank, 0, or 1")

            cfg = SweepConfig(
                repo_root=repo_root,
                exe=resolve_user_path(exe_var.get().strip(), repo_root),
                config=resolve_user_path(config_var.get().strip(), repo_root),
                out_root=resolve_user_path(out_var.get().strip(), repo_root),
                start_year=int(start_year_var.get()),
                end_year=int(end_year_var.get()),
                checkpoint_every_years=max(1, int(checkpoint_var.get())),
                workers=max(1, int(workers_var.get())),
                use_gpu=use_gpu,
                reuse_existing=bool(reuse_var.get()),
                include_initial_log_rows=bool(include_initial_var.get()),
            )

            if cfg.end_year < cfg.start_year:
                raise ValueError("End year must be >= start year")
            if not cfg.exe.exists():
                raise FileNotFoundError(f"Executable not found: {cfg.exe}")
            if not cfg.config.exists():
                raise FileNotFoundError(f"Config not found: {cfg.config}")
        except Exception as exc:
            messagebox.showerror("Invalid Input", str(exc))
            return

        reset_rows(seeds)
        append_log("=" * 90)
        append_log(f"Starting run with {len(seeds)} seeds.")
        status_var.set("Running...")
        run_info_var.set(f"Queued: {len(seeds)} seeds")
        set_running_state(True)

        cancel_event = threading.Event()

        def worker() -> None:
            try:
                _, summary = run_sweep(
                    cfg,
                    seeds,
                    progress_cb=lambda msg: post("log", msg),
                    seed_event_cb=lambda ev: post("seed", ev),
                    cancel_event=cancel_event,
                )
                post("log", "Summary:")
                post("log", json.dumps(summary, indent=2))
                post("status", "Stopped" if summary.get("stopped_early") else "Done")
                post(
                    "run_info",
                    (
                        f"Completed {summary.get('completed_seeds', 0)}/{summary.get('total_seeds', 0)} | "
                        f"OK {summary.get('ok_runs', 0)} | Failed {summary.get('failed_runs', 0)} | "
                        f"Canceled {summary.get('canceled_runs', 0)}"
                    ),
                )
            except Exception as exc:
                post("log", f"ERROR: {exc}")
                post("status", "Error")
                post("run_info", "Error")
            finally:
                post("done", None)

        work_thread = threading.Thread(target=worker, daemon=True)
        work_thread.start()

    def on_stop() -> None:
        if work_thread is None or not work_thread.is_alive():
            return
            if cancel_event is not None and not cancel_event.is_set():
                cancel_event.set()
                status_var.set("Stopping...")
                run_info_var.set("Stop requested")
                append_log("Stop requested. Finishing in-flight seeds and autosaving partial results...")

    def pump_ui() -> None:
        processed = 0
        while processed < 600:
            try:
                kind, payload = ui_queue.get_nowait()
            except queue.Empty:
                break
            processed += 1
            if kind == "log":
                append_log(str(payload))
            elif kind == "seed":
                apply_seed_event(payload)  # type: ignore[arg-type]
            elif kind == "status":
                status_var.set(str(payload))
            elif kind == "run_info":
                run_info_var.set(str(payload))
            elif kind == "done":
                set_running_state(False)
        root.after(100, pump_ui)

    root.after(100, pump_ui)
    append_log("Quick start: set 'Number of simulations', then click 'Start Sweep'.")
    append_log("Manual mode: fill 'Manual seeds csv' or start/end in Advanced Settings.")
    root.mainloop()
    return 0


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.gui:
        return launch_gui(args)
    return run_cli_mode(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(130)
