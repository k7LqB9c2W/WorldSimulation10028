## 0) Scope and non-goals
- Do not match Earth’s political borders, named events, or calendar milestones.
- Do not target exact dates for population, tech, or “eras.”
- Optimize for plausible mechanisms: scarcity, shocks, feedback loops, geography, logistics, institutions.
- The sim must remain diverse across seeds. Converging to the same global storyline is a failure mode.

## 1) Output contract (must be stable)
Headless runs must always write:
- run_meta.json
  - seed, config_path, config_hash, git_commit, backend(cpu/gpu), start_year, end_year
- run_summary.json
  - scalar metrics and aggregated distributions
- timeseries.csv
  - checkpoints at fixed intervals (for example every 25 or 50 years)
  - include both world totals and distribution summaries

### Required checkpoint fields (timeseries.csv)
World:
- year
- world_pop_total
- world_pop_growth_rate_annual
- world_food_per_capita (or food_security_index)
- world_famine_death_rate
- world_war_death_rate
- world_disease_death_rate
- world_trade_intensity (trade volume or imports+exports over GDP proxy)
- world_urban_share_proxy (top city pop / total pop, or % pop in cells above density threshold)
- world_tech_capability_index (median and p90)
- world_collapse_events (per century rate)

Distributions (store as summary stats each checkpoint):
- country_pop_median, country_pop_p90, country_pop_gini (or top1_share)
- country_area_median, country_area_p90, top1_area_share
- city_pop_top1, city_pop_top10_sum_share
- food_security_p10, food_security_median
- market_access_median, market_access_p10
- control_median, control_p10
- wars_active_count

Events counters (since last checkpoint):
- famine_wave_count
- epidemic_wave_count
- major_war_count
- fragmentation_count
- civil_conflict_count
- mass_migration_count

## 2) Parameter hygiene for tuning
- Every tunable must live in sim_config.json with:
  - min, max, default
  - a short description
  - which subsystem it belongs to (food, disease, war, migration, tech, economy, polity)
- Evaluator must refuse configs that set parameters outside allowed bounds.
- Keep groups separate. The optimizer changes only one group per iteration until that group stabilizes.

Suggested tuning groups:
1) food + storage + spoilage
2) disease + density sensitivity
3) migration + shock multipliers + corridor weights
4) war + logistics + attrition + peace terms
5) polity + succession + elite cohesion
6) tech diffusion + cultural friction + capability thresholds

## 3) Anti-reward-hacking rules
### Hidden holdout checks
Maintain two check sets:
- visible_checks: used for optimization
- holdout_checks: not shown to the optimizer, only used to accept/reject

### Holdout seeds
- tuning_seeds: used during search (for example 32)
- holdout_seeds: never used during search, only validation (for example 16)

### Regression suite
Maintain a small golden set (for example 8 seeds) where key outputs must remain stable within tight tolerances.

## 4) Guardrails against “super earth” outcomes
Add explicit penalties for these patterns:
- Long stretches of near-zero famine, near-zero disease, near-zero war while population keeps rising.
- Food_security_index is permanently high and flat for millennia without corresponding increases in storage, logistics, and governance.
- Growth rate remains high for centuries with no negative feedback (no scarcity, no shocks, no fragmentation).
- Density becomes high in low-fertility or arid zones without irrigation capability and strong control.

Add explicit penalties for “dead world” outcomes:
- global population crashes to near-zero and stays there
- no large settlements ever form (urban proxy stays near zero forever)
- tech capability index shows no growth for extremely long spans across all seeds

## 5) Stylized facts additions (free timeline, mechanism-based)
Add more relationship checks that do not depend on dates:
- Trade distance decay:
  - trade intensity should drop with distance and rise with corridors (coast/river access)
- Scarcity-war link:
  - higher scarcity should increase conflict risk and migration pressure, with moderating effects from institutions and trade
- State size-control link:
  - large area share without matching control should predict fragmentation and civil conflict
- City size distribution:
  - top cities should form a heavy-tailed distribution, not uniform
- Disease-density link:
  - disease burden should rise with density pre-sanitation capability, then fall when sanitation/health capabilities spread
- Tech prerequisites:
  - high capability growth should correlate with surplus + market access + energy/ore availability, not random drift

## 6) Robustness tests (prevent overfitting)
Add perturbation tests beyond seeds:
- backend parity test (cpu vs gpu) for a short horizon with tolerance gates
- small parameter jitter test:
  - add +/- 2% noise to a subset of parameters and ensure outcomes do not flip from utopia to extinction
- map perturbations:
  - shuffle starting positions
  - small climate noise
  - optional: rotate longitude or mirror the map to detect hidden timeline bias

## 7) Stop conditions and “structural change needed” triggers
Parameter tuning is not enough when:
- Score plateaus for K iterations (for example 30) and the same 2 to 3 violation categories dominate.
- Improving one category reliably breaks another (for example fixing famine makes tech stall permanently).
- The optimizer repeatedly exploits the same loophole (for example inflating storage without costs).
- Tiny parameter changes cause phase transitions (mass extinction vs runaway) across many seeds.

When triggered:
- Do not continue tuning.
- Open a “mechanism gap” issue with:
  - the top violations
  - which metrics are impossible to satisfy simultaneously
  - a hypothesis for the missing mechanism
Examples of mechanism gaps:
- missing energy constraint
- missing logistics friction for war or state control
- missing depletion or diminishing returns
- missing water proxy or irrigation gating
- missing elite cohesion and succession instability

## 8) Evaluation aggregation rules
- Use median score across seeds as the primary objective.
- Add a penalty for low diversity:
  - if variance across seeds is too small, the sim is brittle or over-constrained.
- Add a penalty for excessive chaos:
  - if variance is extreme and most seeds fail hard-fails, stability feedback is missing.

## 9) Versioning
- Increment goals.yaml version when metrics change.
- Evaluator must include goals version and sim version in run_meta.json.
- Never compare scores across different goals versions.
