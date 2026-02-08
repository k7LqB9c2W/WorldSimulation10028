# Mechanism-Gap Ticket: STORAGE_SMOOTHING_CHEAT Plateau

Timestamp: 2026-02-08
Source Runs:
- out/fine_tuning_parallel2/final_report.json
- out/fine_tuning_parallel1/final_report.json
- out/fine_tuning_autorun/final_report.json

## Trigger
`STRUCTURAL_CHANGE_SIGNAL` triggered under FineTuning.MD stop logic with persistent dominant violation and no accepted improvements over the structural plateau window.

## Top Violations
1. `STORAGE_SMOOTHING_CHEAT` (dominant, persistent)

## Evidence
- Best objective plateaued around `40.72` (`out/fine_tuning_parallel2/final_report.json`).
- Dominant violation stayed unchanged across iterations/runs.
- Baseline tuning seeds show near-max severity:
  - seed_101: severity `99.33`, adequacy_var `1.666e-05`, loss_share `0.0335`
  - seed_202: severity `99.86`, adequacy_var `3.515e-06`, loss_share `0.0258`
  - seed_303: severity `99.63`, adequacy_var `9.177e-06`, loss_share `0.0128`
  - seed_404: severity `99.80`, adequacy_var `4.949e-06`, loss_share `0.0154`
- Required threshold from definitions: `VarMin = 0.0025`; observed variance is roughly 2-3 orders of magnitude smaller.
- End-of-run capability proxy is also very low (`world_tech_capability_index_median` ~ `0.02`), so anti-loophole low-cap branch is always active.

## Likely Missing Mechanism
The economy model over-smooths world food adequacy at low capability because it lacks sufficient low-cap volatility/friction coupling into annual food availability and loss pathways. In practice, preindustrial systems should exhibit larger harvest variability and distribution/storage frictions, especially under weak state/logistics capacity.

Current behavior indicates:
- Food availability is buffered too smoothly over long windows.
- Loss accounting proxy used for anti-loophole checks is weakly coupled to true pre-loss/pre-storage dynamics.

## Proposed Structural Fix
Implement low-cap realism mechanisms in the simulation core:
1. Add deterministic low-cap harvest variability (global + local climate-linked signal) to annual food output.
2. Add explicit post-harvest handling/storage leakage at low capability (in addition to spoilage), tied to institutions/logistics and climate stress.
3. Record explicit per-year food-loss accounting fields in macro state and use them for evaluator timeseries metrics to align measured `loss_share` with actual in-sim losses.

## Code Areas Hint
- `src/economy.cpp`
  - Food output calculation path (`sectorOutput` / `foodOutAnnual`)
  - Storage/spoilage block around food availability before trade
- `include/country.h`
  - `Country::MacroEconomyState` fields for explicit food loss accounting
- `src/cli_main.cpp`
  - Snapshot metric assembly for `spoilage_kcal`, `storage_loss_kcal`, `available_kcal_before_losses`

## Acceptance Criteria After Fix
- `STORAGE_SMOOTHING_CHEAT` severity should materially drop on baseline seeds.
- `world_food_adequacy_index` variance over last N checkpoints should increase toward/above `VarMin` in low-cap regimes OR low-cap branch prerequisites should be satisfied with plausible storage/loss signals.
- Determinism/canary/parity gates must remain passing.

## Follow-up: 2026-02-08 (Post-Fix Curriculum Run)

Source run:
- `out/fine_tuning_curriculum_run1/final_report.json`
- `out/fine_tuning_curriculum_run1/mechanism_gap_ticket.json`

Observed status:
- `STORAGE_SMOOTHING_CHEAT` no longer appears in top violations (`best_top3_violations = []`).
- Best long-horizon objective improved to `62.458911376190606`.
- Canary/parity/holdout all pass for the accepted improvement (`iter_008`).
- Stop condition is still `STRUCTURAL_CHANGE_SIGNAL` due objective plateau under one-step parameter perturbations.

Likely remaining mechanism gap:
- With hard failures and major violations cleared, remaining score loss appears to be distributed across soft objective components, suggesting limited parameter leverage in the current exposed tuning set.
- Next structural step should target additional simulation mechanisms or new tunable parameters tied to the weakest realism component scores (rather than anti-cheat/violation gates).
