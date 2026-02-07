#!/usr/bin/env python3
import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from typing import Dict, List, Tuple

try:
    import tomllib  # py3.11+
except Exception:  # pragma: no cover
    tomllib = None


def clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))


def safe_mean(xs: List[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def safe_stdev(xs: List[float]) -> float:
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
    den = math.sqrt(max(1e-12, dx2 * dy2))
    return max(-1.0, min(1.0, num / den))


def norm_corr(c: float) -> float:
    return clamp01(0.5 * (c + 1.0))


def slope(xs: List[float], ys: List[float]) -> float:
    n = min(len(xs), len(ys))
    if n < 2:
        return 0.0
    xs = xs[:n]
    ys = ys[:n]
    mx = safe_mean(xs)
    my = safe_mean(ys)
    num = 0.0
    den = 0.0
    for i in range(n):
        dx = xs[i] - mx
        num += dx * (ys[i] - my)
        den += dx * dx
    if den <= 1e-12:
        return 0.0
    return num / den


def load_weights(config_path: Path) -> Dict[str, float]:
    weights = {
        "food_stability": 1.0,
        "innovation_urban": 1.0,
        "empire_logistics": 1.0,
        "disease_transition": 1.0,
        "trade_inequality": 1.0,
        "variance_penalty": 1.0,
        "brittleness_penalty": 1.0,
    }
    if tomllib is None or not config_path.exists():
        return weights
    with config_path.open("rb") as f:
        cfg = tomllib.load(f)
    s = cfg.get("scoring", {})
    weights["food_stability"] = float(s.get("weightFoodSecurityStability", 1.0))
    weights["innovation_urban"] = float(s.get("weightInnovationUrbanization", 1.0))
    weights["empire_logistics"] = float(s.get("weightEmpireLogisticsConstraint", 1.0))
    weights["disease_transition"] = float(s.get("weightDiseaseTransition", 1.0))
    weights["trade_inequality"] = float(s.get("weightTradeResourceInequality", 1.0))
    weights["variance_penalty"] = float(s.get("weightVariancePenalty", 1.0))
    weights["brittleness_penalty"] = float(s.get("weightBrittlenessPenalty", 1.0))
    return weights


def read_timeseries(csv_path: Path) -> Dict[str, List[float]]:
    out: Dict[str, List[float]] = {}
    with csv_path.open("r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            for k, v in row.items():
                out.setdefault(k, [])
                try:
                    out[k].append(float(v))
                except Exception:
                    out[k].append(0.0)
    return out


def run_score(ts: Dict[str, List[float]], weights: Dict[str, float]) -> Dict[str, float]:
    years = ts.get("year", [])
    urban = ts.get("urbanShare", [])
    food = ts.get("foodSecurityMean", [])
    disease = ts.get("diseaseBurdenMean", [])
    collapse = ts.get("collapseCount", [])
    area = ts.get("medianCountryArea", [])
    trade = ts.get("tradeIntensity", [])
    cap = [
        (a + b + c) / 3.0
        for a, b, c in zip(
            ts.get("capabilityTier1Share", []),
            ts.get("capabilityTier2Share", []),
            ts.get("capabilityTier3Share", []),
        )
    ]
    n = min(len(years), len(urban), len(food), len(disease), len(collapse), len(area), len(trade), len(cap))
    if n < 4:
        return {
            "food_stability": 0.0,
            "innovation_urban": 0.0,
            "empire_logistics": 0.0,
            "disease_transition": 0.0,
            "trade_inequality": 0.0,
            "run_score": 0.0,
        }

    years = years[:n]
    urban = urban[:n]
    food = food[:n]
    disease = disease[:n]
    collapse = collapse[:n]
    area = area[:n]
    trade = trade[:n]
    cap = cap[:n]

    collapse_rate = [0.0]
    for i in range(1, n):
        dy = max(1.0, years[i] - years[i - 1])
        collapse_rate.append(max(0.0, collapse[i] - collapse[i - 1]) / dy)

    score1 = safe_mean([
        norm_corr(corr(food, urban)),
        norm_corr(corr(food, [-v for v in collapse_rate])),
    ])

    cap_growth = cap[-1] - cap[0]
    trade_growth = trade[-1] - trade[0]
    saturation_penalty = clamp01(max(0.0, cap_growth - 1.8 * max(0.0, trade_growth)) / 0.25)
    score2 = clamp01(
        safe_mean([
            norm_corr(corr(cap, urban)),
            norm_corr(corr(cap, trade)),
        ]) - 0.25 * saturation_penalty
    )

    overall_collapse = safe_mean(collapse_rate)
    high_area_threshold = percentile(area, 0.75)
    high_area_collapse = safe_mean([collapse_rate[i] for i in range(n) if area[i] >= high_area_threshold])
    constraint_bonus = clamp01((high_area_collapse - overall_collapse + 0.01) / 0.06)
    score3 = clamp01(0.6 * norm_corr(corr(area, collapse_rate)) + 0.4 * constraint_bonus)

    mid = n // 2
    early_u = urban[:mid]
    early_d = disease[:mid]
    late_d = disease[mid:]
    late_c = cap[mid:]
    score4 = safe_mean([
        norm_corr(corr(early_d, early_u)),
        norm_corr(-corr(late_d, late_c)),
    ])

    trade_trend = slope(years, trade)
    trend_score = clamp01((trade_trend + 0.00015) / 0.00030)
    score5 = clamp01(0.55 * norm_corr(corr(trade, cap)) + 0.45 * trend_score)

    wsum = (
        weights["food_stability"]
        + weights["innovation_urban"]
        + weights["empire_logistics"]
        + weights["disease_transition"]
        + weights["trade_inequality"]
    )
    if wsum <= 0.0:
        wsum = 1.0

    total = (
        score1 * weights["food_stability"]
        + score2 * weights["innovation_urban"]
        + score3 * weights["empire_logistics"]
        + score4 * weights["disease_transition"]
        + score5 * weights["trade_inequality"]
    ) / wsum

    return {
        "food_stability": score1,
        "innovation_urban": score2,
        "empire_logistics": score3,
        "disease_transition": score4,
        "trade_inequality": score5,
        "run_score": clamp01(total),
    }


def collect_runs(runs_dir: Path) -> List[Tuple[Path, Path]]:
    out: List[Tuple[Path, Path]] = []
    for summary in sorted(runs_dir.glob("**/run_summary.json")):
        csv_path = summary.parent / "timeseries.csv"
        if csv_path.exists():
            out.append((summary, csv_path))
    return out


def load_final_metric(ts: Dict[str, List[float]], key: str) -> float:
    vals = ts.get(key, [])
    return float(vals[-1]) if vals else 0.0


def main() -> int:
    p = argparse.ArgumentParser(description="Evaluate worldsim stylized-fact realism over multiple seeds.")
    p.add_argument("--runsDir", required=True, help="Directory containing per-seed run_summary.json/timeseries.csv")
    p.add_argument("--config", default="data/sim_config.toml", help="Config file for scoring weights")
    p.add_argument("--baseline", default="tools/baseline_score.json", help="Baseline score JSON path")
    p.add_argument("--tolerance", type=float, default=0.03, help="Regression tolerance (relative floor)")
    p.add_argument("--out", default="", help="Optional output JSON path")
    p.add_argument("--write-baseline", action="store_true", help="Write computed aggregate as baseline")
    args = p.parse_args()

    runs_dir = Path(args.runsDir)
    if not runs_dir.exists():
        raise SystemExit(f"runsDir not found: {runs_dir}")

    weights = load_weights(Path(args.config))
    runs = collect_runs(runs_dir)
    if not runs:
        raise SystemExit("No runs found (expected **/run_summary.json with neighboring timeseries.csv).")

    per_run = []
    final_vectors = {
        "urbanShare": [],
        "tradeIntensity": [],
        "capabilityTier3Share": [],
        "collapseCount": [],
        "diseaseBurdenMean": [],
    }
    for summary_path, csv_path in runs:
        ts = read_timeseries(csv_path)
        score = run_score(ts, weights)
        per_run.append({
            "run": str(summary_path.parent),
            **score,
        })
        for k in final_vectors:
            final_vectors[k].append(load_final_metric(ts, k))

    scores = [r["run_score"] for r in per_run]
    median_score = statistics.median(scores)
    stdev_score = safe_stdev(scores)

    target_cv = 0.18
    brittle_components = []
    for k, vals in final_vectors.items():
        m = abs(safe_mean(vals))
        s = safe_stdev(vals)
        cv = (s / m) if m > 1e-9 else s
        brittle_components.append(min(1.0, abs(cv - target_cv) / max(1e-9, target_cv)))
    brittleness_penalty = safe_mean(brittle_components)
    variance_penalty = min(1.0, stdev_score / 0.15)

    aggregate = clamp01(
        median_score
        - 0.20 * max(0.0, weights["variance_penalty"]) * variance_penalty
        - 0.20 * max(0.0, weights["brittleness_penalty"]) * brittleness_penalty
    )

    result = {
        "run_count": len(per_run),
        "weights": weights,
        "median_run_score": median_score,
        "stdev_run_score": stdev_score,
        "variance_penalty": variance_penalty,
        "brittleness_penalty": brittleness_penalty,
        "aggregate_score": aggregate,
        "runs": per_run,
    }

    print(json.dumps(result, indent=2))

    out_path = Path(args.out) if args.out else (runs_dir / "evaluation_summary.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2), encoding="utf-8")

    baseline_path = Path(args.baseline)
    if args.write_baseline:
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text(
            json.dumps(
                {
                    "aggregate_score": aggregate,
                    "median_run_score": median_score,
                    "stdev_run_score": stdev_score,
                    "run_count": len(per_run),
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        return 0

    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
        base_score = float(baseline.get("aggregate_score", 0.0))
        base_runs = int(baseline.get("run_count", 0))
        if base_runs > 0:
            floor = base_score - max(abs(base_score) * args.tolerance, args.tolerance)
            if aggregate + 1e-12 < floor:
                print(
                    f"REGRESSION: aggregate_score={aggregate:.6f} below floor={floor:.6f} "
                    f"(baseline={base_score:.6f}, tol={args.tolerance:.6f})"
                )
                return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
