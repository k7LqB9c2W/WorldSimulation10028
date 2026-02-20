#!/usr/bin/env python3
"""Web UI wrapper for seed_max_tech_finder.

This app keeps `tools/seed_max_tech_finder.py` as the execution engine and
adds a local web UI for:
- configuring a sweep
- starting/stopping runs
- live per-seed progress
- rolling logs + summary JSON

Unlike simple session-state-only designs, run state is stored server-side in a
module-global registry so reconnecting from a sleeping phone/tab can reattach
to the active run.
"""

from __future__ import annotations

import queue
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

import streamlit as st


TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
UI_DEBUG_LOG_PATH = REPO_ROOT / "out" / "seed_max_tech_web" / "ui_debug.log"
UI_DEBUG_MAX_BYTES = 2_000_000
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import seed_max_tech_finder as core  # noqa: E402
import seed_max_tech_web_runtime as runtime  # noqa: E402


@dataclass
class WebRun:
    run_id: str
    cfg: core.SweepConfig
    seeds: list[int]
    event_queue: queue.Queue[tuple[str, Any]]
    cancel_event: threading.Event
    thread: Optional[threading.Thread] = None
    running: bool = True
    logs: list[str] = field(default_factory=list)
    seed_rows: dict[int, dict[str, Any]] = field(default_factory=dict)
    summary: Optional[dict[str, Any]] = None
    results: list[dict[str, Any]] = field(default_factory=list)
    created_ts: float = field(default_factory=time.time)
    updated_ts: float = field(default_factory=time.time)
    lock: threading.Lock = field(default_factory=threading.Lock, repr=False)


MAX_STORED_RUNS = 12


def _init_state() -> None:
    if "selected_run_id" not in st.session_state:
        st.session_state.selected_run_id = None
    if "run_selector" not in st.session_state:
        st.session_state.run_selector = "<new run>"
    if "ui_debug_session" not in st.session_state:
        st.session_state.ui_debug_session = f"{int(time.time())}_{threading.get_ident()}"
    if "ui_debug_tick" not in st.session_state:
        st.session_state.ui_debug_tick = 0


def _to_use_gpu(choice: str) -> Optional[bool]:
    if choice == "CPU (0)":
        return False
    if choice == "GPU (1)":
        return True
    return None


def _stamp() -> str:
    return time.strftime("%H:%M:%S")


def _append_run_log(run: WebRun, message: str) -> None:
    with run.lock:
        run.logs.append(f"[{_stamp()}] {message}")
        if len(run.logs) > 4000:
            run.logs = run.logs[-4000:]
        run.updated_ts = time.time()


def _run_status(run: WebRun) -> str:
    return "running" if run.running else "done"


def _trim_runs_locked() -> None:
    if len(runtime.REGISTRY.runs) <= MAX_STORED_RUNS:
        return
    removable = sorted(
        (r for r in runtime.REGISTRY.runs.values() if not r.running),
        key=lambda r: r.created_ts,
    )
    while len(runtime.REGISTRY.runs) > MAX_STORED_RUNS and removable:
        victim = removable.pop(0)
        runtime.REGISTRY.runs.pop(victim.run_id, None)


def _create_run(cfg: core.SweepConfig, seeds: list[int]) -> WebRun:
    run_id = f"{time.strftime('%Y%m%d_%H%M%S')}_{int(time.time() * 1000) % 100000:05d}"
    run = WebRun(
        run_id=run_id,
        cfg=cfg,
        seeds=list(seeds),
        event_queue=queue.Queue(),
        cancel_event=threading.Event(),
        seed_rows={
            s: {
                "seed": s,
                "state": "queued",
                "current_year": "",
                "max_total": "",
                "max_tech_id": "",
                "elapsed_sec": "",
                "note": "",
            }
            for s in seeds
        },
    )

    def progress_cb(msg: str) -> None:
        run.event_queue.put(("log", msg))

    def seed_event_cb(event: dict[str, Any]) -> None:
        run.event_queue.put(("seed", event))

    def worker() -> None:
        try:
            results, summary = core.run_sweep(
                cfg,
                seeds,
                progress_cb=progress_cb,
                seed_event_cb=seed_event_cb,
                cancel_event=run.cancel_event,
            )
            run.event_queue.put(("done", {"results": results, "summary": summary}))
        except Exception as exc:  # pragma: no cover
            run.event_queue.put(("error", str(exc)))

    t = threading.Thread(target=worker, daemon=True)
    run.thread = t

    with runtime.REGISTRY.lock:
        runtime.REGISTRY.runs[run_id] = run
        runtime.REGISTRY.latest_run_id = run_id
        _trim_runs_locked()

    t.start()
    return run


