# TUNINGLOG.MD

Living tuning log for `FineTuning.MD` runs. Append new entries; do not delete historical context.

## Entry: 2026-02-07 13:44:34 PST (2026-02-07 21:44:34 UTC)

### Run Scope
- Objective: maximize realism score under `FineTuning.MD` gates (metrics, canary, parity, holdout).
- Constraint: parameter-only tuning (`sim_config`/schema), no C++ mechanism edits unless structural stop.
- End-year handling: runtime argument respected (no hardcoding in evaluator logic).

### Final Outcome
- Stop condition: `STRUCTURAL_CHANGE_SIGNAL`.
- Final report: `out/fine_tuning_parallel2/final_report.json`.
- Mechanism ticket: `out/fine_tuning_parallel2/mechanism_gap_ticket.json`.
- Best tuning objective: `40.724344571064734`.
- Best holdout objective: `38.731423353681365`.
- Best top violation: `STORAGE_SMOOTHING_CHEAT`.
- Best config hash16: `f26dbf166d674f45`.
- Best config path: `out/fine_tuning_parallel2/best_sim_config.toml`.
- Live config: `data/sim_config.toml` (matches best hash).

### Baseline Gate Status (parallel2)
- Metric availability: pass.
- Canary reproducibility: pass.
- Backend parity: pass.
- Baseline objective file: `out/fine_tuning_parallel2/baseline_objective.json`.
- Baseline gate file: `out/fine_tuning_parallel2/baseline_gates.json`.
- Dominant violations at baseline: `[
  "STORAGE_SMOOTHING_CHEAT"
]`.

### Iteration Results (`out/fine_tuning_parallel2/iterations/*/iteration.json`)
- `iter_001`: group `food`, edit `food.baseForaging 19.5 -> 18.0`, objective `40.00953973086017`, delta `-0.7148048402045646`, accepted `false`.
- `iter_002`: group `migration`, edit `migration.famineShockMultiplier 1.6 -> 1.65`, objective `39.90435475183769`, delta `-0.8199898192270467`, accepted `false`.
- `iter_003`: group `disease`, edit `disease.endemicBase 0.002 -> 0.0018`, objective `39.65461869919681`, delta `-1.0697258718679237`, accepted `false`.
- `iter_004`: group `war`, edit `war.overSupplyAttrition 0.1 -> 0.11`, objective `40.553509307664406`, delta `-0.17083526340032762`, accepted `false`.
- `iter_005`: group `tech`, edit `tech.diffusionBase 0.015 -> 0.014`, objective `40.11610486289898`, delta `-0.6082397081657547`, accepted `false`.
- `iter_006`: group `economy`, edit `economy.tradeIntensityScale 18 -> 18.75`, objective `40.20035185731012`, delta `-0.5239927137546161`, accepted `false`.
- `iter_007`: group `polity`, edit `polity.demogShortageStabilityHit 0.01 -> 0.009`, objective `39.92251611385825`, delta `-0.8018284572064829`, accepted `false`.
- `iter_008`: group `food`, edit `food.baseFarming 44 -> 46`, objective `40.04757580927078`, delta `-0.6767687617939515`, accepted `false`.

Common pattern:
- Top violation before/after stayed `STORAGE_SMOOTHING_CHEAT` for all iterations.
- No tuning hardfails.
- Canary/parity only run when provisional gates were met (per evaluator design).

### Best-So-Far Config Notes
- Active best config: `data/sim_config.toml`.
- Key values tied to best accepted state from prior cycle:
  - `food.baseForaging = 19.5`
  - `tech.diffusionBase = 0.015`
- Best config hash16: `f26dbf166d674f45`.

### Tooling/Process Changes Made During This Cycle
- Added safe parallel seed batching in tuner (`tools/fine_tune_realism.py`) using separate processes per seed:
  - `run_seed_set(..., jobs=N)` with `--seed-jobs` CLI flag.
  - Seed results are still returned in deterministic seed order.
- Fixed safety-gate accounting bug in tuner:
  - `SAFETY` counter now increments only on actual executed gate failures, not when canary/parity are intentionally skipped due to failing provisional improvement gate.

### Factors To Note For Next Tuning Cycle
- Current best is already strong relative to nearby perturbations; most one-step schema moves reduce objective.
- Dominant violation persistence (`STORAGE_SMOOTHING_CHEAT`) across all subsystem edits indicates mechanism limitation, not simple parameter miss.
- One-group, one-step local proposals are likely insufficient around this basin.
- Holdout/canary/parity are stable when checks are executed; no reproducibility instability observed in this run.

### Recommendations Before Next Tuning Attempt
- Treat this as a true mechanism-gap state unless new tunable parameters are exposed that directly affect the storage smoothing criterion terms.
- If staying parameter-only, prioritize targeted proposal logic around storage/loss/capability-linked dynamics rather than round-robin broad groups.
- Keep parallel seed execution (`--seed-jobs 4`) for throughput; it materially reduces wall-time without changing determinism semantics.
- Preserve artifact paths and hashes from this entry for reproducibility checks.

