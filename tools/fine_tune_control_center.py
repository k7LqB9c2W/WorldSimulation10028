#!/usr/bin/env python3
"""GUI control center for unattended realism fine-tuning.

This wraps tools/fine_tune_realism.py with:
- live progress (latest iteration + best objective)
- graceful stop via stop flag
- autosave/apply best config ONLY if improved over baseline objective
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


@dataclass
class RunnerProfile:
    profile_path: Path
    tuner_script: Path
    config: Path
    schema: Path
    definitions: Path
    exe_dir: Path
    out_dir: Path
    max_iterations: int
    seed_jobs: int
    force_rebaseline: bool
    live_config_path: Path
    only_if_better_than_baseline: bool
    min_apply_improvement: float


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def resolve_path(repo_root: Path, raw: str) -> Path:
    p = Path(raw)
    if p.is_absolute():
        return p
    return (repo_root / p).resolve()


def load_profile(repo_root: Path, profile_path: Path) -> RunnerProfile:
    data = _load_json(profile_path)
    rt = data["runtime_defaults"]
    ap = data["apply_policy"]
    return RunnerProfile(
        profile_path=profile_path,
        tuner_script=resolve_path(repo_root, str(rt["tuner_script"])),
        config=resolve_path(repo_root, str(rt["config"])),
        schema=resolve_path(repo_root, str(rt["schema"])),
        definitions=resolve_path(repo_root, str(rt["definitions"])),
        exe_dir=resolve_path(repo_root, str(rt["exe_dir"])),
        out_dir=resolve_path(repo_root, str(rt["out_dir"])),
        max_iterations=int(rt["max_iterations"]),
        seed_jobs=int(rt["seed_jobs"]),
        force_rebaseline=bool(rt.get("force_rebaseline", False)),
        live_config_path=resolve_path(repo_root, str(ap["live_config_path"])),
        only_if_better_than_baseline=bool(ap.get("only_if_better_than_baseline", True)),
        min_apply_improvement=float(ap.get("min_apply_improvement", 0.0)),
    )


def latest_iteration_json(out_dir: Path) -> Optional[Path]:
    it_root = out_dir / "iterations"
    if not it_root.exists():
        return None
    cands = sorted(it_root.glob("iter_*/iteration.json"))
    return cands[-1] if cands else None


def read_baseline_objective(out_dir: Path) -> Optional[float]:
    p = out_dir / "baseline_objective.json"
    if not p.exists():
        return None
    try:
        data = _load_json(p)
        return float(data["tuning"]["objective"])
    except Exception:
        return None


def read_best_objective_from_partial(out_dir: Path) -> Optional[float]:
    # Prefer final_report.
    final = out_dir / "final_report.json"
    if final.exists():
        try:
            data = _load_json(final)
            return float(data.get("best_objective"))
        except Exception:
            pass

    # Fallback: latest iteration current best.
    itp = latest_iteration_json(out_dir)
    if itp is None:
        return None
    try:
        data = _load_json(itp)
        return float(data["best_so_far"]["objective"])
    except Exception:
        return None


def maybe_apply_best_config(
    out_dir: Path,
    live_config_path: Path,
    only_if_better_than_baseline: bool,
    min_apply_improvement: float,
) -> dict[str, Any]:
    best_cfg = out_dir / "best_sim_config.toml"
    baseline_obj = read_baseline_objective(out_dir)
    best_obj = read_best_objective_from_partial(out_dir)

    result: dict[str, Any] = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "best_config_exists": best_cfg.exists(),
        "baseline_objective": baseline_obj,
        "best_objective": best_obj,
        "applied": False,
        "reason": "",
        "live_config_path": str(live_config_path),
        "best_config_path": str(best_cfg),
    }

    if not best_cfg.exists():
        result["reason"] = "best_sim_config.toml missing"
        return result

    if only_if_better_than_baseline:
        if baseline_obj is None:
            result["reason"] = "baseline objective missing; skipped safe-apply"
            return result
        if best_obj is None:
            result["reason"] = "best objective missing; skipped safe-apply"
            return result
        if best_obj < baseline_obj + float(min_apply_improvement):
            result["reason"] = (
                f"not improved enough: best={best_obj:.6f}, baseline={baseline_obj:.6f}, "
                f"required_delta={min_apply_improvement:.6f}"
            )
            return result

    live_config_path.parent.mkdir(parents=True, exist_ok=True)
    backups = out_dir / "backups"
    backups.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if live_config_path.exists():
        backup_path = backups / f"sim_config_before_apply_{stamp}.toml"
        shutil.copyfile(live_config_path, backup_path)
        result["backup_path"] = str(backup_path)

    shutil.copyfile(best_cfg, live_config_path)
    result["applied"] = True
    result["reason"] = "applied best config to live config"
    return result


def build_tuner_command(
    py_exe: str,
    profile: RunnerProfile,
    out_dir: Path,
    stop_flag: Path,
    max_iterations: int,
    seed_jobs: int,
    force_rebaseline: bool,
) -> list[str]:
    cmd = [
        py_exe,
        str(profile.tuner_script),
        "--config",
        str(profile.config),
        "--schema",
        str(profile.schema),
        "--definitions",
        str(profile.definitions),
        "--exe-dir",
        str(profile.exe_dir),
        "--out-dir",
        str(out_dir),
        "--max-iterations",
        str(max_iterations),
        "--seed-jobs",
        str(seed_jobs),
        "--stop-flag",
        str(stop_flag),
        "--no-write-live-config",
    ]
    if force_rebaseline:
        cmd.append("--force-rebaseline")
    return cmd


def launch_gui(profile: RunnerProfile, repo_root: Path) -> int:
    try:
        import tkinter as tk
        from tkinter import messagebox
        from tkinter.scrolledtext import ScrolledText
    except Exception as exc:
        print(f"GUI unavailable: {exc}", file=sys.stderr)
        return 2

    root = tk.Tk()
    root.title("WorldSim Fine-Tune Control Center")

    out_dir_var = tk.StringVar(value=str(profile.out_dir))
    max_iter_var = tk.StringVar(value=str(profile.max_iterations))
    seed_jobs_var = tk.StringVar(value=str(profile.seed_jobs))
    force_rebase_var = tk.BooleanVar(value=bool(profile.force_rebaseline))
    live_cfg_var = tk.StringVar(value=str(profile.live_config_path))
    apply_if_better_var = tk.BooleanVar(value=bool(profile.only_if_better_than_baseline))
    apply_eps_var = tk.StringVar(value=f"{profile.min_apply_improvement:.6g}")

    frame = tk.Frame(root)
    frame.pack(fill="x", padx=10, pady=8)

    def row(label: str, var: tk.StringVar, r: int) -> None:
        tk.Label(frame, text=label, anchor="w").grid(row=r, column=0, sticky="w", padx=(0, 8), pady=2)
        tk.Entry(frame, textvariable=var, width=80).grid(row=r, column=1, sticky="we", pady=2)

    row("Out dir", out_dir_var, 0)
    row("Max iterations", max_iter_var, 1)
    row("Seed jobs", seed_jobs_var, 2)
    row("Live config path", live_cfg_var, 3)
    row("Min apply improvement", apply_eps_var, 4)

    tk.Checkbutton(frame, text="Force rebaseline", variable=force_rebase_var).grid(row=5, column=1, sticky="w", pady=2)
    tk.Checkbutton(frame, text="Apply best if better", variable=apply_if_better_var).grid(row=6, column=1, sticky="w", pady=2)

    frame.grid_columnconfigure(1, weight=1)

    status_var = tk.StringVar(value="Idle")
    iter_var = tk.StringVar(value="Iteration: -")
    best_var = tk.StringVar(value="Best objective: -")
    base_var = tk.StringVar(value="Baseline objective: -")

    tk.Label(root, textvariable=status_var, anchor="w").pack(fill="x", padx=10)
    tk.Label(root, textvariable=iter_var, anchor="w").pack(fill="x", padx=10)
    tk.Label(root, textvariable=best_var, anchor="w").pack(fill="x", padx=10)
    tk.Label(root, textvariable=base_var, anchor="w").pack(fill="x", padx=10, pady=(0, 6))

    log = ScrolledText(root, width=140, height=24)
    log.pack(fill="both", expand=True, padx=10, pady=(0, 10))

    btn_frame = tk.Frame(root)
    btn_frame.pack(anchor="w", padx=10, pady=(0, 8))

    run_btn: tk.Button
    stop_btn: tk.Button

    proc: Optional[subprocess.Popen[str]] = None
    stop_flag_path: Optional[Path] = None
    reader_thread: Optional[threading.Thread] = None
    q: queue.Queue[str] = queue.Queue()
    finalized = False
    run_started_at: Optional[float] = None
    last_log_at: Optional[float] = None
    last_log_line = ""

    def append(msg: str) -> None:
        log.insert("end", msg + "\n")
        log.see("end")

    def read_stdout(p: subprocess.Popen[str]) -> None:
        assert p.stdout is not None
        for line in p.stdout:
            q.put(line.rstrip("\n"))

    def refresh_iteration_info(active_out: Path) -> None:
        b = read_baseline_objective(active_out)
        if b is not None:
            base_var.set(f"Baseline objective: {b:.6f}")
        bp = read_best_objective_from_partial(active_out)
        if bp is not None:
            best_var.set(f"Best objective: {bp:.6f}")

        itp = latest_iteration_json(active_out)
        if itp is None:
            return
        try:
            it = _load_json(itp)
            n = int(it.get("iteration", -1))
            grp = str(it.get("subsystem_group", "?"))
            pe = it.get("parameter_edits", [])
            param = pe[0].get("path", "?") if pe else "?"
            accepted = bool(it.get("accepted", False))
            obj = float(it.get("objective_tuning", 0.0))
            delta = float(it.get("score_delta", 0.0))
            iter_var.set(
                f"Iteration: {n} | group={grp} | param={param} | objective={obj:.6f} | delta={delta:.6f} | accepted={accepted}"
            )
        except Exception:
            pass

    def finalize_run(active_out: Path, live_cfg: Path, only_if_better: bool, eps: float) -> None:
        nonlocal finalized
        if finalized:
            return
        finalized = True

        run_btn.config(state="normal")
        stop_btn.config(state="disabled")

        result = maybe_apply_best_config(
            active_out,
            live_cfg,
            only_if_better,
            eps,
        )
        _write_json(active_out / "apply_result.json", result)
        append("Apply result:")
        append(json.dumps(result, indent=2))

    def poll() -> None:
        nonlocal proc, finalized, last_log_at, last_log_line

        while True:
            try:
                msg = q.get_nowait()
            except queue.Empty:
                break
            append(msg)
            last_log_line = msg
            last_log_at = time.monotonic()

        if proc is not None:
            active_out = Path(out_dir_var.get().strip()).resolve()
            refresh_iteration_info(active_out)

            rc = proc.poll()
            if rc is None:
                now = time.monotonic()
                elapsed = 0.0 if run_started_at is None else max(0.0, now - run_started_at)
                stopping = stop_flag_path is not None and stop_flag_path.exists()
                prefix = "Stopping..." if stopping else "Running..."
                if last_log_at is None:
                    status_var.set(f"{prefix} {elapsed:.0f}s | waiting for first log")
                else:
                    since = max(0.0, now - last_log_at)
                    tail = (last_log_line[:92] + "...") if len(last_log_line) > 95 else last_log_line
                    status_var.set(f"{prefix} {elapsed:.0f}s | last log {since:.0f}s ago | {tail}")
                root.after(400, poll)
                return

            status_var.set(f"Finished (exit {rc})")
            try:
                live_cfg = Path(live_cfg_var.get().strip()).resolve()
                eps = float(apply_eps_var.get().strip())
            except Exception:
                live_cfg = profile.live_config_path
                eps = profile.min_apply_improvement

            finalize_run(
                active_out,
                live_cfg,
                bool(apply_if_better_var.get()),
                eps,
            )

            proc = None
            return

        root.after(400, poll)

    def on_run() -> None:
        nonlocal proc, stop_flag_path, reader_thread, finalized, run_started_at, last_log_at, last_log_line
        if proc is not None and proc.poll() is None:
            return

        try:
            active_out = Path(out_dir_var.get().strip()).resolve()
            max_iters = int(max_iter_var.get().strip())
            jobs = int(seed_jobs_var.get().strip())
            live_cfg = Path(live_cfg_var.get().strip()).resolve()
            eps = float(apply_eps_var.get().strip())
            if max_iters <= 0:
                raise ValueError("Max iterations must be > 0")
            if jobs <= 0:
                raise ValueError("Seed jobs must be > 0")
            if not profile.tuner_script.exists():
                raise FileNotFoundError(f"Tuner script not found: {profile.tuner_script}")
            if not profile.config.exists():
                raise FileNotFoundError(f"Config not found: {profile.config}")
            if not profile.schema.exists():
                raise FileNotFoundError(f"Schema not found: {profile.schema}")
            if not profile.definitions.exists():
                raise FileNotFoundError(f"Definitions not found: {profile.definitions}")
            if not profile.exe_dir.exists():
                raise FileNotFoundError(f"Exe dir not found: {profile.exe_dir}")
        except Exception as exc:
            messagebox.showerror("Invalid input", str(exc))
            return

        active_out.mkdir(parents=True, exist_ok=True)
        stop_flag_path = active_out / "stop.flag"
        if stop_flag_path.exists():
            stop_flag_path.unlink(missing_ok=True)

        cmd = build_tuner_command(
            py_exe=sys.executable,
            profile=profile,
            out_dir=active_out,
            stop_flag=stop_flag_path,
            max_iterations=max_iters,
            seed_jobs=jobs,
            force_rebaseline=bool(force_rebase_var.get()),
        )

        append("=" * 90)
        append("Starting tuner:")
        append(" ".join(cmd))

        env = os.environ.copy()
        env["PYTHONUNBUFFERED"] = "1"

        proc = subprocess.Popen(
            cmd,
            cwd=str(repo_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env,
        )
        finalized = False
        run_started_at = time.monotonic()
        last_log_at = None
        last_log_line = ""

        reader_thread = threading.Thread(target=read_stdout, args=(proc,), daemon=True)
        reader_thread.start()

        status_var.set("Running...")
        run_btn.config(state="disabled")
        stop_btn.config(state="normal")

        root.after(100, poll)

    def on_stop() -> None:
        nonlocal proc, stop_flag_path
        if proc is None or proc.poll() is not None:
            return
        if stop_flag_path is None:
            return
        stop_flag_path.parent.mkdir(parents=True, exist_ok=True)
        stop_flag_path.write_text("stop\n", encoding="utf-8")
        status_var.set("Stop requested...")
        append("Stop requested. Waiting for graceful checkpoint and autosave.")

    run_btn = tk.Button(btn_frame, text="Start Tuning", command=on_run)
    run_btn.pack(side="left")
    stop_btn = tk.Button(btn_frame, text="Stop + Save Best", command=on_stop, state="disabled")
    stop_btn.pack(side="left", padx=(8, 0))

    root.mainloop()
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="GUI wrapper for realism fine tuning")
    ap.add_argument("--profile", default="data/fine_tuning_runner.json")
    return ap.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    profile_path = resolve_path(repo_root, args.profile)
    if not profile_path.exists():
        raise SystemExit(f"Profile not found: {profile_path}")
    profile = load_profile(repo_root, profile_path)
    return launch_gui(profile, repo_root)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