def _get_run(run_id: Optional[str]) -> Optional[WebRun]:
    if not run_id:
        return None
    with runtime.REGISTRY.lock:
        return runtime.REGISTRY.runs.get(run_id)


def _all_runs_sorted() -> list[WebRun]:
    with runtime.REGISTRY.lock:
        return sorted(runtime.REGISTRY.runs.values(), key=lambda r: r.created_ts, reverse=True)


def _drain_run_events(run: WebRun) -> bool:
    changed = False
    while True:
        try:
            kind, payload = run.event_queue.get_nowait()
        except queue.Empty:
            break
        changed = True
        if kind == "log":
            _append_run_log(run, str(payload))
        elif kind == "seed":
            event = payload if isinstance(payload, dict) else {}
            seed = event.get("seed")
            if seed is None:
                continue
            with run.lock:
                row = run.seed_rows.get(
                    seed,
                    {
                        "seed": seed,
                        "state": "queued",
                        "current_year": "",
                        "max_total": "",
                        "max_tech_id": "",
                        "elapsed_sec": "",
                        "note": "",
                    },
                )
                if "state" in event:
                    row["state"] = event.get("state", row["state"])
                if event.get("current_year") is not None:
                    row["current_year"] = event.get("current_year")
                if event.get("max_total_unlocked_techs") is not None:
                    row["max_total"] = event.get("max_total_unlocked_techs")
                if event.get("max_tech_id") is not None:
                    row["max_tech_id"] = event.get("max_tech_id")
                if event.get("elapsed_sec") is not None:
                    row["elapsed_sec"] = f"{float(event.get('elapsed_sec', 0.0)):.2f}"
                if event.get("note") is not None:
                    row["note"] = str(event.get("note", ""))
                run.seed_rows[seed] = row
                run.updated_ts = time.time()
        elif kind == "done":
            payload_obj = payload if isinstance(payload, dict) else {}
            with run.lock:
                run.running = False
                run.results = payload_obj.get("results", [])
                run.summary = payload_obj.get("summary")
                run.updated_ts = time.time()
            _append_run_log(run, "Sweep finished.")
        elif kind == "error":
            with run.lock:
                run.running = False
                run.updated_ts = time.time()
            _append_run_log(run, f"ERROR: {payload}")
        else:
            _append_run_log(run, f"Unknown event: {kind}")
    return changed


def _run_selector_label(run: WebRun) -> str:
    started = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(run.created_ts))
    return f"{run.run_id} ({_run_status(run)} | runs={len(run.seeds)} | {started})"


def _append_ui_debug(message: str) -> None:
    try:
        UI_DEBUG_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        if UI_DEBUG_LOG_PATH.exists() and UI_DEBUG_LOG_PATH.stat().st_size > UI_DEBUG_MAX_BYTES:
            UI_DEBUG_LOG_PATH.unlink()
        with UI_DEBUG_LOG_PATH.open("a", encoding="utf-8") as f:
            f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {message}\n")
    except Exception:
        pass


def _read_ui_debug_tail(max_lines: int = 120) -> str:
    if not UI_DEBUG_LOG_PATH.exists():
        return "(no debug log yet)"
    try:
        lines = UI_DEBUG_LOG_PATH.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception as exc:
        return f"(failed to read debug log: {exc})"
    if not lines:
        return "(debug log is empty)"
    return "\n".join(lines[-max_lines:])


