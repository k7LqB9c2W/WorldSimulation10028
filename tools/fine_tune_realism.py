#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import csv
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
import random
import shutil
import statistics
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    import tomli as toml_reader  # preferred explicit dependency
except Exception:
    try:
        import tomllib as toml_reader  # py311+ fallback
    except Exception as exc:  # pragma: no cover
        raise SystemExit(
            "TOML parser not available. Install tomli (`pip install tomli`) "
            f"or use Python 3.11+ (tomllib). Details: {exc}"
        )


TINY = 1e-12
REQUIRED_RUN_ARTIFACTS = ("run_meta.json", "run_summary.json", "timeseries.csv", "violations.json")


def clamp01(x: float) -> float:
    return max(0.0, min(1.0, x))


def relu(x: float) -> float:
    return max(0.0, x)


def closeness(x: float, target: float, tol: float) -> float:
    return clamp01(1.0 - abs(x - target) / max(TINY, tol))


def safe_mean(xs: List[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def safe_std(xs: List[float]) -> float:
    return statistics.pstdev(xs) if len(xs) > 1 else 0.0


def percentile(xs: List[float], p: float) -> float:
    if not xs:
        return 0.0
    ys = sorted(xs)
    p = max(0.0, min(1.0, p))
    pos = p * (len(ys) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    t = pos - lo
    return ys[lo] * (1.0 - t) + ys[hi] * t


def corr(x: List[float], y: List[float]) -> float:
    n = min(len(x), len(y))
    if n < 3:
        return 0.0
    x = x[:n]
    y = y[:n]
    mx = safe_mean(x)
    my = safe_mean(y)
    num = 0.0
    dx2 = 0.0
    dy2 = 0.0
    for i in range(n):
        dx = x[i] - mx
        dy = y[i] - my
        num += dx * dy
        dx2 += dx * dx
        dy2 += dy * dy
    den = math.sqrt(max(TINY, dx2 * dy2))
    return max(-1.0, min(1.0, num / den))


def parse_lat_bands(value: str) -> List[float]:
    if not value:
        return []
    out: List[float] = []
    for tok in value.split("|"):
        tok = tok.strip()
        if not tok:
            continue
        try:
            out.append(float(tok))
        except Exception:
            out.append(0.0)
    s = sum(max(0.0, v) for v in out)
    if s > TINY:
        return [max(0.0, v) / s for v in out]
    return out


def win_path(p: Path) -> str:
    s = str(p.resolve())
    if s.startswith("/mnt/") and len(s) > 6:
        drive = s[5].upper()
        rest = s[6:].replace("/", "\\")
        return f"{drive}:{rest}"
    return s.replace("/", "\\")


def hash16(path: Path) -> str:
    h = hashlib.sha256(path.read_bytes()).hexdigest()
    return h[:16]


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def load_toml(path: Path) -> Dict[str, Any]:
    with path.open("rb") as f:
        return toml_reader.load(f)


def format_toml_value(v: Any) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return f"{v:.12g}"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, list):
        return "[" + ", ".join(format_toml_value(x) for x in v) + "]"
    raise ValueError(f"Unsupported TOML value type: {type(v)!r}")


def dump_toml(cfg: Dict[str, Any], path: Path) -> None:
    lines: List[str] = []

    def emit_table(prefix: str, table: Dict[str, Any]) -> None:
        scalars: List[Tuple[str, Any]] = []
        subtables: List[Tuple[str, Dict[str, Any]]] = []
        for k, v in table.items():
            if isinstance(v, dict):
                subtables.append((k, v))
            else:
                scalars.append((k, v))

        if prefix:
            lines.append(f"[{prefix}]")
        for k, v in scalars:
            lines.append(f"{k} = {format_toml_value(v)}")
        if prefix and (scalars or subtables):
            lines.append("")

        for k, sub in subtables:
            child = f"{prefix}.{k}" if prefix else k
            emit_table(child, sub)

    emit_table("", cfg)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def get_param(cfg: Dict[str, Any], path: str) -> Any:
    cur: Any = cfg
    for key in path.split("."):
        if not isinstance(cur, dict) or key not in cur:
            raise KeyError(path)
        cur = cur[key]
    return cur


def set_param(cfg: Dict[str, Any], path: str, value: Any) -> None:
    keys = path.split(".")
    cur: Dict[str, Any] = cfg
    for key in keys[:-1]:
        nxt = cur.get(key)
        if not isinstance(nxt, dict):
            nxt = {}
            cur[key] = nxt
        cur = nxt
    cur[keys[-1]] = value


def apply_frozen_scenario(
    cfg: Dict[str, Any],
    frozen: Dict[str, Any],
) -> List[Dict[str, Any]]:
    if not bool(frozen.get("enabled", False)):
        return []
    req = frozen.get("required_paths", {})
    if not isinstance(req, dict):
        return []
    changes: List[Dict[str, Any]] = []
    for path, expected in req.items():
        had_old = True
        try:
            old = get_param(cfg, path)
        except Exception:
            had_old = False
            old = None
        if (not had_old) or (old != expected):
            set_param(cfg, path, copy.deepcopy(expected))
            changes.append({"path": path, "old": old, "new": expected})
    return changes


def parse_eps_text(s: str) -> Tuple[str, float]:
    t = s.strip().lower()
    if "% relative" in t:
        val = float(t.split("%", 1)[0]) / 100.0
        return ("relative", val)
    if "absolute" in t:
        val = float(t.split("absolute", 1)[0].strip().split()[-1])
        return ("absolute", val)
    # fallback literal
    return ("absolute", float(t))


def metric_max_error(a: List[float], b: List[float], mode: str) -> float:
    n = min(len(a), len(b))
    if n == 0:
        return float("inf")
    errs: List[float] = []
    for i in range(n):
        av = a[i]
        bv = b[i]
        if mode == "relative":
            errs.append(abs(av - bv) / max(abs(av), TINY))
        else:
            errs.append(abs(av - bv))
    return max(errs) if errs else float("inf")


def read_timeseries(path: Path) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            out: Dict[str, Any] = {}
            for k, v in row.items():
                if v is None:
                    out[k] = 0.0
                    continue
                vv = v.strip()
                if k == "pop_share_by_lat_band":
                    out[k] = parse_lat_bands(vv)
                    continue
                try:
                    out[k] = float(vv)
                except Exception:
                    out[k] = vv
            rows.append(out)
    return rows


@dataclass
class SeedEval:
    seed: int
    total_score_seed: float
    base_score_seed: float
    penalties: float
    hardfails: List[str]
    violations: List[Dict[str, Any]]
    checkpoint_scores: List[float]
    key_series: Dict[str, List[float]]
    top_violations: List[Dict[str, Any]]
    run_summary: Dict[str, Any]


def check_metric_availability(
    out_dir: Path,
    defs: Dict[str, Any],
) -> Tuple[bool, Dict[str, Any], List[Dict[str, Any]]]:
    req_artifacts = ["run_meta.json", "run_summary.json", "timeseries.csv", "violations.json"]
    missing_artifacts = [a for a in req_artifacts if not (out_dir / a).exists()]
    missing: Dict[str, Any] = {
        "missing_artifacts": missing_artifacts,
        "missing_timeseries_columns": [],
        "missing_run_meta_keys": [],
        "missing_run_summary_keys": [],
    }
    violations: List[Dict[str, Any]] = []

    ts_rows: List[Dict[str, Any]] = []
    if (out_dir / "timeseries.csv").exists():
        ts_rows = read_timeseries(out_dir / "timeseries.csv")
        cols = set(ts_rows[0].keys() if ts_rows else [])
        for c in defs["required_timeseries_columns"]:
            if c not in cols:
                missing["missing_timeseries_columns"].append(c)
    else:
        missing["missing_timeseries_columns"] = list(defs["required_timeseries_columns"])

    if (out_dir / "run_meta.json").exists():
        meta = load_json(out_dir / "run_meta.json")
        for k in defs["required_run_meta_keys"]:
            if k not in meta:
                missing["missing_run_meta_keys"].append(k)
    else:
        missing["missing_run_meta_keys"] = list(defs["required_run_meta_keys"])

    if (out_dir / "run_summary.json").exists():
        rs = load_json(out_dir / "run_summary.json")
        for k in defs["required_run_summary_keys"]:
            if k not in rs:
                missing["missing_run_summary_keys"].append(k)
    else:
        missing["missing_run_summary_keys"] = list(defs["required_run_summary_keys"])

    ok = not any(
        [
            missing["missing_artifacts"],
            missing["missing_timeseries_columns"],
            missing["missing_run_meta_keys"],
            missing["missing_run_summary_keys"],
        ]
    )
    if not ok:
        violations.append(
            {
                "id": "MISSING_METRIC",
                "severity": 100.0,
                "hardfail": True,
                "details": missing,
            }
        )
    return ok, missing, violations


def evaluate_seed_run(
    seed: int,
    out_dir: Path,
    defs: Dict[str, Any],
    write_eval_artifacts: bool = True,
) -> SeedEval:
    t = defs["thresholds"]
    hardfail_ids = set(defs["hard_fails"])
    anti_ids = set(defs["anti_loophole_ids"])

    metric_ok, missing, violations = check_metric_availability(out_dir, defs)
    ts = read_timeseries(out_dir / "timeseries.csv") if (out_dir / "timeseries.csv").exists() else []
    rs_raw = load_json(out_dir / "run_summary.json") if (out_dir / "run_summary.json").exists() else {}

    if not ts:
        violations.append({"id": "MISSING_METRIC", "severity": 100.0, "hardfail": True, "details": {"empty_timeseries": True}})

    years = [int(r.get("year", 0.0)) for r in ts]
    pop = [float(r.get("world_pop_total", 0.0)) for r in ts]
    food = [float(r.get("world_food_adequacy_index", 0.0)) for r in ts]
    pop_growth = [float(r.get("world_pop_growth_rate_annual", 0.0)) for r in ts]
    trade = [float(r.get("world_trade_intensity", 0.0)) for r in ts]
    urban = [float(r.get("world_urban_share_proxy", 0.0)) for r in ts]
    tech = [float(r.get("world_tech_capability_index_median", 0.0)) for r in ts]
    disease_rate = [float(r.get("world_disease_death_rate", 0.0)) for r in ts]
    fam_exp = [float(r.get("famine_exposure_share_t", 0.0)) for r in ts]
    migration = [float(r.get("migration_rate_t", 0.0)) for r in ts]
    market = [float(r.get("market_access_median", 0.0)) for r in ts]
    hab_small = [float(r.get("habitable_cell_share_pop_gt_small", 0.0)) for r in ts]
    coastal = [float(r.get("pop_share_coastal_vs_inland", 0.0)) for r in ts]
    river = [float(r.get("pop_share_river_proximal", 0.0)) for r in ts]
    health_cap = [float(r.get("health_capability_index", 0.0)) for r in ts]
    storage_cap = [float(r.get("storage_capability_index", 0.0)) for r in ts]
    logistics_cap = [float(r.get("logistics_capability_index", 0.0)) for r in ts]
    transport_cost = [float(r.get("transport_cost_index", 0.0)) for r in ts]
    long_trade_proxy = [float(r.get("long_distance_trade_proxy", 0.0)) for r in ts]
    spoilage = [float(r.get("spoilage_kcal", 0.0)) for r in ts]
    storage_loss = [float(r.get("storage_loss_kcal", 0.0)) for r in ts]
    avail_before = [float(r.get("available_kcal_before_losses", 0.0)) for r in ts]
    extraction = [float(r.get("extraction_index", 0.0)) for r in ts]

    fam_count = [float(r.get("famine_wave_count", 0.0)) for r in ts]
    epi_count = [float(r.get("epidemic_wave_count", 0.0)) for r in ts]
    war_count = [float(r.get("major_war_count", 0.0)) for r in ts]
    mig_count = [float(r.get("mass_migration_count", 0.0)) for r in ts]

    def window_years(i: int) -> float:
        if i <= 0:
            return max(1.0, float(t.get("depletion_monotonic_window_M", 8)) * 25.0)
        return max(1.0, float(years[i] - years[i - 1]))

    # Hard-fail BROKEN_ACCOUNTING from invariant check.
    if isinstance(rs_raw.get("invariants"), dict) and not bool(rs_raw["invariants"].get("ok", True)):
        violations.append(
            {
                "id": "BROKEN_ACCOUNTING",
                "severity": 100.0,
                "hardfail": True,
                "details": {"message": rs_raw["invariants"].get("message", "")},
            }
        )

    # Extinction persistent.
    floor = float(t["extinction_pop_floor"])
    grace = float(t["extinction_grace_years"])
    first_below_idx = None
    for i, p in enumerate(pop):
        if p < floor:
            if first_below_idx is None:
                first_below_idx = i
            yrs = years[i] - years[first_below_idx]
            if yrs > grace:
                violations.append({"id": "EXTINCTION_PERSISTENT", "severity": 100.0, "hardfail": True, "details": {"years_below": yrs}})
                break
        else:
            first_below_idx = None

    # Compute checkpoint component scores.
    L = int(t["coupling_lag_L"])
    W = int(t["corr_window_W"])
    rates_w = defs["shock_rate_weights"]
    ck_scores: List[float] = []
    major_war_rate: List[float] = []
    famine_wave_rate: List[float] = []
    epidemic_wave_rate: List[float] = []
    migration_wave_rate: List[float] = []
    adequacy_score: List[float] = []
    for i in range(len(ts)):
        wy = window_years(i)
        wc = wy / 100.0
        pop_avg = pop[i] if i == 0 else 0.5 * (pop[i] + pop[i - 1])
        scale = max(pop_avg / float(t["rate_pop_base"]), TINY)
        war_r = (war_count[i] / max(wc, TINY)) / scale
        fam_r = (fam_count[i] / max(wc, TINY)) / scale
        epi_r = (epi_count[i] / max(wc, TINY)) / scale
        mm_r = (mig_count[i] / max(wc, TINY)) / scale
        major_war_rate.append(war_r)
        famine_wave_rate.append(fam_r)
        epidemic_wave_rate.append(epi_r)
        migration_wave_rate.append(mm_r)

        if bool(t["use_sigmoid_adequacy"]):
            ad = 1.0 / (1.0 + math.exp(-float(t["sigmoid_k"]) * (food[i] - 1.0)))
        else:
            ad = clamp01(food[i] / 1.0)
        adequacy_score.append(ad)

        # Geography
        g_settle = clamp01(hab_small[i] / float(t["settlement_target_share"]))
        g_access = clamp01((coastal[i] + river[i]) / float(t["access_target_sum"]))
        bands = ts[i].get("pop_share_by_lat_band", [])
        if not isinstance(bands, list):
            bands = []
        ent = 0.0
        n_b = len(bands)
        for p_i in bands:
            p_i = max(0.0, float(p_i))
            if p_i > TINY:
                ent += -p_i * math.log(p_i)
        if n_b >= 2:
            ent /= math.log(float(n_b))
        g_lat = clamp01(ent / float(t["lat_entropy_target"]))
        geography = 0.45 * g_settle + 0.35 * g_access + 0.20 * g_lat

        # Constraint
        shock_rate = (
            float(rates_w["a_famine"]) * fam_r
            + float(rates_w["b_epidemic"]) * epi_r
            + float(rates_w["c_war"]) * war_r
        )
        c_adequacy = ad
        c_shocks = clamp01(shock_rate / float(t["shock_min_rate"]))
        if tech[i] < float(t["capability_T1"]):
            c_growth = closeness(pop_growth[i], float(t["lowcap_growth_target"]), float(t["lowcap_growth_tol"]))
        else:
            c_growth = 1.0
        constraint = 0.45 * c_adequacy + 0.25 * c_shocks + 0.30 * c_growth

        # Coupling
        if i >= L:
            d_adequacy = adequacy_score[i] - adequacy_score[i - L]
            d_migration = migration[i] - migration[i - L]
            d_conflict = major_war_rate[i] - major_war_rate[i - L]
            d_market = market[i] - market[i - L]
            d_fam_exp = fam_exp[i] - fam_exp[i - L]
            shock = relu(-d_adequacy)
            if shock > 0.0:
                rr_m = relu(d_migration) / max(shock, TINY)
                k_m = closeness(rr_m, float(t["response_ratio_target"]), float(t["response_ratio_tol"]))
                rr_w = relu(d_conflict) / max(shock, TINY)
                k_w = closeness(rr_w, float(t["response_ratio_target"]), float(t["response_ratio_tol"]))
            else:
                k_m = 1.0
                k_w = 1.0
            if d_market > 0.0 and d_fam_exp > 0.0:
                k_b = 0.0
            elif d_market > 0.0:
                rr_b = relu(-d_fam_exp) / max(relu(d_market), TINY)
                k_b = closeness(rr_b, float(t["response_ratio_target"]), float(t["response_ratio_tol"]))
            else:
                k_b = 0.5
            coupling = 0.40 * k_m + 0.35 * k_w + 0.25 * k_b
        else:
            coupling = 0.5

        # Regime consistency
        if tech[i] < float(t["capability_T1"]) and i + 1 >= W:
            x = urban[i + 1 - W : i + 1]
            y = disease_rate[i + 1 - W : i + 1]
            r = corr(x, y)
            r_score = closeness(r, float(t["lowcap_disease_corr_target"]), float(t["lowcap_disease_corr_tol"]))
        else:
            r_score = 0.5

        if health_cap[i] >= float(t["health_threshold"]):
            h_score = closeness(disease_rate[i], float(t["disease_low_target"]), float(t["disease_low_tol"]))
        else:
            h_score = 0.5
        regime = 0.60 * r_score + 0.40 * h_score

        ck = (
            float(t["wG"]) * geography
            + float(t["wC"]) * constraint
            + float(t["wK"]) * coupling
            + float(t["wR"]) * regime
        )
        ck_scores.append(ck)

    # Anti-loophole checks.
    n_var = int(t["adequacy_var_window_N"])
    if len(food) >= n_var:
        win = food[-n_var:]
        var = statistics.pvariance(win) if len(win) > 1 else 0.0
        loss_share = (spoilage[-1] + storage_loss[-1]) / max(avail_before[-1], TINY)
        if tech[-1] < float(t["capability_T1"]) and var < float(t["VarMin"]):
            if not (storage_cap[-1] >= float(t["storage_S1"]) and loss_share >= float(t["loss_L1"])):
                sev = clamp01((float(t["VarMin"]) - var) / max(float(t["VarMin"]), TINY)) * 100.0
                violations.append(
                    {
                        "id": "STORAGE_SMOOTHING_CHEAT",
                        "severity": sev,
                        "hardfail": False,
                        "details": {"adequacy_var": var, "loss_share": loss_share},
                    }
                )

    if ts:
        if tech[-1] < float(t["capability_T1"]) and long_trade_proxy[-1] > float(t["long_trade_share_max"]):
            if not (
                logistics_cap[-1] >= float(t["logistics_R1"])
                and transport_cost[-1] >= float(t["transport_C1"])
            ):
                exc = max(0.0, long_trade_proxy[-1] - float(t["long_trade_share_max"]))
                sev = clamp01(exc / max(float(t["long_trade_share_max"]), TINY)) * 100.0
                violations.append(
                    {
                        "id": "TRANSPORT_CHEAT",
                        "severity": sev,
                        "hardfail": False,
                        "details": {"long_distance_trade_proxy": long_trade_proxy[-1]},
                    }
                )

    m_win = int(t["depletion_monotonic_window_M"])
    if len(extraction) >= m_win and len(tech) >= m_win:
        ex_win = extraction[-m_win:]
        tech_win = tech[-m_win:]
        mono = all(ex_win[i] <= ex_win[i + 1] + 1e-9 for i in range(len(ex_win) - 1))
        tech_growth = tech_win[-1] - tech_win[0]
        if mono and tech_growth <= 1e-6:
            slope = (ex_win[-1] - ex_win[0]) / max(1.0, float(m_win - 1))
            sev = clamp01(slope / max(ex_win[-1] + 1.0, 1.0)) * 100.0
            violations.append(
                {
                    "id": "DEPLETION_IGNORED",
                    "severity": sev,
                    "hardfail": False,
                    "details": {"slope": slope, "window": m_win},
                }
            )

    # Penalties.
    def pmax_for(vid: str) -> float:
        if vid in hardfail_ids:
            return float(t["Pmax_major"])
        if vid in anti_ids:
            return float(t["Pmax_medium"])
        return float(t["Pmax_minor"])

    penalties = 0.0
    for v in violations:
        sev = clamp01(float(v.get("severity", 0.0)) / 100.0)
        penalties += pmax_for(str(v.get("id", ""))) * (sev * sev)

    base_score_seed = safe_mean(ck_scores)
    total_score_seed = 100.0 * base_score_seed - penalties
    hardfails = sorted({str(v["id"]) for v in violations if bool(v.get("hardfail", False))})
    top_violations = sorted(violations, key=lambda x: float(x.get("severity", 0.0)), reverse=True)[:10]

    summary = {
        "seed": seed,
        "base_score_seed": base_score_seed,
        "total_score": total_score_seed,
        "penalty_points": penalties,
        "gates": {
            "metric_availability": metric_ok,
            "hardfails": hardfails,
            "hardfail_count": len(hardfails),
        },
        "top_violations": top_violations,
        "scalar_metrics": {
            "world_pop_total_final": pop[-1] if pop else 0.0,
            "world_food_adequacy_index_final": food[-1] if food else 0.0,
            "world_trade_intensity_final": trade[-1] if trade else 0.0,
            "world_urban_share_proxy_final": urban[-1] if urban else 0.0,
            "major_war_rate_final": major_war_rate[-1] if major_war_rate else 0.0,
            "famine_wave_rate_final": famine_wave_rate[-1] if famine_wave_rate else 0.0,
            "epidemic_wave_rate_final": epidemic_wave_rate[-1] if epidemic_wave_rate else 0.0,
        },
        "checkpoint_scores": ck_scores,
        "checkpoints": rs_raw.get("checkpoints", []),
    }

    if write_eval_artifacts:
        write_json(out_dir / "violations.json", {"violations": violations})
        write_json(out_dir / "run_summary.json", summary)
        meta_path = out_dir / "run_meta.json"
        meta = load_json(meta_path) if meta_path.exists() else {}
        meta["goals_version"] = defs.get("goals_version", "realism-envelope-v7")
        meta["evaluator_version"] = defs.get("evaluator_version", "v7")
        meta["definitions_version"] = defs.get("definitions_version", "v7")
        meta["scoring_version"] = defs.get("scoring_version", "v7")
        meta["definitions_values"] = defs.get("thresholds", {})
        write_json(meta_path, meta)
    return SeedEval(
        seed=seed,
        total_score_seed=total_score_seed,
        base_score_seed=base_score_seed,
        penalties=penalties,
        hardfails=hardfails,
        violations=violations,
        checkpoint_scores=ck_scores,
        key_series={
            "world_pop_total": pop,
            "world_food_adequacy_index": food,
            "habitable_cell_share_pop_gt_small": hab_small,
            "world_trade_intensity": trade,
            "world_urban_share_proxy": urban,
            "major_war_rate": major_war_rate,
            "famine_wave_rate": famine_wave_rate,
            "epidemic_wave_rate": epidemic_wave_rate,
        },
        top_violations=top_violations,
        run_summary=summary,
    )


def compare_metric_series(
    a: SeedEval, b: SeedEval, eps_map: Dict[str, str]
) -> Tuple[bool, List[Dict[str, Any]]]:
    details: List[Dict[str, Any]] = []
    ok = True
    for metric, eps_txt in eps_map.items():
        mode, eps = parse_eps_text(eps_txt)
        if metric == "event_rates_per_century_per_billion":
            keys = ["major_war_rate", "famine_wave_rate", "epidemic_wave_rate"]
            sub = []
            for k in keys:
                sa_k = a.key_series.get(k, [])
                sb_k = b.key_series.get(k, [])
                sub.append(metric_max_error(sa_k, sb_k, mode))
            err = max(sub) if sub else float("inf")
        else:
            sa = a.key_series.get(metric, [])
            sb = b.key_series.get(metric, [])
            err = metric_max_error(sa, sb, mode)
        passed = err <= eps + 1e-12
        if not passed:
            ok = False
        details.append({"metric": metric, "mode": mode, "eps": eps, "max_error": err, "pass": passed})
    return ok, details


def run_dir_has_required_artifacts(run_dir: Path) -> bool:
    for name in REQUIRED_RUN_ARTIFACTS:
        p = run_dir / name
        if (not p.exists()) or p.stat().st_size <= 0:
            return False
    return True


def run_meta_matches(
    run_dir: Path,
    seed: int,
    config_hash16: str,
    start_year: int,
    end_year: int,
    use_gpu: bool,
) -> bool:
    meta_path = run_dir / "run_meta.json"
    if not meta_path.exists():
        return False
    try:
        meta = load_json(meta_path)
    except Exception:
        return False
    backend = str(meta.get("backend", "")).strip().lower()
    expected_backend = "gpu" if use_gpu else "cpu"
    return (
        int(meta.get("seed", -1)) == int(seed)
        and str(meta.get("config_hash", "")) == str(config_hash16)
        and int(meta.get("start_year", 0)) == int(start_year)
        and int(meta.get("end_year", 0)) == int(end_year)
        and backend == expected_backend
    )


def copy_run_artifacts(src_dir: Path, dst_dir: Path) -> None:
    dst_dir.mkdir(parents=True, exist_ok=True)
    for name in REQUIRED_RUN_ARTIFACTS:
        shutil.copy2(src_dir / name, dst_dir / name)


def run_cli(
    exe_dir: Path,
    seed: int,
    config_path: Path,
    out_dir: Path,
    start_year: int,
    end_year: int,
    checkpoint_every: int,
    use_gpu: bool,
    runtime_env: Optional[Dict[str, str]] = None,
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    exe_win = win_path(exe_dir)
    cfg_win = win_path(config_path)
    out_win = win_path(out_dir)
    cmd = (
        f"cd /d {exe_win} && "
        f"worldsim_cli.exe --seed {seed} "
        f"--config {cfg_win} "
        f'--startYear {start_year} --endYear {end_year} '
        f'--checkpointEveryYears {checkpoint_every} '
        f"--outDir {out_win} "
        f'--useGPU {1 if use_gpu else 0} >NUL 2>&1'
    )
    cmd_exe = None
    if os.name == "nt":
        cmd_exe = os.environ.get("COMSPEC", "cmd.exe")
    else:
        wsl_cmd = Path("/mnt/c/Windows/System32/cmd.exe")
        cmd_exe = str(wsl_cmd) if wsl_cmd.exists() else "cmd.exe"
    env = os.environ.copy()
    if runtime_env:
        env.update({k: str(v) for k, v in runtime_env.items()})
    p = subprocess.run(
        [cmd_exe, "/c", cmd],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=env,
        check=False,
    )
    if p.returncode != 0:
        raise RuntimeError(f"worldsim_cli failed seed={seed} rc={p.returncode}")


def aggregate_objective(seed_evals: List[SeedEval], defs: Dict[str, Any]) -> Dict[str, Any]:
    t = defs["thresholds"]
    scores = [s.total_score_seed for s in seed_evals]
    score_median = statistics.median(scores) if scores else -1e9
    std = safe_std(scores)
    hardfail_rate = (
        sum(1 for s in seed_evals if s.hardfails) / float(max(1, len(seed_evals)))
    )
    var_pen = float(t["lambdaVar"]) * max(0.0, std - float(t["targetStd"]))
    hardfail_pen = float(t["lambdaFail"]) * hardfail_rate
    objective = score_median - var_pen - hardfail_pen
    return {
        "score_median": score_median,
        "stddev": std,
        "hardfail_rate": hardfail_rate,
        "variance_penalty": var_pen,
        "hardfail_penalty": hardfail_pen,
        "objective": objective,
    }


def top3_violations(seed_evals: List[SeedEval]) -> List[str]:
    agg: Dict[str, float] = {}
    for s in seed_evals:
        for v in s.violations:
            vid = str(v.get("id", ""))
            agg[vid] = agg.get(vid, 0.0) + float(v.get("severity", 0.0))
    return [k for k, _ in sorted(agg.items(), key=lambda kv: kv[1], reverse=True)[:3]]


def eval_map_by_seed(seed_evals: List[SeedEval]) -> Dict[int, SeedEval]:
    return {int(s.seed): s for s in seed_evals}


def paired_delta_stats(
    candidate_by_seed: Dict[int, SeedEval],
    incumbent_by_seed: Dict[int, SeedEval],
    seeds: List[int],
    z_value: float,
) -> Dict[str, Any]:
    diffs: List[float] = []
    for seed in seeds:
        if seed in candidate_by_seed and seed in incumbent_by_seed:
            diffs.append(
                float(candidate_by_seed[seed].total_score_seed)
                - float(incumbent_by_seed[seed].total_score_seed)
            )
    n = len(diffs)
    mean_diff = safe_mean(diffs)
    std_diff = safe_std(diffs)
    stderr = (std_diff / math.sqrt(float(n))) if n > 1 else 0.0
    lcb = mean_diff - float(z_value) * stderr
    ucb = mean_diff + float(z_value) * stderr
    return {
        "n": n,
        "mean_diff": mean_diff,
        "std_diff": std_diff,
        "stderr": stderr,
        "z": float(z_value),
        "lcb": lcb,
        "ucb": ucb,
    }


def normalized_stage_counts(stage_counts: List[int], total_seeds: int) -> List[int]:
    out: List[int] = []
    for n in stage_counts:
        try:
            v = int(n)
        except Exception:
            continue
        if v <= 0:
            continue
        v = max(1, min(total_seeds, v))
        if (not out) or out[-1] != v:
            out.append(v)
    if not out:
        out = [total_seeds]
    if out[-1] != total_seeds:
        out.append(total_seeds)
    return out


def make_candidate(
    lane: str,
    pdef: Dict[str, Any],
    old_val: Any,
    new_val: Any,
    direction: int,
) -> Dict[str, Any]:
    return {
        "lane": lane,
        "group": pdef["group"],
        "path": pdef["path"],
        "pdef": pdef,
        "old_val": old_val,
        "new_val": new_val,
        "direction": direction,
    }


def choose_direction(path: str, top_viol: List[str], old_val: Any, pdef: Dict[str, Any], iteration: int) -> int:
    if "DEPLETION_IGNORED" in top_viol:
        if path in {"tech.diffusionBase", "food.baseForaging", "food.baseFarming"}:
            return +1
        if path == "tech.capabilityThresholdScale":
            return -1
    if "EXTINCTION_PERSISTENT" in top_viol:
        if path in {"food.baseForaging", "food.baseFarming", "food.storageBase"}:
            return +1
        if path in {"disease.endemicBase", "war.overSupplyAttrition"}:
            return -1
    if "TRANSPORT_CHEAT" in top_viol:
        if path in {"economy.tradeIntensityScale", "economy.tradeScarcityCapacityBoost"}:
            return -1
    if "STORAGE_SMOOTHING_CHEAT" in top_viol and path == "food.spoilageBase":
        return +1
    return +1 if (iteration % 2 == 0) else -1


def apply_step(old_val: Any, pdef: Dict[str, Any], direction: int) -> Any:
    step = pdef["recommended_step"]
    lo = pdef["min"]
    hi = pdef["max"]
    typ = pdef["type"]
    if typ == "int":
        nv = int(old_val) + int(direction) * int(step)
        nv = max(int(lo), min(int(hi), nv))
        return int(nv)
    nvf = float(old_val) + float(direction) * float(step)
    nvf = max(float(lo), min(float(hi), nvf))
    return float(nvf)


def init_param_stats(pdefs: List[Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
    stats: Dict[str, Dict[str, Any]] = {}
    for p in pdefs:
        stats[str(p["path"])] = {
            "attempts": 0,
            "accepts": 0,
            "sum_inner_delta": 0.0,
            "sum_long_delta": 0.0,
            "dir_gain": {"+1": 0.0, "-1": 0.0},
            "last_direction": 1,
        }
    return stats


def propose_exploit_candidate(
    best_cfg: Dict[str, Any],
    pdefs: List[Dict[str, Any]],
    param_stats: Dict[str, Dict[str, Any]],
    total_attempts: int,
    top3: List[str],
    iteration: int,
    ucb_c: float,
) -> Dict[str, Any]:
    best_idx = 0
    best_score = -1e18
    for i, pdef in enumerate(pdefs):
        st = param_stats[str(pdef["path"])]
        attempts = int(st["attempts"])
        mean_gain = float(st["sum_inner_delta"]) / float(max(1, attempts))
        bonus = float(ucb_c) * math.sqrt(math.log(float(total_attempts + 2)) / float(attempts + 1))
        score = mean_gain + bonus
        if score > best_score:
            best_score = score
            best_idx = i
    pdef = pdefs[best_idx]
    path = str(pdef["path"])
    old_val = get_param(best_cfg, path)
    direction = choose_direction(path, top3, old_val, pdef, iteration)
    # Bias direction toward historically better sign if data exists.
    st = param_stats[path]
    pos_gain = float(st["dir_gain"].get("+1", 0.0))
    neg_gain = float(st["dir_gain"].get("-1", 0.0))
    if abs(pos_gain - neg_gain) > 1e-9:
        direction = +1 if pos_gain >= neg_gain else -1
    new_val = apply_step(old_val, pdef, direction)
    return make_candidate("exploit", pdef, old_val, new_val, direction)


def propose_explore_candidate(
    best_cfg: Dict[str, Any],
    pdefs: List[Dict[str, Any]],
    top3: List[str],
    iteration: int,
    rng: random.Random,
    avoid_path: Optional[str] = None,
) -> Dict[str, Any]:
    choices = [p for p in pdefs if str(p["path"]) != str(avoid_path)] if avoid_path else list(pdefs)
    if not choices:
        choices = list(pdefs)
    pdef = rng.choice(choices)
    path = str(pdef["path"])
    old_val = get_param(best_cfg, path)
    direction = choose_direction(path, top3, old_val, pdef, iteration + 1)
    # Exploration uses random sign half the time to avoid local lock-in.
    if rng.random() < 0.5:
        direction = -direction
    new_val = apply_step(old_val, pdef, direction)
    return make_candidate("explore", pdef, old_val, new_val, direction)


def run_seed_set(
    seeds: List[int],
    exe_dir: Path,
    config_path: Path,
    out_dir: Path,
    start_year: int,
    end_year: int,
    checkpoint_every: int,
    use_gpu: bool,
    defs: Dict[str, Any],
    jobs: int = 1,
    label: str = "",
    run_cache: Optional[Dict[str, Any]] = None,
    runtime_env: Optional[Dict[str, str]] = None,
    write_eval_artifacts: bool = True,
) -> List[SeedEval]:
    out_dir.mkdir(parents=True, exist_ok=True)
    n_jobs = max(1, min(int(jobs), len(seeds)))
    cfg_hash16 = hash16(config_path)
    cache_enabled = bool((run_cache or {}).get("enabled", False))
    cache_root = Path((run_cache or {}).get("cache_root", out_dir / "_cache")).resolve()
    reuse_existing = bool((run_cache or {}).get("reuse_existing_seed_dirs", True))
    materialize_from_cache = bool((run_cache or {}).get("materialize_from_cache", False))
    if cache_enabled:
        cache_root.mkdir(parents=True, exist_ok=True)

    def run_one(seed: int) -> SeedEval:
        sd = out_dir / f"seed_{seed}"
        run_dir_for_eval = sd

        if reuse_existing and run_dir_has_required_artifacts(sd) and run_meta_matches(sd, seed, cfg_hash16, start_year, end_year, use_gpu):
            run_dir_for_eval = sd
        else:
            used_cache = False
            cache_sd = cache_root / f"{cfg_hash16}_{seed}_{start_year}_{end_year}_{checkpoint_every}_{'gpu' if use_gpu else 'cpu'}"
            if cache_enabled and run_dir_has_required_artifacts(cache_sd) and run_meta_matches(cache_sd, seed, cfg_hash16, start_year, end_year, use_gpu):
                used_cache = True
                if materialize_from_cache:
                    copy_run_artifacts(cache_sd, sd)
                    run_dir_for_eval = sd
                else:
                    run_dir_for_eval = cache_sd

            if not used_cache:
                run_cli(
                    exe_dir,
                    seed,
                    config_path,
                    sd,
                    start_year,
                    end_year,
                    checkpoint_every,
                    use_gpu,
                    runtime_env=runtime_env,
                )
                run_dir_for_eval = sd
                if cache_enabled:
                    try:
                        copy_run_artifacts(sd, cache_sd)
                    except Exception:
                        pass
        return evaluate_seed_run(seed, run_dir_for_eval, defs, write_eval_artifacts=write_eval_artifacts)

    def p(msg: str) -> None:
        if label:
            print(f"[{label}] {msg}", flush=True)

    p(f"starting {len(seeds)} seed(s), jobs={n_jobs}, gpu={use_gpu}, years={start_year}->{end_year}")

    if n_jobs == 1:
        out: List[SeedEval] = []
        for i, seed in enumerate(seeds, start=1):
            p(f"seed {seed} ({i}/{len(seeds)}) start")
            out.append(run_one(seed))
            p(f"seed {seed} ({i}/{len(seeds)}) done")
        return out

    by_seed: Dict[int, SeedEval] = {}
    with ThreadPoolExecutor(max_workers=n_jobs) as pool:
        seed_iter = iter(seeds)
        active: Dict[Any, int] = {}
        for _ in range(n_jobs):
            try:
                s = next(seed_iter)
            except StopIteration:
                break
            active[pool.submit(run_one, s)] = s
        p("workers launched")
        done_n = 0
        while active:
            for fut in as_completed(list(active.keys()), timeout=None):
                seed = active.pop(fut)
                by_seed[seed] = fut.result()
                done_n += 1
                p(f"seed {seed} ({done_n}/{len(seeds)}) done")
                try:
                    s = next(seed_iter)
                    active[pool.submit(run_one, s)] = s
                except StopIteration:
                    pass
                break
    return [by_seed[seed] for seed in seeds]


def load_seed_set_from_existing(
    seeds: List[int],
    seed_root: Path,
    defs: Dict[str, Any],
) -> Optional[List[SeedEval]]:
    out: List[SeedEval] = []
    for seed in seeds:
        sd = seed_root / f"seed_{seed}"
        if not run_dir_has_required_artifacts(sd):
            return None
        out.append(evaluate_seed_run(seed, sd, defs, write_eval_artifacts=False))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Fine tuning loop for realism objective.")
    ap.add_argument("--config", default="data/sim_config.toml")
    ap.add_argument("--schema", default="data/sim_config_schema.json")
    ap.add_argument("--definitions", default="data/realism_definitions.json")
    ap.add_argument("--exe-dir", default="out/cmake/release/bin")
    ap.add_argument("--out-dir", default="out/fine_tuning")
    ap.add_argument("--max-iterations", type=int, default=80)
    ap.add_argument("--seed-jobs", type=int, default=4)
    ap.add_argument("--force-rebaseline", action="store_true")
    ap.add_argument("--stop-flag", default="", help="Optional file path; if created, loop stops gracefully after current iteration.")
    ap.add_argument("--no-write-live-config", action="store_true", help="Do not overwrite --config with best_sim_config.toml at end.")
    args = ap.parse_args()

    root = Path.cwd()
    config_path = (root / args.config).resolve()
    schema_path = (root / args.schema).resolve()
    defs_path = (root / args.definitions).resolve()
    exe_dir = (root / args.exe_dir).resolve()
    out_root = (root / args.out_dir).resolve()
    out_root.mkdir(parents=True, exist_ok=True)
    it_root = out_root / "iterations"
    it_root.mkdir(parents=True, exist_ok=True)
    stop_flag = Path(args.stop_flag).resolve() if args.stop_flag else None

    defs = load_json(defs_path)
    schema = load_json(schema_path)
    cfg0 = load_toml(config_path)
    horizon_policy = schema.get("tuning_year_window", {}) if isinstance(schema.get("tuning_year_window", {}), dict) else {}
    frozen_scenario = schema.get("frozen_scenario", {}) if isinstance(schema.get("frozen_scenario", {}), dict) else {}

    # Enforce frozen baseline scenario settings (spawn/start-tech/world bounds) up front so
    # every baseline/candidate run is comparable.
    frozen_changes = apply_frozen_scenario(cfg0, frozen_scenario)
    if frozen_changes:
        print(f"[startup] applied {len(frozen_changes)} frozen scenario override(s) from schema", flush=True)

    # Tuning window policy: all tuning horizons must live inside [-5000, 2025].
    policy_start = int(horizon_policy.get("start_year", -5000))
    policy_max_end = int(horizon_policy.get("max_end_year", 2025))
    enforce_start_year = bool(horizon_policy.get("enforce_start_year", True))
    allow_shorter_end_year = bool(horizon_policy.get("allow_shorter_end_year", True))
    if policy_max_end < policy_start:
        raise SystemExit(
            f"Invalid tuning_year_window in schema: max_end_year ({policy_max_end}) < start_year ({policy_start})."
        )

    cfg_start = int(cfg0["world"]["startYear"])
    cfg_end = int(cfg0["world"]["endYear"])
    start_year = policy_start if enforce_start_year else max(cfg_start, policy_start)
    end_year = min(cfg_end, policy_max_end)
    if end_year < start_year:
        raise SystemExit(
            f"Tuning window invalid after policy clamp: start={start_year}, end={end_year}. "
            f"Config/world bounds are start={cfg_start}, end={cfg_end}; policy is [{policy_start}, {policy_max_end}]."
        )
    if (not allow_shorter_end_year) and (end_year != policy_max_end):
        raise SystemExit(
            f"Tuning policy requires end_year={policy_max_end}, but effective end_year is {end_year}. "
            f"Adjust config/world.endYear or policy."
        )

    # Ensure candidate configs themselves are pinned to the active tuning window so CLI
    # does not warm-up from earlier eras outside policy bounds.
    set_param(cfg0, "world.startYear", int(start_year))
    set_param(cfg0, "world.endYear", int(policy_max_end))

    checkpoint_every = int(schema.get("checkpoint_every_years", 50))
    accel = schema.get("optimization_accelerators", {}) if isinstance(schema.get("optimization_accelerators", {}), dict) else {}
    crn_cfg = accel.get("common_random_numbers", {}) if isinstance(accel.get("common_random_numbers", {}), dict) else {}
    racing_cfg = accel.get("adaptive_racing", {}) if isinstance(accel.get("adaptive_racing", {}), dict) else {}
    paired_cfg = accel.get("paired_acceptance", {}) if isinstance(accel.get("paired_acceptance", {}), dict) else {}
    cache_cfg = accel.get("run_cache", {}) if isinstance(accel.get("run_cache", {}), dict) else {}
    io_cfg = accel.get("io", {}) if isinstance(accel.get("io", {}), dict) else {}
    search_cfg = accel.get("search", {}) if isinstance(accel.get("search", {}), dict) else {}
    rt_cfg = accel.get("runtime_hygiene", {}) if isinstance(accel.get("runtime_hygiene", {}), dict) else {}

    # (1) Common random numbers: fixed deterministic seed ordering for candidate-vs-incumbent comparisons.
    crn_enabled = bool(crn_cfg.get("enabled", True))
    # (2) Adaptive racing: stage-wise evaluation with early reject.
    racing_enabled = bool(racing_cfg.get("enabled", True))
    early_reject_margin = float(racing_cfg.get("early_reject_margin", 0.75))
    # (5) Paired statistical accept rule (additional to objective gates).
    paired_enabled = bool(paired_cfg.get("enabled", True))
    paired_z = float(paired_cfg.get("confidence_z", 1.96))
    paired_min_inner_lcb_delta = float(paired_cfg.get("min_inner_lcb_delta", 0.0))
    paired_min_long_lcb_delta = float(paired_cfg.get("min_long_lcb_delta", 0.0))
    paired_min_holdout_lcb_delta = float(paired_cfg.get("min_holdout_lcb_delta", -1.0))

    # (10) Runtime hygiene: avoid oversubscription and pin child process math libs to 1 thread.
    seed_jobs = max(1, int(args.seed_jobs))
    cpu_count = os.cpu_count() or 1
    reserve_cpu_cores = max(0, int(rt_cfg.get("reserve_cpu_cores", 1)))
    auto_seed_jobs = bool(rt_cfg.get("auto_seed_jobs_from_cpu", True))
    if auto_seed_jobs:
        seed_jobs = max(1, min(seed_jobs, max(1, cpu_count - reserve_cpu_cores)))
    runtime_env: Dict[str, str] = {}
    if bool(rt_cfg.get("pin_single_thread_env", True)):
        runtime_env.update(
            {
                "OMP_NUM_THREADS": "1",
                "OPENBLAS_NUM_THREADS": "1",
                "MKL_NUM_THREADS": "1",
                "NUMEXPR_NUM_THREADS": "1",
            }
        )

    # (6) Run-cache reuse for deterministic runs.
    run_cache_enabled = bool(cache_cfg.get("enabled", True))
    run_cache = {
        "enabled": run_cache_enabled,
        "cache_root": str((out_root / str(cache_cfg.get("cache_subdir", "run_cache"))).resolve()),
        "reuse_existing_seed_dirs": bool(cache_cfg.get("reuse_existing_seed_dirs", True)),
        "materialize_from_cache": bool(cache_cfg.get("materialize_from_cache", False)),
    }

    # (7) I/O minimization policy.
    write_eval_inner = bool(io_cfg.get("write_eval_artifacts_for_inner", False))
    write_eval_holdout = bool(io_cfg.get("write_eval_artifacts_for_holdout", True))
    prune_rejected = bool(io_cfg.get("prune_rejected_iterations", True))
    keep_candidate_if_rejected = bool(io_cfg.get("keep_candidate_if_rejected", False))

    # (8) Smarter proposals and (9) two-lane search.
    two_lane = bool(search_cfg.get("two_lane_enabled", True))
    ucb_explore_coeff = float(search_cfg.get("ucb_explore_coeff", 0.75))
    search_random_seed = int(search_cfg.get("random_seed", 1337))
    rng = random.Random(search_random_seed)

    curriculum = schema.get("tuning_curriculum", {}) if isinstance(schema.get("tuning_curriculum", {}), dict) else {}
    curriculum_enabled = bool(curriculum.get("enabled", False))
    long_end_year = int(end_year if curriculum.get("long_end_year", None) is None else curriculum.get("long_end_year", end_year))
    inner_end_year = int(curriculum.get("inner_end_year", end_year)) if curriculum_enabled else int(end_year)
    medium_end_year = int(curriculum.get("medium_end_year", long_end_year)) if curriculum_enabled else int(long_end_year)
    long_end_year = max(start_year, min(end_year, long_end_year))
    inner_end_year = max(start_year, min(long_end_year, inner_end_year))
    medium_end_year = max(inner_end_year, min(long_end_year, medium_end_year))
    medium_check_every_iterations = int(curriculum.get("medium_check_every_iterations", 0)) if curriculum_enabled else 0
    medium_check_every_accepted = int(curriculum.get("medium_check_every_accepted", 0)) if curriculum_enabled else 0
    medium_enabled = curriculum_enabled and (medium_end_year > inner_end_year) and ((medium_check_every_iterations > 0) or (medium_check_every_accepted > 0))
    tuning_seeds = [int(x) for x in schema["tuning_seeds"]]
    holdout_seeds = [int(x) for x in schema["holdout_seeds"]]
    if crn_enabled:
        tuning_seeds = sorted(tuning_seeds)
        holdout_seeds = sorted(holdout_seeds)
    stage_counts = normalized_stage_counts(
        [int(x) for x in racing_cfg.get("stage_seed_counts", [2, len(tuning_seeds)])],
        len(tuning_seeds),
    )
    pdefs = [p for p in schema["parameters"] if bool(p.get("safe_to_auto_tune", False))]
    groups = []
    seen_g = set()
    for p in pdefs:
        g = p["group"]
        if g not in seen_g:
            groups.append(g)
            seen_g.add(g)
    by_group: Dict[str, List[Dict[str, Any]]] = {g: [] for g in groups}
    for p in pdefs:
        by_group[p["group"]].append(p)

    best_cfg = copy.deepcopy(cfg0)
    best_cfg_path = out_root / "best_sim_config.toml"
    dump_toml(best_cfg, best_cfg_path)
    best_obj = -1e18
    best_holdout_obj = -1e18
    best_eval: Dict[str, Any] = {}
    best_top3: List[str] = []
    consecutive_gate_fail = 0
    accepted_since_improve = 0
    plateau_same_top3 = 0
    accepted_iters = 0
    stop_reason = ""

    baseline_dir = out_root / "baseline"
    baseline_obj_path = out_root / "baseline_objective.json"
    baseline_gate_path = out_root / "baseline_gates.json"
    print(f"[startup] output_dir={out_root}", flush=True)
    print(f"[startup] tuning_seeds={tuning_seeds} holdout_seeds={holdout_seeds} seed_jobs={seed_jobs}", flush=True)
    print(
        f"[startup] tuning_window policy=[{policy_start}, {policy_max_end}] effective=[{start_year}, {end_year}]",
        flush=True,
    )
    print(
        f"[startup] horizons inner={inner_end_year} medium={medium_end_year if medium_enabled else 'disabled'} long={long_end_year}",
        flush=True,
    )
    print(
        f"[startup] accelerators crn={crn_enabled} racing={racing_enabled} stages={stage_counts} paired={paired_enabled} two_lane={two_lane} cache={run_cache_enabled}",
        flush=True,
    )
    write_json(
        out_root / "tuning_policy.json",
        {
            "tuning_year_window": {
                "policy_start_year": policy_start,
                "policy_max_end_year": policy_max_end,
                "enforce_start_year": enforce_start_year,
                "allow_shorter_end_year": allow_shorter_end_year,
                "effective_start_year": start_year,
                "effective_end_year": end_year,
            },
            "frozen_scenario": {
                "enabled": bool(frozen_scenario.get("enabled", False)),
                "required_paths": frozen_scenario.get("required_paths", {}),
                "applied_overrides": frozen_changes,
            },
            "accelerators": {
                "common_random_numbers": {"enabled": crn_enabled},
                "adaptive_racing": {
                    "enabled": racing_enabled,
                    "stage_seed_counts": stage_counts,
                    "early_reject_margin": early_reject_margin,
                },
                "paired_acceptance": {
                    "enabled": paired_enabled,
                    "confidence_z": paired_z,
                    "min_inner_lcb_delta": paired_min_inner_lcb_delta,
                    "min_long_lcb_delta": paired_min_long_lcb_delta,
                    "min_holdout_lcb_delta": paired_min_holdout_lcb_delta,
                },
                "run_cache": run_cache,
                "io": {
                    "write_eval_artifacts_for_inner": write_eval_inner,
                    "write_eval_artifacts_for_holdout": write_eval_holdout,
                    "prune_rejected_iterations": prune_rejected,
                    "keep_candidate_if_rejected": keep_candidate_if_rejected,
                },
                "search": {
                    "two_lane_enabled": two_lane,
                    "ucb_explore_coeff": ucb_explore_coeff,
                    "random_seed": search_random_seed,
                },
                "runtime_hygiene": {
                    "auto_seed_jobs_from_cpu": auto_seed_jobs,
                    "reserve_cpu_cores": reserve_cpu_cores,
                    "pin_single_thread_env": bool(rt_cfg.get("pin_single_thread_env", True)),
                    "runtime_env": runtime_env,
                },
            },
        },
    )
    base_inner_agg: Dict[str, Any]
    base_inner_holdout_agg: Dict[str, Any]
    base_inner_top3: List[str]
    base_medium_agg: Dict[str, Any]
    base_medium_holdout_agg: Dict[str, Any]
    base_medium_top3: List[str]
    base_agg: Dict[str, Any]
    base_holdout_agg: Dict[str, Any]
    base_top3: List[str]
    baseline_inner_tune: List[SeedEval]
    baseline_long_tune: List[SeedEval]
    baseline_long_holdout: List[SeedEval]

    use_cached = False
    if (not args.force_rebaseline) and baseline_obj_path.exists() and baseline_gate_path.exists():
        bo_probe = load_json(baseline_obj_path)
        hz_probe = bo_probe.get("horizons", {}) if isinstance(bo_probe.get("horizons", {}), dict) else {}
        if curriculum_enabled:
            has_inner = isinstance(hz_probe.get("inner", {}), dict)
            has_long = isinstance(hz_probe.get("long", {}), dict)
            has_medium = (not medium_enabled) or isinstance(hz_probe.get("medium", {}), dict)
            use_cached = has_inner and has_long and has_medium
        else:
            use_cached = bool("tuning" in bo_probe and "holdout" in bo_probe)

    if use_cached:
        print("[baseline] reusing cached baseline_objective.json and baseline_gates.json", flush=True)
        bo = load_json(baseline_obj_path)
        hz = bo.get("horizons", {}) if isinstance(bo.get("horizons", {}), dict) else {}
        if curriculum_enabled:
            base_inner_agg = hz["inner"]["tuning"]
            base_inner_holdout_agg = hz["inner"]["holdout"]
            base_inner_top3 = list(hz["inner"].get("top3", []))
            if medium_enabled:
                base_medium_agg = hz["medium"]["tuning"]
                base_medium_holdout_agg = hz["medium"]["holdout"]
                base_medium_top3 = list(hz["medium"].get("top3", []))
            else:
                base_medium_agg = base_inner_agg
                base_medium_holdout_agg = base_inner_holdout_agg
                base_medium_top3 = base_inner_top3
            base_agg = hz["long"]["tuning"]
            base_holdout_agg = hz["long"]["holdout"]
            base_top3 = list(hz["long"].get("top3", []))
        else:
            base_agg = bo["tuning"]
            base_holdout_agg = bo["holdout"]
            base_top3 = list(bo.get("top3", []))
            base_inner_agg = base_agg
            base_inner_holdout_agg = base_holdout_agg
            base_inner_top3 = base_top3
            base_medium_agg = base_agg
            base_medium_holdout_agg = base_holdout_agg
            base_medium_top3 = base_top3
        bg = load_json(baseline_gate_path)
        canary_ok = bool(bg.get("canary_pass", False))
        parity_ok = bool(bg.get("parity_pass", False))
        canary_det = list(bg.get("canary", []))
        parity_det = list(bg.get("parity", []))
        baseline_inner_tune_loaded = load_seed_set_from_existing(tuning_seeds, baseline_dir / "inner" / "tuning", defs)
        baseline_long_tune_loaded = load_seed_set_from_existing(tuning_seeds, baseline_dir / "long" / "tuning", defs)
        baseline_long_holdout_loaded = load_seed_set_from_existing(holdout_seeds, baseline_dir / "long" / "holdout", defs)
        if (
            baseline_inner_tune_loaded is None
            or baseline_long_tune_loaded is None
            or baseline_long_holdout_loaded is None
        ):
            print("[baseline] cached objective exists but seed artifacts are incomplete; recomputing baseline", flush=True)
            use_cached = False
        else:
            baseline_inner_tune = baseline_inner_tune_loaded
            baseline_long_tune = baseline_long_tune_loaded
            baseline_long_holdout = baseline_long_holdout_loaded
    if not use_cached:
        print(f"[baseline] running baseline inner horizon (end={inner_end_year})", flush=True)
        baseline_inner_tune = run_seed_set(
            tuning_seeds,
            exe_dir,
            config_path,
            baseline_dir / "inner" / "tuning",
            start_year,
            inner_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
            label="baseline:inner:tuning",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )
        base_inner_agg = aggregate_objective(baseline_inner_tune, defs)
        base_inner_top3 = top3_violations(baseline_inner_tune)
        baseline_inner_holdout = run_seed_set(
            holdout_seeds,
            exe_dir,
            config_path,
            baseline_dir / "inner" / "holdout",
            start_year,
            inner_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
            label="baseline:inner:holdout",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )
        base_inner_holdout_agg = aggregate_objective(baseline_inner_holdout, defs)
        print(
            f"[baseline] inner objective={float(base_inner_agg['objective']):.6f} holdout={float(base_inner_holdout_agg['objective']):.6f} top3={base_inner_top3}",
            flush=True,
        )

        if medium_enabled:
            print(f"[baseline] running baseline medium horizon (end={medium_end_year})", flush=True)
            baseline_medium_tune = run_seed_set(
                tuning_seeds,
                exe_dir,
                config_path,
                baseline_dir / "medium" / "tuning",
                start_year,
                medium_end_year,
                checkpoint_every,
                bool(cfg0["economy"]["useGPU"]),
                defs,
                jobs=seed_jobs,
                label="baseline:medium:tuning",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=True,
            )
            base_medium_agg = aggregate_objective(baseline_medium_tune, defs)
            base_medium_top3 = top3_violations(baseline_medium_tune)
            baseline_medium_holdout = run_seed_set(
                holdout_seeds,
                exe_dir,
                config_path,
                baseline_dir / "medium" / "holdout",
                start_year,
                medium_end_year,
                checkpoint_every,
                bool(cfg0["economy"]["useGPU"]),
                defs,
                jobs=seed_jobs,
                label="baseline:medium:holdout",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=True,
            )
            base_medium_holdout_agg = aggregate_objective(baseline_medium_holdout, defs)
            print(
                f"[baseline] medium objective={float(base_medium_agg['objective']):.6f} holdout={float(base_medium_holdout_agg['objective']):.6f} top3={base_medium_top3}",
                flush=True,
            )
        else:
            base_medium_agg = base_inner_agg
            base_medium_holdout_agg = base_inner_holdout_agg
            base_medium_top3 = base_inner_top3

        print(f"[baseline] running baseline long horizon (end={long_end_year})", flush=True)
        baseline_long_tune = run_seed_set(
            tuning_seeds,
            exe_dir,
            config_path,
            baseline_dir / "long" / "tuning",
            start_year,
            long_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
            label="baseline:long:tuning",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )
        base_agg = aggregate_objective(baseline_long_tune, defs)
        base_top3 = top3_violations(baseline_long_tune)
        baseline_long_holdout = run_seed_set(
            holdout_seeds,
            exe_dir,
            config_path,
            baseline_dir / "long" / "holdout",
            start_year,
            long_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
            label="baseline:long:holdout",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )
        base_holdout_agg = aggregate_objective(baseline_long_holdout, defs)
        print(
            f"[baseline] long objective={float(base_agg['objective']):.6f} holdout={float(base_holdout_agg['objective']):.6f} top3={base_top3}",
            flush=True,
        )

        write_json(
            out_root / "baseline_objective.json",
            {
                "horizons": {
                    "inner": {"end_year": inner_end_year, "tuning": base_inner_agg, "holdout": base_inner_holdout_agg, "top3": base_inner_top3},
                    "medium": {"end_year": medium_end_year, "tuning": base_medium_agg, "holdout": base_medium_holdout_agg, "top3": base_medium_top3},
                    "long": {"end_year": long_end_year, "tuning": base_agg, "holdout": base_holdout_agg, "top3": base_top3},
                },
                # Legacy keys remain mapped to long-horizon objective for compatibility.
                "tuning": base_agg,
                "holdout": base_holdout_agg,
                "top3": base_top3,
            },
        )

        # Canary/parity baseline uses inner horizon for fast deterministic safety checks.
        print("[baseline] running canary checks (inner horizon)", flush=True)
        canary_dir = baseline_dir / "canary"
        canary_a = run_seed_set(
            [tuning_seeds[0]],
            exe_dir,
            config_path,
            canary_dir / "a",
            start_year,
            inner_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            label="baseline:canary:a",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )[0]
        canary_b = run_seed_set(
            [tuning_seeds[0]],
            exe_dir,
            config_path,
            canary_dir / "b",
            start_year,
            inner_end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            label="baseline:canary:b",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )[0]
        canary_ok, canary_det = compare_metric_series(canary_a, canary_b, defs["canary_eps"])
        print(f"[baseline] canary_pass={canary_ok}", flush=True)
        print("[baseline] running parity checks (inner horizon)", flush=True)
        parity_dir = baseline_dir / "parity"
        parity_gpu = run_seed_set(
            [tuning_seeds[0]],
            exe_dir,
            config_path,
            parity_dir / "gpu",
            start_year,
            inner_end_year,
            checkpoint_every,
            True,
            defs,
            label="baseline:parity:gpu",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )[0]
        parity_cpu = run_seed_set(
            [tuning_seeds[0]],
            exe_dir,
            config_path,
            parity_dir / "cpu",
            start_year,
            inner_end_year,
            checkpoint_every,
            False,
            defs,
            label="baseline:parity:cpu",
            run_cache=run_cache,
            runtime_env=runtime_env,
            write_eval_artifacts=True,
        )[0]
        parity_ok, parity_det = compare_metric_series(parity_gpu, parity_cpu, defs["parity_eps"])
        print(f"[baseline] parity_pass={parity_ok}", flush=True)
        write_json(
            out_root / "baseline_gates.json",
            {
                "horizon_end_year": inner_end_year,
                "canary_pass": canary_ok,
                "canary": canary_det,
                "parity_pass": parity_ok,
                "parity": parity_det,
            },
        )

    best_inner_obj = float(base_inner_agg["objective"])
    best_inner_holdout_obj = float(base_inner_holdout_agg["objective"])
    best_medium_obj = float(base_medium_agg["objective"])
    best_medium_holdout_obj = float(base_medium_holdout_agg["objective"])
    best_obj = float(base_agg["objective"])
    best_holdout_obj = float(base_holdout_agg["objective"])
    best_eval = {
        "tuning": base_agg,
        "holdout": base_holdout_agg,
        "horizons": {
            "inner": {"tuning": base_inner_agg, "holdout": base_inner_holdout_agg, "top3": base_inner_top3, "end_year": inner_end_year},
            "medium": {"tuning": base_medium_agg, "holdout": base_medium_holdout_agg, "top3": base_medium_top3, "end_year": medium_end_year},
            "long": {"tuning": base_agg, "holdout": base_holdout_agg, "top3": base_top3, "end_year": long_end_year},
        },
    }
    best_top3 = base_top3
    print(
        f"[baseline] complete inner_obj={best_inner_obj:.6f} long_obj={best_obj:.6f} long_holdout={best_holdout_obj:.6f} top3={best_top3}",
        flush=True,
    )

    # Iterative loop.
    best_inner_seed_evals = list(baseline_inner_tune)
    best_long_seed_evals = list(baseline_long_tune)
    best_holdout_seed_evals = list(baseline_long_holdout)
    best_inner_by_seed = eval_map_by_seed(best_inner_seed_evals)
    best_long_by_seed = eval_map_by_seed(best_long_seed_evals)
    best_holdout_by_seed = eval_map_by_seed(best_holdout_seed_evals)
    param_stats = init_param_stats(pdefs)
    total_param_attempts = 0
    prev_top3_inner = list(base_inner_top3)
    proposal_top3 = list(base_top3) if base_top3 else list(base_inner_top3)
    min_delta = float(defs["thresholds"]["min_delta"])
    holdout_delta_req = float(defs["thresholds"]["holdout_objective_min_delta"])

    for it in range(1, args.max_iterations + 1):
        if stop_flag is not None and stop_flag.exists():
            stop_reason = "MANUAL_STOP"
            break
        it_dir = it_root / f"iter_{it:03d}"
        it_dir.mkdir(parents=True, exist_ok=True)
        top3_before = list(proposal_top3)

        lane_candidates: List[Dict[str, Any]] = []
        cand_exploit = propose_exploit_candidate(
            best_cfg,
            pdefs,
            param_stats,
            total_param_attempts,
            proposal_top3,
            it,
            ucb_explore_coeff,
        )
        lane_candidates.append(cand_exploit)
        if two_lane and len(pdefs) > 1:
            cand_explore = propose_explore_candidate(
                best_cfg,
                pdefs,
                proposal_top3,
                it,
                rng,
                avoid_path=str(cand_exploit["path"]),
            )
            lane_candidates.append(cand_explore)

        scout_n = min(stage_counts[0], len(tuning_seeds))
        scout_seeds = tuning_seeds[:scout_n]
        lane_scout_rows: List[Dict[str, Any]] = []
        selected_lane = lane_candidates[0]
        best_scout_delta = -1e18
        selected_cfg: Dict[str, Any] = {}
        selected_cfg_path = it_dir / "candidate_sim_config.toml"
        selected_scout_by_seed: Dict[int, SeedEval] = {}
        selected_scout_evals: List[SeedEval] = []

        for lane in lane_candidates:
            lane_name = str(lane["lane"])
            path = str(lane["path"])
            lane_cfg = copy.deepcopy(best_cfg)
            set_param(lane_cfg, path, lane["new_val"])
            lane_cfg_path = it_dir / f"candidate_{lane_name}.toml"
            dump_toml(lane_cfg, lane_cfg_path)
            lane_scout = run_seed_set(
                scout_seeds,
                exe_dir,
                lane_cfg_path,
                it_dir / "lanes" / lane_name / "scout",
                start_year,
                inner_end_year,
                checkpoint_every,
                bool(lane_cfg["economy"]["useGPU"]),
                defs,
                jobs=seed_jobs,
                label=f"iter {it:03d}:{lane_name}:scout",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=write_eval_inner,
            )
            lane_scout_agg = aggregate_objective(lane_scout, defs)
            inc_scout = [best_inner_by_seed[s] for s in scout_seeds if s in best_inner_by_seed]
            inc_scout_agg = aggregate_objective(inc_scout, defs) if inc_scout else {"objective": best_inner_obj}
            scout_delta = float(lane_scout_agg["objective"]) - float(inc_scout_agg["objective"])
            scout_pair = (
                paired_delta_stats(eval_map_by_seed(lane_scout), best_inner_by_seed, scout_seeds, paired_z)
                if paired_enabled
                else {"n": 0, "lcb": 0.0, "ucb": 0.0}
            )
            lane_scout_rows.append(
                {
                    "lane": lane_name,
                    "group": lane["group"],
                    "path": path,
                    "old": lane["old_val"],
                    "new": lane["new_val"],
                    "scout_objective": lane_scout_agg["objective"],
                    "scout_delta_vs_incumbent": scout_delta,
                    "scout_paired": scout_pair,
                }
            )
            print(
                f"[iter {it:03d}] lane={lane_name} scout group={lane['group']} param={path} old={lane['old_val']} new={lane['new_val']} delta={scout_delta:.6f}",
                flush=True,
            )
            if scout_delta > best_scout_delta:
                best_scout_delta = scout_delta
                selected_lane = lane
                selected_cfg = lane_cfg
                selected_cfg_path = lane_cfg_path
                selected_scout_evals = lane_scout
                selected_scout_by_seed = eval_map_by_seed(lane_scout)

        if selected_cfg:
            dump_toml(selected_cfg, selected_cfg_path)

        group = str(selected_lane["group"])
        path = str(selected_lane["path"])
        pdef = selected_lane["pdef"]
        old_val = selected_lane["old_val"]
        new_val = selected_lane["new_val"]
        direction = int(selected_lane["direction"])
        print(
            f"[iter {it:03d}] selected lane={selected_lane['lane']} group={group} param={path} old={old_val} new={new_val}",
            flush=True,
        )

        cand_inner_by_seed = dict(selected_scout_by_seed)
        stage_records: List[Dict[str, Any]] = []
        early_reject = False
        early_reject_reason = ""
        for stage_n in stage_counts:
            stage_seed_subset = tuning_seeds[:stage_n]
            need = [s for s in stage_seed_subset if s not in cand_inner_by_seed]
            if need:
                stage_eval = run_seed_set(
                    need,
                    exe_dir,
                    selected_cfg_path,
                    it_dir / "inner" / f"tuning_stage_{stage_n}",
                    start_year,
                    inner_end_year,
                    checkpoint_every,
                    bool(selected_cfg["economy"]["useGPU"]),
                    defs,
                    jobs=seed_jobs,
                    label=f"iter {it:03d}:inner:stage{stage_n}",
                    run_cache=run_cache,
                    runtime_env=runtime_env,
                    write_eval_artifacts=write_eval_inner,
                )
                for s in stage_eval:
                    cand_inner_by_seed[int(s.seed)] = s

            cand_stage = [cand_inner_by_seed[s] for s in stage_seed_subset if s in cand_inner_by_seed]
            inc_stage = [best_inner_by_seed[s] for s in stage_seed_subset if s in best_inner_by_seed]
            cand_stage_agg = aggregate_objective(cand_stage, defs)
            inc_stage_agg = aggregate_objective(inc_stage, defs) if inc_stage else {"objective": best_inner_obj}
            stage_delta = float(cand_stage_agg["objective"]) - float(inc_stage_agg["objective"])
            stage_pair = (
                paired_delta_stats(cand_inner_by_seed, best_inner_by_seed, stage_seed_subset, paired_z)
                if paired_enabled
                else {"n": 0, "lcb": 0.0, "ucb": 0.0}
            )
            stage_records.append(
                {
                    "stage_seed_count": stage_n,
                    "candidate_objective": cand_stage_agg["objective"],
                    "incumbent_objective": inc_stage_agg["objective"],
                    "objective_delta": stage_delta,
                    "paired": stage_pair,
                }
            )
            if racing_enabled and stage_n < len(tuning_seeds):
                reject_obj = stage_delta < (min_delta - early_reject_margin)
                reject_pair = paired_enabled and (stage_pair.get("n", 0) >= 2) and (float(stage_pair.get("lcb", 0.0)) < (paired_min_inner_lcb_delta - early_reject_margin))
                if reject_obj or reject_pair:
                    early_reject = True
                    early_reject_reason = "OBJECTIVE_MARGIN" if reject_obj else "PAIRED_LCB_MARGIN"
                    print(
                        f"[iter {it:03d}] early reject at stage={stage_n} reason={early_reject_reason}",
                        flush=True,
                    )
                    break

        evaluated_inner_seeds = [s for s in tuning_seeds if s in cand_inner_by_seed]
        cand_inner = [cand_inner_by_seed[s] for s in evaluated_inner_seeds]
        cand_inner_agg = aggregate_objective(cand_inner, defs)
        cand_inner_top3 = top3_violations(cand_inner)
        tune_hardfails = sorted({hf for s in cand_inner for hf in s.hardfails})
        inner_incumbent = [best_inner_by_seed[s] for s in evaluated_inner_seeds if s in best_inner_by_seed]
        inner_incumbent_agg = aggregate_objective(inner_incumbent, defs) if inner_incumbent else {"objective": best_inner_obj}
        inner_delta = float(cand_inner_agg["objective"]) - float(inner_incumbent_agg["objective"])
        inner_pair = (
            paired_delta_stats(cand_inner_by_seed, best_inner_by_seed, evaluated_inner_seeds, paired_z)
            if paired_enabled
            else {"n": 0, "lcb": 0.0, "ucb": 0.0}
        )
        objective_delta = inner_delta
        cand_agg = cand_inner_agg
        cand_top3 = cand_inner_top3

        canary_pass = False
        parity_pass = False
        canary_detail: List[Dict[str, Any]] = []
        parity_detail: List[Dict[str, Any]] = []
        no_hardfail_tuning = len(tune_hardfails) == 0
        improve_ok = (inner_delta >= min_delta) and ((not paired_enabled) or (float(inner_pair.get("lcb", -1e18)) >= paired_min_inner_lcb_delta))
        checks_executed = no_hardfail_tuning and improve_ok and (not early_reject)
        if checks_executed:
            print(f"[iter {it:03d}] running canary/parity checks", flush=True)
            cand_canary_a = run_seed_set(
                [tuning_seeds[0]],
                exe_dir,
                selected_cfg_path,
                it_dir / "canary" / "a",
                start_year,
                inner_end_year,
                checkpoint_every,
                bool(selected_cfg["economy"]["useGPU"]),
                defs,
                label=f"iter {it:03d}:canary:a",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=write_eval_inner,
            )[0]
            cand_canary_b = run_seed_set(
                [tuning_seeds[0]],
                exe_dir,
                selected_cfg_path,
                it_dir / "canary" / "b",
                start_year,
                inner_end_year,
                checkpoint_every,
                bool(selected_cfg["economy"]["useGPU"]),
                defs,
                label=f"iter {it:03d}:canary:b",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=write_eval_inner,
            )[0]
            canary_pass, canary_detail = compare_metric_series(cand_canary_a, cand_canary_b, defs["canary_eps"])
            cand_parity_gpu = run_seed_set(
                [tuning_seeds[0]],
                exe_dir,
                selected_cfg_path,
                it_dir / "parity" / "gpu",
                start_year,
                inner_end_year,
                checkpoint_every,
                True,
                defs,
                label=f"iter {it:03d}:parity:gpu",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=write_eval_inner,
            )[0]
            cand_parity_cpu = run_seed_set(
                [tuning_seeds[0]],
                exe_dir,
                selected_cfg_path,
                it_dir / "parity" / "cpu",
                start_year,
                inner_end_year,
                checkpoint_every,
                False,
                defs,
                label=f"iter {it:03d}:parity:cpu",
                run_cache=run_cache,
                runtime_env=runtime_env,
                write_eval_artifacts=write_eval_inner,
            )[0]
            parity_pass, parity_detail = compare_metric_series(cand_parity_gpu, cand_parity_cpu, defs["parity_eps"])
            print(f"[iter {it:03d}] canary_pass={canary_pass} parity_pass={parity_pass}", flush=True)
        else:
            print(
                f"[iter {it:03d}] skipping canary/parity (hardfails={len(tune_hardfails)} improve={improve_ok} early_reject={early_reject})",
                flush=True,
            )

        medium_required = medium_enabled and (
            (medium_check_every_iterations > 0 and (it % medium_check_every_iterations == 0))
            or (medium_check_every_accepted > 0 and ((accepted_iters + 1) % medium_check_every_accepted == 0))
        )
        medium_pass = True
        medium_ran = False
        medium_hardfails: List[str] = []
        medium_holdout_hardfails: List[str] = []
        medium_agg: Dict[str, Any] = {}
        medium_holdout_agg: Dict[str, Any] = {}
        medium_delta = 0.0

        holdout_ok = False
        holdout_agg: Dict[str, Any] = {}
        holdout_hardfails: List[str] = []
        cand_hold: List[SeedEval] = []
        holdout_pair: Dict[str, Any] = {"n": 0, "lcb": 0.0, "ucb": 0.0}
        long_pair: Dict[str, Any] = {"n": 0, "lcb": 0.0, "ucb": 0.0}
        long_ran = False
        long_hardfails: List[str] = []
        cand_long: List[SeedEval] = []

        if no_hardfail_tuning and improve_ok and canary_pass and parity_pass and (not early_reject):
            if medium_required:
                medium_ran = True
                print(f"[iter {it:03d}] running medium horizon checks (end={medium_end_year})", flush=True)
                cand_medium = run_seed_set(
                    tuning_seeds,
                    exe_dir,
                    selected_cfg_path,
                    it_dir / "medium" / "tuning",
                    start_year,
                    medium_end_year,
                    checkpoint_every,
                    bool(selected_cfg["economy"]["useGPU"]),
                    defs,
                    jobs=seed_jobs,
                    label=f"iter {it:03d}:medium:tuning",
                    run_cache=run_cache,
                    runtime_env=runtime_env,
                    write_eval_artifacts=write_eval_inner,
                )
                medium_agg = aggregate_objective(cand_medium, defs)
                medium_hardfails = sorted({hf for s in cand_medium for hf in s.hardfails})
                medium_delta = float(medium_agg["objective"]) - float(best_medium_obj)
                cand_medium_hold = run_seed_set(
                    holdout_seeds,
                    exe_dir,
                    selected_cfg_path,
                    it_dir / "medium" / "holdout",
                    start_year,
                    medium_end_year,
                    checkpoint_every,
                    bool(selected_cfg["economy"]["useGPU"]),
                    defs,
                    jobs=seed_jobs,
                    label=f"iter {it:03d}:medium:holdout",
                    run_cache=run_cache,
                    runtime_env=runtime_env,
                    write_eval_artifacts=write_eval_holdout,
                )
                medium_holdout_agg = aggregate_objective(cand_medium_hold, defs)
                medium_holdout_hardfails = sorted({hf for s in cand_medium_hold for hf in s.hardfails})
                medium_pass = (
                    len(medium_hardfails) == 0
                    and len(medium_holdout_hardfails) <= int(defs["thresholds"]["holdout_hardfail_max"])
                    and float(medium_holdout_agg["objective"]) >= float(best_medium_holdout_obj) + holdout_delta_req
                )
                print(
                    f"[iter {it:03d}] medium_obj={float(medium_agg['objective']):.6f} medium_delta={medium_delta:.6f} medium_holdout={float(medium_holdout_agg['objective']):.6f} medium_pass={medium_pass}",
                    flush=True,
                )
            if medium_pass:
                long_ran = True
                print(f"[iter {it:03d}] running long horizon promotion check (end={long_end_year})", flush=True)
                cand_long = run_seed_set(
                    tuning_seeds,
                    exe_dir,
                    selected_cfg_path,
                    it_dir / "long" / "tuning",
                    start_year,
                    long_end_year,
                    checkpoint_every,
                    bool(selected_cfg["economy"]["useGPU"]),
                    defs,
                    jobs=seed_jobs,
                    label=f"iter {it:03d}:long:tuning",
                    run_cache=run_cache,
                    runtime_env=runtime_env,
                    write_eval_artifacts=write_eval_holdout,
                )
                cand_agg = aggregate_objective(cand_long, defs)
                cand_top3 = top3_violations(cand_long)
                long_hardfails = sorted({hf for s in cand_long for hf in s.hardfails})
                objective_delta = float(cand_agg["objective"]) - float(best_obj)
                long_pair = (
                    paired_delta_stats(eval_map_by_seed(cand_long), best_long_by_seed, tuning_seeds, paired_z)
                    if paired_enabled
                    else {"n": 0, "lcb": 0.0, "ucb": 0.0}
                )
                long_improve_ok = (objective_delta >= min_delta) and ((not paired_enabled) or (float(long_pair.get("lcb", -1e18)) >= paired_min_long_lcb_delta))
                if len(long_hardfails) == 0 and long_improve_ok:
                    print(f"[iter {it:03d}] running long holdout validation", flush=True)
                    cand_hold = run_seed_set(
                        holdout_seeds,
                        exe_dir,
                        selected_cfg_path,
                        it_dir / "long" / "holdout",
                        start_year,
                        long_end_year,
                        checkpoint_every,
                        bool(selected_cfg["economy"]["useGPU"]),
                        defs,
                        jobs=seed_jobs,
                        label=f"iter {it:03d}:long:holdout",
                        run_cache=run_cache,
                        runtime_env=runtime_env,
                        write_eval_artifacts=write_eval_holdout,
                    )
                    holdout_agg = aggregate_objective(cand_hold, defs)
                    holdout_hardfails = sorted({hf for s in cand_hold for hf in s.hardfails})
                    holdout_pair = (
                        paired_delta_stats(eval_map_by_seed(cand_hold), best_holdout_by_seed, holdout_seeds, paired_z)
                        if paired_enabled
                        else {"n": 0, "lcb": 0.0, "ucb": 0.0}
                    )
                    holdout_ok = (
                        len(holdout_hardfails) <= int(defs["thresholds"]["holdout_hardfail_max"])
                        and float(holdout_agg["objective"]) >= float(best_holdout_obj) + holdout_delta_req
                        and ((not paired_enabled) or (float(holdout_pair.get("lcb", -1e18)) >= paired_min_holdout_lcb_delta))
                    )
                    print(
                        f"[iter {it:03d}] long_obj={float(cand_agg['objective']):.6f} long_delta={objective_delta:.6f} holdout_obj={float(holdout_agg['objective']):.6f} holdout_ok={holdout_ok}",
                        flush=True,
                    )
                else:
                    print(
                        f"[iter {it:03d}] skipping long holdout (long_hardfails={len(long_hardfails)} long_delta={objective_delta:.6f})",
                        flush=True,
                    )
            else:
                print(f"[iter {it:03d}] medium gate failed, skipping long promotion check", flush=True)
        else:
            print(
                f"[iter {it:03d}] promotion checks skipped (no_hardfail={no_hardfail_tuning} improve_ok={improve_ok} canary={canary_pass} parity={parity_pass} early_reject={early_reject})",
                flush=True,
            )

        accepted = (
            no_hardfail_tuning
            and improve_ok
            and canary_pass
            and parity_pass
            and medium_pass
            and long_ran
            and holdout_ok
        )

        st = param_stats[path]
        st["attempts"] = int(st["attempts"]) + 1
        st["sum_inner_delta"] = float(st["sum_inner_delta"]) + float(inner_delta)
        st["sum_long_delta"] = float(st["sum_long_delta"]) + float(objective_delta if long_ran else 0.0)
        st["last_direction"] = direction
        dir_key = "+1" if direction > 0 else "-1"
        st["dir_gain"][dir_key] = float(st["dir_gain"].get(dir_key, 0.0)) + float(inner_delta)
        if accepted:
            st["accepts"] = int(st["accepts"]) + 1
        total_param_attempts += 1

        if accepted:
            best_cfg = selected_cfg
            best_inner_obj = float(cand_inner_agg["objective"])
            best_inner_seed_evals = [cand_inner_by_seed[s] for s in tuning_seeds if s in cand_inner_by_seed]
            best_inner_by_seed = eval_map_by_seed(best_inner_seed_evals)
            if medium_ran:
                best_medium_obj = float(medium_agg["objective"])
                best_medium_holdout_obj = float(medium_holdout_agg["objective"])
            best_obj = float(cand_agg["objective"])
            best_holdout_obj = float(holdout_agg["objective"])
            best_long_seed_evals = list(cand_long)
            best_long_by_seed = eval_map_by_seed(best_long_seed_evals)
            best_holdout_seed_evals = list(cand_hold)
            best_holdout_by_seed = eval_map_by_seed(best_holdout_seed_evals)
            best_eval = {
                "tuning": cand_agg,
                "holdout": holdout_agg,
                "horizons": {
                    "inner": {"end_year": inner_end_year, "tuning": cand_inner_agg},
                    "medium": {"end_year": medium_end_year, "tuning": medium_agg if medium_ran else None, "holdout": medium_holdout_agg if medium_ran else None},
                    "long": {"end_year": long_end_year, "tuning": cand_agg, "holdout": holdout_agg, "top3": cand_top3},
                },
            }
            best_top3 = cand_top3
            dump_toml(best_cfg, best_cfg_path)
            accepted_iters += 1
            accepted_since_improve = 0
        else:
            accepted_since_improve += 1

        gate_failed = False
        if "MISSING_METRIC" in tune_hardfails:
            gate_failed = True
        if checks_executed and (not canary_pass):
            gate_failed = True
        if checks_executed and (not parity_pass):
            gate_failed = True
        consecutive_gate_fail = (consecutive_gate_fail + 1) if gate_failed else 0

        if cand_inner_top3 == prev_top3_inner and inner_delta < min_delta:
            plateau_same_top3 += 1
        else:
            plateau_same_top3 = 0
        prev_top3_inner = list(cand_inner_top3)
        if long_ran and cand_top3:
            proposal_top3 = list(cand_top3)
        elif cand_inner_top3:
            proposal_top3 = list(cand_inner_top3)

        iter_json = {
            "iteration": it,
            "subsystem_group": group,
            "selected_lane": selected_lane["lane"],
            "lane_scout": lane_scout_rows,
            "parameter_edits": [
                {
                    "path": path,
                    "old": old_val,
                    "new": new_val,
                    "recommended_step": pdef["recommended_step"],
                    "direction": direction,
                }
            ],
            "top_violations_before": top3_before,
            "top_violations_after": cand_inner_top3,
            "top_violations_after_long": cand_top3 if long_ran else None,
            "objective_tuning": cand_agg["objective"],
            "objective_tuning_inner": cand_inner_agg["objective"],
            "objective_tuning_medium": medium_agg.get("objective", None) if medium_ran else None,
            "objective_tuning_long": cand_agg["objective"] if long_ran else None,
            "score_delta": objective_delta,
            "score_delta_inner": inner_delta,
            "score_delta_medium": medium_delta if medium_ran else None,
            "racing": {
                "enabled": racing_enabled,
                "stages": stage_records,
                "early_reject": early_reject,
                "early_reject_reason": early_reject_reason,
            },
            "paired_stats": {
                "inner": inner_pair,
                "long": long_pair,
                "holdout": holdout_pair,
            },
            "gates": {
                "metric_availability": "MISSING_METRIC" not in tune_hardfails,
                "canary_pass": canary_pass,
                "backend_parity_pass": parity_pass,
                "medium_required": medium_required,
                "medium_pass": medium_pass,
                "long_ran": long_ran,
                "holdout_pass": holdout_ok,
                "tuning_hardfails": tune_hardfails,
                "medium_hardfails": medium_hardfails,
                "medium_holdout_hardfails": medium_holdout_hardfails,
                "long_hardfails": long_hardfails,
                "holdout_hardfails": holdout_hardfails,
            },
            "holdout_objective": holdout_agg.get("objective", None),
            "accepted": accepted,
            "best_so_far": {
                "objective": best_obj,
                "holdout_objective": best_holdout_obj,
                "config_path": str(best_cfg_path),
                "config_hash16": hash16(best_cfg_path),
            },
            "canary_detail": canary_detail,
            "parity_detail": parity_detail,
            "curriculum": {
                "enabled": curriculum_enabled,
                "inner_end_year": inner_end_year,
                "medium_end_year": medium_end_year if medium_enabled else None,
                "long_end_year": long_end_year,
            },
        }
        write_json(it_dir / "iteration.json", iter_json)
        print(
            f"[iter {it:03d}] group={group} param={path} old={old_val} new={new_val} inner_obj={cand_inner_agg['objective']:.6f} inner_delta={inner_delta:.6f} promoted_obj={cand_agg['objective']:.6f} promoted_delta={objective_delta:.6f} accepted={accepted}",
            flush=True,
        )

        if (not accepted) and prune_rejected:
            for child in ("inner", "medium", "long", "canary", "parity", "lanes"):
                cdir = it_dir / child
                if cdir.exists():
                    shutil.rmtree(cdir, ignore_errors=True)
            if (not keep_candidate_if_rejected) and selected_cfg_path.exists():
                try:
                    selected_cfg_path.unlink()
                except Exception:
                    pass

        if stop_flag is not None and stop_flag.exists():
            stop_reason = "MANUAL_STOP"
            break

        if (
            accepted_iters >= int(schema.get("convergence_iterations", 50))
            and accepted_since_improve >= int(schema.get("convergence_iterations", 50))
            and best_top3 == cand_top3
        ):
            stop_reason = "CONVERGENCE"
            break

        major_left_src = cand_long if long_ran else cand_inner
        major_left = any(float(v.get("severity", 0.0)) >= float(schema.get("major_violation_threshold", 50.0)) for s in major_left_src for v in s.violations)
        major_left = major_left or any(float(v.get("severity", 0.0)) >= float(schema.get("major_violation_threshold", 50.0)) for s in (cand_hold if holdout_ok else []) for v in s.violations)
        if best_obj >= float(schema.get("target_objective", 90.0)) and (not major_left):
            stop_reason = "TARGET_REALISM"
            break

        if plateau_same_top3 >= int(schema.get("plateau_iterations_structural", 8)):
            stop_reason = "STRUCTURAL_CHANGE_SIGNAL"
            break

        if consecutive_gate_fail >= 5:
            stop_reason = "SAFETY"
            break

    # Write final outputs.
    final = {
        "stop_condition": stop_reason if stop_reason else "MAX_ITERATIONS",
        "best_objective": best_obj,
        "best_objective_inner": best_inner_obj,
        "best_objective_medium": best_medium_obj,
        "best_holdout_objective": best_holdout_obj,
        "best_holdout_objective_inner": best_inner_holdout_obj,
        "best_holdout_objective_medium": best_medium_holdout_obj,
        "best_top3_violations": best_top3,
        "best_config": str(best_cfg_path),
        "best_config_hash16": hash16(best_cfg_path),
        "best_eval": best_eval,
        "curriculum": {
            "enabled": curriculum_enabled,
            "inner_end_year": inner_end_year,
            "medium_end_year": medium_end_year if medium_enabled else None,
            "long_end_year": long_end_year,
            "medium_check_every_iterations": medium_check_every_iterations,
            "medium_check_every_accepted": medium_check_every_accepted,
        },
        "optimization_accelerators": {
            "common_random_numbers": {"enabled": crn_enabled},
            "adaptive_racing": {"enabled": racing_enabled, "stage_seed_counts": stage_counts, "early_reject_margin": early_reject_margin},
            "paired_acceptance": {
                "enabled": paired_enabled,
                "confidence_z": paired_z,
                "min_inner_lcb_delta": paired_min_inner_lcb_delta,
                "min_long_lcb_delta": paired_min_long_lcb_delta,
                "min_holdout_lcb_delta": paired_min_holdout_lcb_delta,
            },
            "run_cache": run_cache,
            "io": {
                "write_eval_artifacts_for_inner": write_eval_inner,
                "write_eval_artifacts_for_holdout": write_eval_holdout,
                "prune_rejected_iterations": prune_rejected,
                "keep_candidate_if_rejected": keep_candidate_if_rejected,
            },
            "search": {"two_lane_enabled": two_lane, "ucb_explore_coeff": ucb_explore_coeff, "random_seed": search_random_seed},
            "runtime_hygiene": {"seed_jobs": seed_jobs, "cpu_count": cpu_count, "reserve_cpu_cores": reserve_cpu_cores, "runtime_env": runtime_env},
        },
        "iterations_completed": len(list(it_root.glob("iter_*"))),
    }
    write_json(out_root / "final_report.json", final)

    # Also update the live config to best-so-far so subsequent runs use best known settings.
    if not bool(args.no_write_live_config):
        shutil.copyfile(best_cfg_path, config_path)

    # Mechanism-gap ticket on structural stop.
    if final["stop_condition"] == "STRUCTURAL_CHANGE_SIGNAL":
        ticket = {
            "ticket_type": "mechanism-gap",
            "top_violations": best_top3,
            "evidence": {
                "objective_plateau": True,
                "plateau_iterations": int(schema.get("plateau_iterations_structural", 8)),
            },
            "likely_missing_mechanism": "Observed violations persist under one-group parameter perturbations.",
            "code_areas_hint": [
                "src/cli_main.cpp",
                "src/map.cpp",
                "src/country.cpp",
                "src/trade.cpp",
            ],
        }
        write_json(out_root / "mechanism_gap_ticket.json", ticket)

    if final["stop_condition"] == "SAFETY":
        minimal_fix = {
            "stop_condition": "SAFETY",
            "reason": "Hard-gate failures persisted for 5 consecutive iterations.",
            "minimal_fix_required": [
                "Stabilize determinism/reproducibility for canary and parity, or fix missing metric emissions if present.",
                "Re-run baseline gate checks before resuming tuning."
            ],
        }
        write_json(out_root / "safety_stop_minimal_fix.json", minimal_fix)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
