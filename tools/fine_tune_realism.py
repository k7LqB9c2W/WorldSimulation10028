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
import shutil
import statistics
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple

try:
    import tomllib  # py311+
except Exception as exc:  # pragma: no cover
    raise SystemExit(f"Python 3.11+ is required (tomllib missing): {exc}")


TINY = 1e-12


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
        return tomllib.load(f)


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
    for section, values in cfg.items():
        if not isinstance(values, dict):
            continue
        lines.append(f"[{section}]")
        for k, v in values.items():
            lines.append(f"{k} = {format_toml_value(v)}")
        lines.append("")
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def get_param(cfg: Dict[str, Any], path: str) -> Any:
    a, b = path.split(".", 1)
    return cfg[a][b]


def set_param(cfg: Dict[str, Any], path: str, value: Any) -> None:
    a, b = path.split(".", 1)
    cfg[a][b] = value


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


def run_cli(
    exe_dir: Path,
    seed: int,
    config_path: Path,
    out_dir: Path,
    start_year: int,
    end_year: int,
    checkpoint_every: int,
    use_gpu: bool,
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
    p = subprocess.run(
        ["/mnt/c/Windows/System32/cmd.exe", "/c", cmd],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
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


def choose_direction(path: str, top_viol: List[str], old_val: Any, pdef: Dict[str, Any], iteration: int) -> int:
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
) -> List[SeedEval]:
    out_dir.mkdir(parents=True, exist_ok=True)
    n_jobs = max(1, min(int(jobs), len(seeds)))

    def run_one(seed: int) -> SeedEval:
        sd = out_dir / f"seed_{seed}"
        run_cli(exe_dir, seed, config_path, sd, start_year, end_year, checkpoint_every, use_gpu)
        return evaluate_seed_run(seed, sd, defs)

    if n_jobs == 1:
        return [run_one(seed) for seed in seeds]

    by_seed: Dict[int, SeedEval] = {}
    with ThreadPoolExecutor(max_workers=n_jobs) as pool:
        futures = {pool.submit(run_one, seed): seed for seed in seeds}
        for fut in as_completed(futures):
            seed = futures[fut]
            by_seed[seed] = fut.result()
    return [by_seed[seed] for seed in seeds]


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

    defs = load_json(defs_path)
    schema = load_json(schema_path)
    cfg0 = load_toml(config_path)
    start_year = int(cfg0["world"]["startYear"])
    end_year = int(cfg0["world"]["endYear"])
    checkpoint_every = int(schema.get("checkpoint_every_years", 50))
    seed_jobs = max(1, int(args.seed_jobs))
    tuning_seeds = [int(x) for x in schema["tuning_seeds"]]
    holdout_seeds = [int(x) for x in schema["holdout_seeds"]]
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
    if (not args.force_rebaseline) and baseline_obj_path.exists() and baseline_gate_path.exists():
        bo = load_json(baseline_obj_path)
        base_agg = bo["tuning"]
        base_holdout_agg = bo["holdout"]
        base_top3 = list(bo.get("top3", []))
        bg = load_json(baseline_gate_path)
        canary_ok = bool(bg.get("canary_pass", False))
        parity_ok = bool(bg.get("parity_pass", False))
        canary_det = list(bg.get("canary", []))
        parity_det = list(bg.get("parity", []))
    else:
        baseline_tune = run_seed_set(
            tuning_seeds,
            exe_dir,
            config_path,
            baseline_dir / "tuning",
            start_year,
            end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
        )
        base_agg = aggregate_objective(baseline_tune, defs)
        base_top3 = top3_violations(baseline_tune)
        baseline_holdout = run_seed_set(
            holdout_seeds,
            exe_dir,
            config_path,
            baseline_dir / "holdout",
            start_year,
            end_year,
            checkpoint_every,
            bool(cfg0["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
        )
        base_holdout_agg = aggregate_objective(baseline_holdout, defs)
        write_json(out_root / "baseline_objective.json", {"tuning": base_agg, "holdout": base_holdout_agg, "top3": base_top3})

        # Canary/parity baseline.
        canary_dir = baseline_dir / "canary"
        canary_a = run_seed_set([tuning_seeds[0]], exe_dir, config_path, canary_dir / "a", start_year, end_year, checkpoint_every, bool(cfg0["economy"]["useGPU"]), defs)[0]
        canary_b = run_seed_set([tuning_seeds[0]], exe_dir, config_path, canary_dir / "b", start_year, end_year, checkpoint_every, bool(cfg0["economy"]["useGPU"]), defs)[0]
        canary_ok, canary_det = compare_metric_series(canary_a, canary_b, defs["canary_eps"])
        parity_dir = baseline_dir / "parity"
        parity_gpu = run_seed_set([tuning_seeds[0]], exe_dir, config_path, parity_dir / "gpu", start_year, end_year, checkpoint_every, True, defs)[0]
        parity_cpu = run_seed_set([tuning_seeds[0]], exe_dir, config_path, parity_dir / "cpu", start_year, end_year, checkpoint_every, False, defs)[0]
        parity_ok, parity_det = compare_metric_series(parity_gpu, parity_cpu, defs["parity_eps"])
        write_json(out_root / "baseline_gates.json", {"canary_pass": canary_ok, "canary": canary_det, "parity_pass": parity_ok, "parity": parity_det})

    best_obj = float(base_agg["objective"])
    best_holdout_obj = float(base_holdout_agg["objective"])
    best_eval = {"tuning": base_agg, "holdout": base_holdout_agg}
    best_top3 = base_top3

    # Iterative loop.
    group_idx = 0
    per_group_param_idx: Dict[str, int] = {g: 0 for g in groups}
    prev_top3 = base_top3
    for it in range(1, args.max_iterations + 1):
        it_dir = it_root / f"iter_{it:03d}"
        it_dir.mkdir(parents=True, exist_ok=True)

        group = groups[group_idx % max(1, len(groups))]
        plist = by_group[group]
        pi = per_group_param_idx[group] % max(1, len(plist))
        pdef = plist[pi]
        per_group_param_idx[group] += 1
        group_idx += 1
        path = pdef["path"]
        old_val = get_param(best_cfg, path)
        direction = choose_direction(path, prev_top3, old_val, pdef, it)
        new_val = apply_step(old_val, pdef, direction)
        cand_cfg = copy.deepcopy(best_cfg)
        set_param(cand_cfg, path, new_val)
        cand_cfg_path = it_dir / "candidate_sim_config.toml"
        dump_toml(cand_cfg, cand_cfg_path)

        cand_tune = run_seed_set(
            tuning_seeds,
            exe_dir,
            cand_cfg_path,
            it_dir / "tuning",
            start_year,
            end_year,
            checkpoint_every,
            bool(cand_cfg["economy"]["useGPU"]),
            defs,
            jobs=seed_jobs,
        )
        cand_agg = aggregate_objective(cand_tune, defs)
        cand_top3 = top3_violations(cand_tune)
        tune_hardfails = sorted({hf for s in cand_tune for hf in s.hardfails})
        objective_delta = float(cand_agg["objective"]) - float(best_obj)

        # Run expensive canary/parity checks only for candidates that clear basic improvement gates.
        canary_pass = False
        parity_pass = False
        canary_detail: List[Dict[str, Any]] = []
        parity_detail: List[Dict[str, Any]] = []
        if (len(tune_hardfails) == 0) and (objective_delta >= float(defs["thresholds"]["min_delta"])):
            cand_canary_a = run_seed_set([tuning_seeds[0]], exe_dir, cand_cfg_path, it_dir / "canary" / "a", start_year, end_year, checkpoint_every, bool(cand_cfg["economy"]["useGPU"]), defs)[0]
            cand_canary_b = run_seed_set([tuning_seeds[0]], exe_dir, cand_cfg_path, it_dir / "canary" / "b", start_year, end_year, checkpoint_every, bool(cand_cfg["economy"]["useGPU"]), defs)[0]
            canary_pass, canary_detail = compare_metric_series(cand_canary_a, cand_canary_b, defs["canary_eps"])

            cand_parity_gpu = run_seed_set([tuning_seeds[0]], exe_dir, cand_cfg_path, it_dir / "parity" / "gpu", start_year, end_year, checkpoint_every, True, defs)[0]
            cand_parity_cpu = run_seed_set([tuning_seeds[0]], exe_dir, cand_cfg_path, it_dir / "parity" / "cpu", start_year, end_year, checkpoint_every, False, defs)[0]
            parity_pass, parity_detail = compare_metric_series(cand_parity_gpu, cand_parity_cpu, defs["parity_eps"])

        holdout_ok = False
        holdout_agg = {}
        holdout_hardfails: List[str] = []

        # Provisional acceptance gate before holdouts.
        no_hardfail_tuning = len(tune_hardfails) == 0
        improve_ok = objective_delta >= float(defs["thresholds"]["min_delta"])
        if no_hardfail_tuning and improve_ok and canary_pass and parity_pass:
            cand_hold = run_seed_set(
                holdout_seeds,
                exe_dir,
                cand_cfg_path,
                it_dir / "holdout",
                start_year,
                end_year,
                checkpoint_every,
                bool(cand_cfg["economy"]["useGPU"]),
                defs,
                jobs=seed_jobs,
            )
            holdout_agg = aggregate_objective(cand_hold, defs)
            holdout_hardfails = sorted({hf for s in cand_hold for hf in s.hardfails})
            holdout_delta_req = float(defs["thresholds"]["holdout_objective_min_delta"])
            holdout_ok = (
                len(holdout_hardfails) <= int(defs["thresholds"]["holdout_hardfail_max"])
                and float(holdout_agg["objective"]) >= float(best_holdout_obj) + holdout_delta_req
            )
        else:
            holdout_ok = False

        accepted = no_hardfail_tuning and improve_ok and canary_pass and parity_pass and holdout_ok
        if accepted:
            best_cfg = cand_cfg
            best_obj = float(cand_agg["objective"])
            best_holdout_obj = float(holdout_agg["objective"])
            best_eval = {"tuning": cand_agg, "holdout": holdout_agg}
            best_top3 = cand_top3
            dump_toml(best_cfg, best_cfg_path)
            accepted_iters += 1
            accepted_since_improve = 0
        else:
            accepted_since_improve += 1

        gate_failed = False
        checks_executed = no_hardfail_tuning and improve_ok
        if "MISSING_METRIC" in tune_hardfails:
            gate_failed = True
        # Count canary/parity only when those checks are actually executed.
        if checks_executed and (not canary_pass):
            gate_failed = True
        if checks_executed and (not parity_pass):
            gate_failed = True
        consecutive_gate_fail = (consecutive_gate_fail + 1) if gate_failed else 0

        if cand_top3 == prev_top3 and objective_delta < float(defs["thresholds"]["min_delta"]):
            plateau_same_top3 += 1
        else:
            plateau_same_top3 = 0
        prev_top3 = cand_top3

        iter_json = {
            "iteration": it,
            "subsystem_group": group,
            "parameter_edits": [
                {
                    "path": path,
                    "old": old_val,
                    "new": new_val,
                    "recommended_step": pdef["recommended_step"],
                }
            ],
            "top_violations_before": base_top3 if it == 1 else best_top3,
            "top_violations_after": cand_top3,
            "objective_tuning": cand_agg["objective"],
            "score_delta": objective_delta,
            "gates": {
                "metric_availability": "MISSING_METRIC" not in tune_hardfails,
                "canary_pass": canary_pass,
                "backend_parity_pass": parity_pass,
                "holdout_pass": holdout_ok,
                "tuning_hardfails": tune_hardfails,
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
        }
        write_json(it_dir / "iteration.json", iter_json)

        # Stop conditions (first triggered wins).
        if (
            accepted_iters >= int(schema.get("convergence_iterations", 50))
            and accepted_since_improve >= int(schema.get("convergence_iterations", 50))
            and best_top3 == cand_top3
        ):
            stop_reason = "CONVERGENCE"
            break

        major_left = any(float(v.get("severity", 0.0)) >= float(schema.get("major_violation_threshold", 50.0)) for s in cand_tune for v in s.violations)
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
        "best_holdout_objective": best_holdout_obj,
        "best_top3_violations": best_top3,
        "best_config": str(best_cfg_path),
        "best_config_hash16": hash16(best_cfg_path),
        "best_eval": best_eval,
        "iterations_completed": len(list(it_root.glob("iter_*"))),
    }
    write_json(out_root / "final_report.json", final)

    # Also update the live config to best-so-far so subsequent runs use best known settings.
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