def main() -> None:
    st.set_page_config(page_title="WorldSim Run Explorer", layout="wide")
    _init_state()

    st.session_state.ui_debug_tick = int(st.session_state.ui_debug_tick) + 1
    repo_root = REPO_ROOT

    runs = _all_runs_sorted()
    run_ids = {r.run_id for r in runs}
    running_run = next((r for r in runs if r.running), None)
    if st.session_state.selected_run_id is not None and st.session_state.selected_run_id not in run_ids:
        st.session_state.selected_run_id = None
    if st.session_state.selected_run_id is None and running_run is not None:
        st.session_state.selected_run_id = running_run.run_id
    elif st.session_state.selected_run_id is None and runs:
        st.session_state.selected_run_id = runs[0].run_id

    st.title("WorldSim Run Explorer (Web)")
    st.caption("Engine: tools/seed_max_tech_finder.py")

    run_options = ["<new run>"] + [r.run_id for r in runs]
    label_by_id = {"<new run>": "<new run>"}
    for r in runs:
        label_by_id[r.run_id] = _run_selector_label(r)

    preferred_choice = st.session_state.selected_run_id or "<new run>"
    if preferred_choice not in run_options:
        preferred_choice = "<new run>"
    if st.session_state.run_selector not in run_options:
        st.session_state.run_selector = preferred_choice

    chosen = st.selectbox(
        "Saved run",
        options=run_options,
        format_func=lambda rid: label_by_id.get(rid, rid),
        key="run_selector",
    )
    if chosen == "<new run>":
        # Keep showing the active run even when selector is at <new run>, so
        # progress/table never disappears during a run.
        if running_run is not None:
            st.session_state.selected_run_id = running_run.run_id
        elif runs:
            st.session_state.selected_run_id = runs[0].run_id
        else:
            st.session_state.selected_run_id = None
    else:
        st.session_state.selected_run_id = chosen

    active_run = _get_run(st.session_state.selected_run_id)
    if active_run is None and running_run is not None:
        active_run = running_run
        st.session_state.selected_run_id = running_run.run_id
    if active_run is not None:
        _drain_run_events(active_run)

    _append_ui_debug(
        f"tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
        f"chosen={chosen} selected={st.session_state.selected_run_id} "
        f"runs={len(runs)} running={(running_run.run_id if running_run else '-')}"
    )

    left, right = st.columns(2)
    with left:
        exe_input = st.text_input("Simulation program path", value="out/cmake/release/bin/worldsim_cli.exe")
        config_input = st.text_input("Simulation config file", value="data/sim_config.toml")
        out_input = st.text_input("Results folder", value="out/seed_max_tech_web")
        workers = st.number_input("Parallel runs", min_value=1, max_value=128, value=core.default_workers(), step=1)
        use_gpu_choice = st.selectbox("Hardware mode", options=["CPU (0)", "GPU (1)", "Auto (unset)"], index=0)
        reuse_existing = st.checkbox("Reuse completed runs already on disk", value=True)
        include_initial_rows = st.checkbox("Include initial tech rows", value=True)

    with right:
        start_year = st.number_input("Start year", value=-5000, step=1)
        end_year = st.number_input("End year", value=2025, step=1)
        checkpoint_every = st.number_input("Save checkpoint every N years", min_value=1, value=50, step=1)

        seed_mode = st.selectbox("How should runs be selected?", options=["Random run count", "Seed range", "Specific seed list"])
        random_count: Optional[int] = None
        random_seed: Optional[int] = None
        seed_min = 1
        seed_max = 2_000_000_000
        seed_start: Optional[int] = None
        seed_end: Optional[int] = None
        seeds_csv: Optional[str] = None

        if seed_mode == "Random run count":
            random_count = int(st.number_input("How many runs?", min_value=1, value=1, step=1))
            random_seed_text = st.text_input("Randomizer seed (optional, for repeatability)", value="")
            random_seed = int(random_seed_text) if random_seed_text.strip() else None
            seed_min = int(st.number_input("Allowed seed minimum", min_value=1, value=1, step=1))
            seed_max = int(st.number_input("Allowed seed maximum", min_value=1, value=2_000_000_000, step=1))
        elif seed_mode == "Seed range":
            seed_start = int(st.number_input("Range start", value=1, step=1))
            seed_end = int(st.number_input("Range end", value=100, step=1))
        else:
            seeds_csv = st.text_input("Seed list (comma-separated)", value="1,2,3")

    c1, c2 = st.columns([1, 1])
    start_disabled = running_run is not None
    stop_target = active_run if (active_run is not None and active_run.running) else running_run
    stop_disabled = stop_target is None
    start_clicked = c1.button("Start Runs", disabled=start_disabled, width="stretch")
    stop_clicked = c2.button("Stop Active Run", disabled=stop_disabled, width="stretch")

    if start_clicked:
        _append_ui_debug(
            f"start_click tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
            f"selector={st.session_state.run_selector} selected={st.session_state.selected_run_id}"
        )
        try:
            if int(end_year) < int(start_year):
                raise ValueError("End year must be >= start year.")
            seeds = core.parse_seed_values(
                random_count=random_count,
                random_seed=random_seed,
                seed_min=seed_min,
                seed_max=seed_max,
                seed_start=seed_start,
                seed_end=seed_end,
                seeds_csv=seeds_csv,
            )
            cfg = core.SweepConfig(
                repo_root=repo_root,
                exe=core.resolve_user_path(exe_input, repo_root),
                config=core.resolve_user_path(config_input, repo_root),
                out_root=core.resolve_user_path(out_input, repo_root),
                start_year=int(start_year),
                end_year=int(end_year),
                checkpoint_every_years=max(1, int(checkpoint_every)),
                workers=max(1, int(workers)),
                use_gpu=_to_use_gpu(use_gpu_choice),
                reuse_existing=bool(reuse_existing),
                include_initial_log_rows=bool(include_initial_rows),
            )
            if not cfg.exe.exists():
                raise FileNotFoundError(f"Executable not found: {cfg.exe}")
            if not cfg.config.exists():
                raise FileNotFoundError(f"Config not found: {cfg.config}")

            run = _create_run(cfg, seeds)
            _append_run_log(
                run,
                f"Started run set: runs={len(seeds)} workers={cfg.workers} "
                f"years=[{cfg.start_year},{cfg.end_year}] use_gpu={cfg.use_gpu}",
            )
            st.session_state.selected_run_id = run.run_id
            _append_ui_debug(
                f"run_created tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
                f"run_id={run.run_id} runs_requested={len(seeds)} workers={cfg.workers}"
            )
            st.rerun()
        except Exception as exc:
            _append_ui_debug(
                f"start_error tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
                f"error={exc}"
            )
            st.error(str(exc))

    if stop_clicked and stop_target is not None:
        stop_target.cancel_event.set()
        _append_run_log(stop_target, "Stop requested by user.")
        _append_ui_debug(
            f"stop_click tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
            f"target={stop_target.run_id}"
        )

    active_run = _get_run(st.session_state.selected_run_id)
    if active_run is None and running_run is not None:
        active_run = running_run
        st.session_state.selected_run_id = running_run.run_id

    with st.expander("Debug / UI Diagnostics", expanded=False):
        st.write(
            {
                "debug_session": st.session_state.ui_debug_session,
                "debug_tick": int(st.session_state.ui_debug_tick),
                "run_selector_widget": st.session_state.run_selector,
                "selected_run_id": st.session_state.selected_run_id,
                "runs_in_memory": len(runs),
                "running_run_id": (running_run.run_id if running_run is not None else None),
                "active_run_id": (active_run.run_id if active_run is not None else None),
            }
        )
        st.caption(f"Debug log file: {UI_DEBUG_LOG_PATH}")
        st.text_area("UI debug log tail", value=_read_ui_debug_tail(), height=220)

    if active_run is None:
        _append_ui_debug(
            f"no_active_run tick={st.session_state.ui_debug_tick} sid={st.session_state.ui_debug_session} "
            f"selector={st.session_state.run_selector}"
        )
        st.info("No run selected. Configure inputs and click Start Runs.")
        return

    _drain_run_events(active_run)
    with active_run.lock:
        total = len(active_run.seeds)
        completed = sum(
            1
            for row in active_run.seed_rows.values()
            if str(row.get("state", "")) in {"finished", "done", "failed", "canceled"}
        )
        rows = [active_run.seed_rows[s] for s in sorted(active_run.seed_rows)]
        logs_text = "\n".join(active_run.logs[-800:])
        summary_obj = active_run.summary
        run_status = _run_status(active_run)

    state_counts: dict[str, int] = {}
    for row in rows:
        state = str(row.get("state", "queued")).strip().lower() or "queued"
        state_counts[state] = state_counts.get(state, 0) + 1
    queued_count = state_counts.get("queued", 0)
    active_count = sum(state_counts.get(s, 0) for s in ("starting", "running"))
    failed_count = state_counts.get("failed", 0)
    canceled_count = state_counts.get("canceled", 0)
    progress_value = (float(completed) / float(total)) if total > 0 else 0.0

    st.info(f"Run `{active_run.run_id}`: {run_status} | completed {completed}/{total}")
    st.progress(progress_value)
    st.caption(
        f"Completed: {completed}/{total} | Active: {active_count} | Queued: {queued_count} "
        f"| Failed: {failed_count} | Canceled: {canceled_count}"
    )

    st.subheader("Per-Run Progress")
    # Static table is more Safari/mobile-friendly than interactive dataframe grid.
    st.table(rows)

    if summary_obj is not None:
        detail = ((summary_obj.get("best_by_tech_id") or {}).get("detail") or {})
        country = detail.get("country_name", "")
        culture = detail.get("country_culture", "")
        tech_name = detail.get("tech_name", "")
        tech_id = detail.get("tech_id", "")
        year = detail.get("year", "")
        st.markdown(
            f"**Most Advanced Tech Country:** {country}  \n"
            f"**Culture:** {culture}  \n"
            f"**Tech:** {tech_name} (id {tech_id}) at year {year}"
        )

    log_col, summary_col = st.columns(2)
    with log_col:
        st.subheader("Logs")
        st.text_area("Run log", value=logs_text, height=320)
    with summary_col:
        st.subheader("Summary JSON")
        if summary_obj is not None:
            st.json(summary_obj)
        else:
            st.write("No summary yet.")

    if active_run.running:
        time.sleep(1.0)
        st.rerun()


if __name__ == "__main__":
    main()
