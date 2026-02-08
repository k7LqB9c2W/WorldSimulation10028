#!/usr/bin/env python3
"""Parallel seed search for target technology by year.

Runs `worldsim_cli` for many seeds (in parallel), enables optional tech unlock logging,
and reports which seeds reach a target technology by a target year.

Supports both CLI mode and a small Tkinter GUI (`--gui`).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import os
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional


@dataclass(frozen=True)
class SearchConfig:
    repo_root: Path
    exe: Path
    config: Path
    out_root: Path
    start_year: int
    end_year: int
    checkpoint_every_years: int
    target_year: int
    target_tech_id: Optional[int]
    target_tech_name: Optional[str]
    workers: int
    use_gpu: Optional[bool]
    reuse_existing: bool


def parse_seed_values(seed_start: Optional[int], seed_end: Optional[int], seeds_csv: Optional[str]) -> list[int]:
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
        raise ValueError("Provide either --seeds or both --seed-start and --seed-end")
    if seed_end < seed_start:
        raise ValueError("--seed-end must be >= --seed-start")
    return list(range(seed_start, seed_end + 1))


def resolve_user_path(path_value: str, repo_root: Path) -> Path:
    p = Path(path_value)
    if p.is_absolute():
        return p
    return (repo_root / p).resolve()


def to_cli_path(path: Path) -> str:
    """Return a path string suitable for worldsim_cli.exe on both Windows and WSL."""
    s = str(path)
    if s.startswith("/mnt/") and len(s) > 7:
        # /mnt/c/foo/bar -> C:\foo\bar
        drive = s[5].upper()
        rest = s[7:].replace("/", "\\")
        return f"{drive}:\\{rest}"
    return s


def normalize_tech_name(name: Optional[str]) -> Optional[str]:
    if name is None:
        return None
    s = name.strip().lower()
    return s or None


def matches_target(row: dict[str, str], target_tech_id: Optional[int], target_tech_name: Optional[str]) -> bool:
    if target_tech_id is not None:
        try:
            row_id = int(row.get("tech_id", ""))
        except ValueError:
            return False
        if row_id != target_tech_id:
            return False

    if target_tech_name is not None:
        row_name = row.get("tech_name", "").strip().lower()
        if target_tech_name not in row_name:
            return False

    return True


def scan_tech_log(
    tech_log_path: Path,
    *,
    target_year: int,
    target_tech_id: Optional[int],
    target_tech_name: Optional[str],
) -> dict:
    best: Optional[dict] = None
    if not tech_log_path.exists():
        return {
            "hit": False,
            "match": None,
            "reason": "tech log missing",
        }

    with tech_log_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                year = int(row.get("year", ""))
            except ValueError:
                continue
            if year > target_year:
                continue
            if not matches_target(row, target_tech_id, target_tech_name):
                continue
            candidate = {
                "year": year,
                "event_type": row.get("event_type", ""),
                "country_index": row.get("country_index", ""),
                "country_name": row.get("country_name", ""),
                "tech_id": row.get("tech_id", ""),
                "tech_name": row.get("tech_name", ""),
            }
            if best is None or year < int(best["year"]):
                best = candidate

    return {
        "hit": best is not None,
        "match": best,
        "reason": "" if best is not None else "no matching unlock <= target year",
    }


def build_cli_cmd(seed: int, cfg: SearchConfig, seed_out_dir: Path, tech_log_path: Path) -> list[str]:
    effective_end_year = min(cfg.end_year, cfg.target_year)
    cmd = [
        str(cfg.exe),
        "--seed",
        str(seed),
        "--config",
        to_cli_path(cfg.config),
        "--startYear",
        str(cfg.start_year),
        "--endYear",
        str(effective_end_year),
        "--checkpointEveryYears",
        str(cfg.checkpoint_every_years),
        "--outDir",
        to_cli_path(seed_out_dir),
        "--techUnlockLog",
        to_cli_path(tech_log_path),
        "--techUnlockLogIncludeInitial",
        "1",
    ]
    if cfg.target_tech_id is not None:
        cmd.extend(["--stopOnTechId", str(cfg.target_tech_id)])
    if cfg.use_gpu is not None:
        cmd.extend(["--useGPU", "1" if cfg.use_gpu else "0"])
    return cmd


def _load_json(path: Path) -> Optional[dict]:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def can_reuse_existing_run(seed: int, cfg: SearchConfig, seed_dir: Path, tech_log: Path) -> bool:
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

    expected_end_year = min(cfg.end_year, cfg.target_year)
    if meta_seed != seed or meta_start != cfg.start_year or meta_end != expected_end_year:
        return False

    if cfg.use_gpu is not None:
        backend = str(meta.get("backend", "")).strip().lower()
        want = "gpu" if cfg.use_gpu else "cpu"
        if backend != want:
            return False

    # Config path check (best-effort): compare file names when both are present.
    meta_cfg_raw = str(meta.get("config_path", "")).strip()
    if meta_cfg_raw:
        if Path(meta_cfg_raw).name != cfg.config.name:
            return False

    return True


def run_one_seed(seed: int, cfg: SearchConfig, cancel_event: Optional[threading.Event] = None) -> dict:
    seed_dir = cfg.out_root / f"seed_{seed}"
    tech_log = seed_dir / "tech_unlocks.csv"
    run_log = seed_dir / "seed_run.log"
    seed_dir.mkdir(parents=True, exist_ok=True)

    started = time.time()
    reused = False
    rc = 0

    if cancel_event is not None and cancel_event.is_set():
        return {
            "seed": seed,
            "ok": False,
            "hit": False,
            "returncode": -2,
            "elapsed_sec": 0.0,
            "run_dir": str(seed_dir),
            "reused": False,
            "reason": "canceled",
            "match": None,
            "canceled": True,
        }

    if cfg.reuse_existing and can_reuse_existing_run(seed, cfg, seed_dir, tech_log):
        reused = True
    else:
        cmd = build_cli_cmd(seed, cfg, seed_dir, tech_log)
        with run_log.open("w", encoding="utf-8") as logf:
            logf.write("COMMAND:\n")
            logf.write(" ".join(cmd) + "\n\n")
            proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, cwd=str(cfg.repo_root))
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
                time.sleep(0.1)

            if canceled:
                elapsed = time.time() - started
                return {
                    "seed": seed,
                    "ok": False,
                    "hit": False,
                    "returncode": rc,
                    "elapsed_sec": elapsed,
                    "run_dir": str(seed_dir),
                    "reused": reused,
                    "reason": "canceled",
                    "match": None,
                    "canceled": True,
                }

    elapsed = time.time() - started

    if rc != 0:
        return {
            "seed": seed,
            "ok": False,
            "hit": False,
            "returncode": rc,
            "elapsed_sec": elapsed,
            "run_dir": str(seed_dir),
            "reused": reused,
            "reason": "cli run failed",
            "match": None,
            "canceled": False,
        }

    match = scan_tech_log(
        tech_log,
        target_year=cfg.target_year,
        target_tech_id=cfg.target_tech_id,
        target_tech_name=cfg.target_tech_name,
    )

    return {
        "seed": seed,
        "ok": True,
        "hit": bool(match["hit"]),
        "returncode": 0,
        "elapsed_sec": elapsed,
        "run_dir": str(seed_dir),
        "reused": reused,
        "reason": match["reason"],
        "match": match["match"],
        "canceled": False,
    }


def write_results(out_root: Path, results: list[dict], summary: dict) -> None:
    out_root.mkdir(parents=True, exist_ok=True)

    rows_csv = out_root / "seed_search_results.csv"
    with rows_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "seed",
                "ok",
                "hit",
                "returncode",
                "elapsed_sec",
                "reused",
                "first_hit_year",
                "country_index",
                "country_name",
                "tech_id",
                "tech_name",
                "run_dir",
                "reason",
                "canceled",
            ]
        )
        for r in results:
            m = r.get("match") or {}
            writer.writerow(
                [
                    r.get("seed"),
                    r.get("ok"),
                    r.get("hit"),
                    r.get("returncode"),
                    f"{r.get('elapsed_sec', 0.0):.3f}",
                    r.get("reused"),
                    m.get("year", ""),
                    m.get("country_index", ""),
                    m.get("country_name", ""),
                    m.get("tech_id", ""),
                    m.get("tech_name", ""),
                    r.get("run_dir", ""),
                    r.get("reason", ""),
                    r.get("canceled", False),
                ]
            )

    with (out_root / "seed_search_summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)


def build_summary(cfg: SearchConfig, results: list[dict], total: int, elapsed_total: float, stopped_early: bool) -> dict:
    completed = len(results)
    hits = [r for r in results if r.get("hit")]
    ok_count = sum(1 for r in results if r.get("ok"))
    canceled_count = sum(1 for r in results if r.get("canceled"))
    fail_count = completed - ok_count - canceled_count

    earliest_hit = None
    if hits:
        earliest_hit = min(
            (
                {
                    "seed": r["seed"],
                    **(r.get("match") or {}),
                }
                for r in hits
            ),
            key=lambda m: int(m.get("year", 10**9)),
        )

    return {
        "total_seeds": total,
        "completed_seeds": completed,
        "ok_runs": ok_count,
        "failed_runs": fail_count,
        "canceled_runs": canceled_count,
        "hit_count": len(hits),
        "hit_ratio": (len(hits) / total) if total else 0.0,
        "hit_ratio_completed": (len(hits) / completed) if completed else 0.0,
        "elapsed_sec": elapsed_total,
        "stopped_early": stopped_early,
        "target": {
            "target_year": cfg.target_year,
            "target_tech_id": cfg.target_tech_id,
            "target_tech_name": cfg.target_tech_name,
        },
        "earliest_hit": earliest_hit,
    }


def run_search(
    cfg: SearchConfig,
    seeds: Iterable[int],
    progress_cb: Optional[Callable[[str], None]] = None,
    cancel_event: Optional[threading.Event] = None,
) -> tuple[list[dict], dict]:
    seed_list = list(seeds)
    total = len(seed_list)
    if total == 0:
        raise ValueError("No seeds to run")

    def emit(msg: str) -> None:
        if progress_cb is not None:
            progress_cb(msg)

    emit(
        f"Starting search: seeds={total}, workers={cfg.workers}, target_year={cfg.target_year}, "
        f"tech_id={cfg.target_tech_id}, tech_name={cfg.target_tech_name}"
    )

    started = time.time()
    results: list[dict] = []
    done = 0

    def make_exception_result(seed: int, exc: Exception) -> dict:
        return {
            "seed": seed,
            "ok": False,
            "hit": False,
            "returncode": -1,
            "elapsed_sec": 0.0,
            "run_dir": str(cfg.out_root / f"seed_{seed}"),
            "reused": False,
            "reason": f"exception: {exc}",
            "match": None,
            "canceled": False,
        }

    def process_result(seed: int, result: dict) -> None:
        nonlocal done
        results.append(result)
        done += 1

        hit_mark = "HIT" if result["hit"] else "MISS"
        if result.get("canceled"):
            hit_mark = "CANCELED"
        match = result.get("match") or {}
        detail = ""
        if result["hit"]:
            detail = (
                f" year={match.get('year')} country={match.get('country_name')} "
                f"tech={match.get('tech_name')}"
            )
        emit(
            f"[{done}/{total}] seed={seed} {hit_mark} rc={result['returncode']} "
            f"elapsed={result['elapsed_sec']:.2f}s{detail}"
        )

        partial = build_summary(
            cfg,
            sorted(results, key=lambda r: r["seed"]),
            total,
            time.time() - started,
            stopped_early=(cancel_event.is_set() if cancel_event is not None else False),
        )
        write_results(cfg.out_root, sorted(results, key=lambda r: r["seed"]), partial)

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
            fut = ex.submit(run_one_seed, seed, cfg, cancel_event)
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

    results.sort(key=lambda r: r["seed"])
    elapsed_total = time.time() - started
    stopped_early = cancel_event.is_set() if cancel_event is not None else False
    summary = build_summary(cfg, results, total, elapsed_total, stopped_early)

    write_results(cfg.out_root, results, summary)
    emit(
        f"Done in {elapsed_total:.2f}s. hits={summary['hit_count']}/{total}, "
        f"failed={summary['failed_runs']}, canceled={summary['canceled_runs']}. Results in {cfg.out_root}"
    )

    return results, summary


def default_workers() -> int:
    cpu = os.cpu_count() or 4
    return max(1, min(32, cpu))


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Find seeds that reach a target technology by a target year.")
    p.add_argument("--exe", default="out/cmake/release/bin/worldsim_cli.exe", help="Path to worldsim_cli executable")
    p.add_argument("--config", default="data/sim_config.toml", help="Path to simulation config")
    p.add_argument("--out-root", default="out/tech_seed_search", help="Output root for per-seed runs/results")

    p.add_argument("--seed-start", type=int)
    p.add_argument("--seed-end", type=int)
    p.add_argument("--seeds", help="Comma-separated explicit seed list (overrides --seed-start/--seed-end)")

    p.add_argument("--start-year", type=int, default=-5000)
    p.add_argument("--end-year", type=int, default=2025)
    p.add_argument("--checkpoint-every-years", type=int, default=50)

    p.add_argument("--target-year", type=int, required=False)
    p.add_argument("--tech-id", type=int)
    p.add_argument("--tech-name")

    p.add_argument("--workers", type=int, default=default_workers())
    p.add_argument("--use-gpu", choices=["0", "1"], help="Optional override for CLI --useGPU")
    p.add_argument("--no-reuse", action="store_true", help="Force rerun even if seed tech log already exists")

    p.add_argument("--gui", action="store_true", help="Launch a simple GUI")
    return p.parse_args(argv)


def run_cli_mode(args: argparse.Namespace) -> int:
    repo_root = Path(__file__).resolve().parent.parent
    if args.target_year is None:
        raise ValueError("--target-year is required in CLI mode")

    target_tech_id = args.tech_id
    target_tech_name = normalize_tech_name(args.tech_name)
    if target_tech_id is None and target_tech_name is None:
        raise ValueError("Provide --tech-id or --tech-name")

    seeds = parse_seed_values(args.seed_start, args.seed_end, args.seeds)
    if args.end_year < args.start_year:
        raise ValueError("--end-year must be >= --start-year")

    use_gpu = None
    if args.use_gpu is not None:
        use_gpu = args.use_gpu == "1"

    cfg = SearchConfig(
        repo_root=repo_root,
        exe=resolve_user_path(args.exe, repo_root),
        config=resolve_user_path(args.config, repo_root),
        out_root=resolve_user_path(args.out_root, repo_root),
        start_year=args.start_year,
        end_year=args.end_year,
        checkpoint_every_years=max(1, args.checkpoint_every_years),
        target_year=args.target_year,
        target_tech_id=target_tech_id,
        target_tech_name=target_tech_name,
        workers=max(1, args.workers),
        use_gpu=use_gpu,
        reuse_existing=(not args.no_reuse),
    )

    if not cfg.exe.exists():
        raise FileNotFoundError(f"Executable not found: {cfg.exe}")
    if not cfg.config.exists():
        raise FileNotFoundError(f"Config not found: {cfg.config}")

    def printer(msg: str) -> None:
        print(msg, flush=True)

    _, summary = run_search(cfg, seeds, progress_cb=printer)

    print("\nSummary:")
    print(json.dumps(summary, indent=2))
    return 0 if summary["failed_runs"] == 0 else 1


def launch_gui(defaults: argparse.Namespace) -> int:
    repo_root = Path(__file__).resolve().parent.parent
    try:
        import tkinter as tk
        from tkinter import messagebox
        from tkinter.scrolledtext import ScrolledText
    except Exception as exc:  # pragma: no cover
        print(f"GUI unavailable: {exc}", file=sys.stderr)
        return 2

    root = tk.Tk()
    root.title("WorldSim Seed Tech Finder")

    # Inputs
    exe_var = tk.StringVar(value=str(resolve_user_path(str(defaults.exe), repo_root)))
    config_var = tk.StringVar(value=str(resolve_user_path(str(defaults.config), repo_root)))
    out_var = tk.StringVar(value=str(resolve_user_path(str(defaults.out_root), repo_root)))
    seed_start_var = tk.StringVar(value=str(defaults.seed_start or 1))
    seed_end_var = tk.StringVar(value=str(defaults.seed_end or 100))
    seeds_var = tk.StringVar(value=defaults.seeds or "")
    start_year_var = tk.StringVar(value=str(defaults.start_year))
    end_year_var = tk.StringVar(value=str(defaults.end_year))
    checkpoint_var = tk.StringVar(value=str(defaults.checkpoint_every_years))
    target_year_var = tk.StringVar(value=str(defaults.target_year or 2025))
    tech_id_var = tk.StringVar(value="" if defaults.tech_id is None else str(defaults.tech_id))
    tech_name_var = tk.StringVar(value=defaults.tech_name or "")
    workers_var = tk.StringVar(value=str(defaults.workers))
    use_gpu_var = tk.StringVar(value="" if defaults.use_gpu is None else str(defaults.use_gpu))
    reuse_var = tk.BooleanVar(value=not defaults.no_reuse)

    frm = tk.Frame(root)
    frm.pack(fill="x", padx=10, pady=8)

    def add_row(label: str, var: tk.StringVar, row: int) -> None:
        tk.Label(frm, text=label, anchor="w").grid(row=row, column=0, sticky="w", padx=(0, 8), pady=2)
        tk.Entry(frm, textvariable=var, width=72).grid(row=row, column=1, sticky="we", pady=2)

    add_row("CLI exe", exe_var, 0)
    add_row("Config", config_var, 1)
    add_row("Out root", out_var, 2)
    add_row("Seeds csv (optional)", seeds_var, 3)
    add_row("Seed start", seed_start_var, 4)
    add_row("Seed end", seed_end_var, 5)
    add_row("Start year", start_year_var, 6)
    add_row("End year", end_year_var, 7)
    add_row("Checkpoint every years", checkpoint_var, 8)
    add_row("Target year", target_year_var, 9)
    add_row("Tech id (optional)", tech_id_var, 10)
    add_row("Tech name (optional)", tech_name_var, 11)
    add_row("Workers", workers_var, 12)
    add_row("Use GPU (blank/0/1)", use_gpu_var, 13)

    tk.Checkbutton(frm, text="Reuse existing logs", variable=reuse_var).grid(row=14, column=1, sticky="w", pady=2)

    frm.grid_columnconfigure(1, weight=1)

    log = ScrolledText(root, width=120, height=24)
    log.pack(fill="both", expand=True, padx=10, pady=(0, 10))

    status_var = tk.StringVar(value="Idle")
    tk.Label(root, textvariable=status_var, anchor="w").pack(fill="x", padx=10, pady=(0, 8))

    work_thread: Optional[threading.Thread] = None
    cancel_event: Optional[threading.Event] = None
    queue_lock = threading.Lock()
    messages: list[str] = []

    def push_message(msg: str) -> None:
        with queue_lock:
            messages.append(msg)

    def flush_messages() -> None:
        with queue_lock:
            pending = messages[:]
            messages.clear()
        for msg in pending:
            log.insert("end", msg + "\n")
            log.see("end")
        root.after(100, flush_messages)

    def on_run() -> None:
        nonlocal work_thread, cancel_event
        if work_thread is not None and work_thread.is_alive():
            return

        try:
            seeds = parse_seed_values(
                int(seed_start_var.get()) if seed_start_var.get().strip() else None,
                int(seed_end_var.get()) if seed_end_var.get().strip() else None,
                seeds_var.get().strip() or None,
            )
            target_year = int(target_year_var.get())
            tech_id = int(tech_id_var.get()) if tech_id_var.get().strip() else None
            tech_name = normalize_tech_name(tech_name_var.get())
            if tech_id is None and tech_name is None:
                raise ValueError("Provide tech id or tech name")

            use_gpu_text = use_gpu_var.get().strip()
            use_gpu: Optional[bool]
            if use_gpu_text == "":
                use_gpu = None
            elif use_gpu_text in ("0", "1"):
                use_gpu = use_gpu_text == "1"
            else:
                raise ValueError("Use GPU must be blank, 0, or 1")

            cfg = SearchConfig(
                repo_root=repo_root,
                exe=resolve_user_path(exe_var.get().strip(), repo_root),
                config=resolve_user_path(config_var.get().strip(), repo_root),
                out_root=resolve_user_path(out_var.get().strip(), repo_root),
                start_year=int(start_year_var.get()),
                end_year=int(end_year_var.get()),
                checkpoint_every_years=max(1, int(checkpoint_var.get())),
                target_year=target_year,
                target_tech_id=tech_id,
                target_tech_name=tech_name,
                workers=max(1, int(workers_var.get())),
                use_gpu=use_gpu,
                reuse_existing=bool(reuse_var.get()),
            )

            if not cfg.exe.exists():
                raise FileNotFoundError(f"Executable not found: {cfg.exe}")
            if not cfg.config.exists():
                raise FileNotFoundError(f"Config not found: {cfg.config}")

            if cfg.end_year < cfg.start_year:
                raise ValueError("End year must be >= start year")

        except Exception as exc:
            messagebox.showerror("Invalid Input", str(exc))
            return

        status_var.set("Running...")
        push_message("=" * 80)
        push_message(f"Starting run with {len(seeds)} seeds")
        cancel_event = threading.Event()
        run_btn.config(state="disabled")
        stop_btn.config(state="normal")

        def worker() -> None:
            try:
                _, summary = run_search(cfg, seeds, progress_cb=push_message, cancel_event=cancel_event)
                push_message("Summary:")
                push_message(json.dumps(summary, indent=2))
                root.after(0, lambda: status_var.set("Stopped" if summary.get("stopped_early") else "Done"))
            except Exception as exc:
                push_message(f"ERROR: {exc}")
                root.after(0, lambda: status_var.set("Error"))
            finally:
                root.after(0, lambda: run_btn.config(state="normal"))
                root.after(0, lambda: stop_btn.config(state="disabled"))

        work_thread = threading.Thread(target=worker, daemon=True)
        work_thread.start()

    def on_stop() -> None:
        if work_thread is None or not work_thread.is_alive():
            return
        if cancel_event is not None and not cancel_event.is_set():
            cancel_event.set()
            status_var.set("Stopping...")
            push_message("Stop requested. Finishing in-flight seeds and autosaving partial results...")

    btn_frame = tk.Frame(root)
    btn_frame.pack(anchor="w", padx=10, pady=(0, 8))
    run_btn = tk.Button(btn_frame, text="Run Search", command=on_run)
    run_btn.pack(side="left")
    stop_btn = tk.Button(btn_frame, text="Stop", command=on_stop, state="disabled")
    stop_btn.pack(side="left", padx=(8, 0))

    root.after(100, flush_messages)
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
