# FineTuning.MD (Realism Envelope Edition, v7, fully specified components, cleanup pass)

## 0) Scope and non-goals
- Simulation spans from 5000 BCE to a configurable end year.
- End year MUST be a runtime argument, not a fixed constant in evaluator logic.
- Do NOT match Earth’s named events, borders, leaders, religions, or calendar milestones.
- Do NOT target exact dates for “eras” or tech levels.
- Optimize for: constraints, feedback loops, geography, logistics, disease, institutions, and endogenous regime changes.
- Diversity across seeds is mandatory. Converging to a single storyline is a failure mode.

Realism definition:
A run is “realistic” if it respects hard physical and biological anchors, produces plausible spatial clustering, and exhibits correct directional relationships between scarcity, migration, conflict, disease, trade, and state capacity.

## 1) Output contract (stable artifacts)
Headless runs must always write:
- run_meta.json
  - seed, config_path, config_hash, git_commit, backend(cpu/gpu), start_year, end_year, map_hash
  - goals_version, evaluator_version, definitions_version, scoring_version
- run_summary.json
  - scalar metrics, distributions, window summaries (optional), total score, pass/fail gates, top violations
- timeseries.csv
  - fixed interval checkpoints (25 or 50 years)

Additionally required:
- violations.json
- iteration.json (optimizer loop per iteration)

## 2) Definitions registry and centralized thresholds
All thresholds, epsilons, scoring weights, and symbols used below live in:
- realism_definitions.json (or .yaml), versioned as definitions_version

The evaluator MUST read them from that file and MUST write the concrete values into run_meta.json.

### 2.1 Default thresholds and symbols (units and meaning)
Capability thresholds (dimensionless indices):
- capability_T1 = 0.35
- capability_T2 = 0.70

Storage and logistics anti-loophole thresholds:
- storage_S1 = 0.50
- loss_L1 = 0.05
- logistics_R1 = 0.55
- transport_C1 = 0.40
- VarMin = 0.0025

Extinction grace and floor:
- extinction_pop_floor = 1000
- extinction_grace_years = 200

Settlement and access targets (geography scoring):
- settlement_target_share = 0.10
- access_target_sum = 1.10
- lat_entropy_target = 0.75

Early settlement emergence check:
- settlement_min_share_habitable = 0.02
- settlement_check_year = -2000

Event rate scaling base:
- rate_pop_base = 1e9

Shock rate definition (constraint scoring):
- shock_rate_weights:
  - a_famine = 1.0
  - b_epidemic = 1.0
  - c_war = 1.0
- shock_min_rate = 0.5   # per century per billion pop

Coupling ratio targets (response proportional to driver):
- response_ratio_target = 1.0
- response_ratio_tol = 1.0

Coupling lag and correlation windows:
- coupling_lag_L = 2
- corr_window_W = 6

Low-cap growth plausibility:
- lowcap_growth_target = 0.002
- lowcap_growth_tol = 0.004

Disease correlation targets (low capability):
- lowcap_disease_corr_target = 0.3
- lowcap_disease_corr_tol = 0.3

Health gating:
- health_threshold = 0.6
- disease_low_target = 0.01
- disease_low_tol = 0.02

Density proxy requirement:
- density_required_for_tuning = false

Cities requirement:
- require_cities_for_tuning = false

Trade and transport symbols used in anti-loophole checks:
- trade_distance_threshold = 800.0         # distance units must match your route distance units (documented in definitions)
- long_trade_share_max = 0.10             # max acceptable share of trade on routes beyond trade_distance_threshold at low capability
- long_distance_trade_proxy = "defined"    # see section 9.2 for computation rule
- transport_cost_index_source = "column_or_derived"  # see section 6 for how indices are provided

Loss share definition (used in 9.1):
- loss_share_definition = "(spoilage_kcal + storage_loss_kcal) / max(available_kcal_before_losses, tiny)"

Adequacy variance window N (used in 9.1):
- adequacy_var_window_N = 6    # number of checkpoints

Depletion monotonic window M (used in 9.3):
- depletion_monotonic_window_M = 8   # number of checkpoints

Sigmoid mapping (optional):
- use_sigmoid_adequacy = false
- sigmoid_k = 8

Holdout acceptance thresholds:
- holdout_objective_min_delta = -1.0
- holdout_hardfail_max = 0
- holdout_metric_eps:
  - world_pop_total: 0.5% relative
  - world_food_adequacy_index: 0.01 absolute
  - habitable_cell_share_pop_gt_small: 0.01 absolute
  - world_trade_intensity: 2.0% relative
  - world_urban_share_proxy: 0.02 absolute
  - major_war_rate: 0.5 absolute (per century per billion)
  - famine_wave_rate: 0.5 absolute (per century per billion)
  - epidemic_wave_rate: 0.5 absolute (per century per billion)

Scoring weights (checkpoint):
- wG = 0.25
- wC = 0.25
- wK = 0.35
- wR = 0.15

Penalty weights:
- Pmax_major = 40
- Pmax_medium = 20
- Pmax_minor = 5

Variance and failure penalties:
- lambdaVar = 0.5
- targetStd = 20
- lambdaFail = 200

Optimization thresholds:
- min_delta = 0.5

Coastal metric semantics (for g_access):
- pop_share_coastal = true   # locks pop_share_coastal_vs_inland as a SHARE in [0,1], not a ratio

## 3) Metric availability contract (hard gate)
For tuning, required metrics MUST exist and be non-empty.
- If any required metric is missing, emit MISSING_METRIC severity 100 and hard-fail the run for tuning.
- Some capability proxies are allowed to be missing only if explicitly marked optional in realism_definitions.json.

## 4) Determinism, RNG, and ordering (gates)
Hard gates:
- Simulation RNG must be independent of any UI, rendering, logging, or debug sampling RNG.
- Iteration order must be stable for any collection that influences outcomes.
- Canary reproducibility must pass.

## 5) Parity and canary semantics (tightened)
Two different comparison regimes.

### 5.1 Same-backend reproducibility (canary)
Goal: detect nondeterminism.
- Prefer exact match for integer metrics when determinism is enforced.
- Use tight epsilons for floats if float noise exists.

Default canary epsilons:
- world_pop_total: 0.05% relative
- world_food_adequacy_index: 0.002 absolute
- habitable_cell_share_pop_gt_small: 0.002 absolute
- world_trade_intensity: 0.25% relative
- world_urban_share_proxy: 0.002 absolute
- event rates per century per billion: 0.05 absolute

### 5.2 CPU vs GPU parity (looser than canary)
Default parity epsilons:
- world_pop_total: 0.2% relative
- world_food_adequacy_index: 0.01 absolute
- habitable_cell_share_pop_gt_small: 0.01 absolute
- world_trade_intensity: 1.0% relative
- world_urban_share_proxy: 0.01 absolute
- event rates per century per billion: 0.2 absolute

Comparison function:
- For each metric, compute max over checkpoints of error.
- Fail if any max error exceeds epsilon.

## 6) Capability indices and derived indices (fully specified)
Any “capability index” or “cost index” used in checks must be defined as either:
- a first-class output column in timeseries.csv, or
- a deterministic proxy formula computed by evaluator from existing columns

Each must be declared in realism_definitions.json:
- name
- column_name (if direct) OR formula (if derived)
- required_for_tuning: true/false

Required indices (defaults required_for_tuning = true):
- tech_capability_index_median
- logistics_capability_index
- storage_capability_index
- health_capability_index

Transport cost index:
- transport_cost_index must be either:
  - a timeseries column transport_cost_index, or
  - a derived index declared in definitions with its formula
If missing and required_for_tuning true -> MISSING_METRIC.

Health capability “exists” definition:
- health_capability_exists_t = (health_capability_index >= health_threshold)

## 7) Metric definitions (core)
### 7.1 Food adequacy index (0..2 internal, maps to 0..1 for scoring)
- world_food_adequacy_index is clamped to 0..2.

Mapping to 0..1 score:
If use_sigmoid_adequacy = false:
- adequacy_score_t = clamp(world_food_adequacy_index / 1.0, 0, 1)
If use_sigmoid_adequacy = true:
- adequacy_score_t = 1 / (1 + exp(-sigmoid_k*(world_food_adequacy_index - 1.0)))

### 7.2 Event rate scaling (scale-invariant)
For each checkpoint window:
- major_war_rate_t = (major_war_count / window_centuries) / max(world_pop_avg / rate_pop_base, tiny)
Similarly:
- famine_wave_rate_t
- epidemic_wave_rate_t
- mass_migration_rate_t

### 7.3 Famine exposure share (required for coupling)
- famine_exposure_share_t = pop_in_cells_with_adequacy_below_1_0 / max(world_pop_total, tiny)

### 7.4 Shock rate (explicit)
- shock_rate_t = a_famine*famine_wave_rate_t + b_epidemic*epidemic_wave_rate_t + c_war*major_war_rate_t
Weights come from shock_rate_weights.

### 7.5 City tail index (method pinned)
- Hill estimator on top-k city sizes as previously specified.
- If not enough cities:
  - if require_cities_for_tuning true -> emit CITY_TAIL_UNDEFINED severity 100 and hard-fail
  - else mark undefined and exclude from scoring

## 8) Required metrics (timeseries.csv)
World totals:
- year
- world_pop_total
- world_pop_growth_rate_annual
- world_food_adequacy_index
- world_famine_death_rate
- world_disease_death_rate
- world_war_death_rate
- world_trade_intensity
- world_urban_share_proxy
- world_tech_capability_index_median
- world_tech_capability_index_p90
- world_state_capacity_index_median
- world_state_capacity_index_p10
- migration_rate_t
- famine_exposure_share_t

Spatial distributions:
- habitable_cell_share_pop_gt_0
- habitable_cell_share_pop_gt_small
- pop_share_by_lat_band (summary arrays)
- pop_share_coastal_vs_inland   # MUST be a share in [0,1] per definitions
- pop_share_river_proximal
- market_access_p10, market_access_median
- food_adequacy_p10, food_adequacy_median
- travel_cost_index_median

Polities and cities:
- country_pop_median, country_pop_p90, country_pop_top1_share
- country_area_median, country_area_p90, country_area_top1_share
- control_median, control_p10
- wars_active_count
- city_pop_top1
- city_pop_top10_sum_share
- city_tail_index (if defined)

Events (since last checkpoint):
- famine_wave_count
- epidemic_wave_count
- major_war_count
- civil_conflict_count
- fragmentation_count
- mass_migration_count

Indices if first-class:
- logistics_capability_index
- storage_capability_index
- health_capability_index
- transport_cost_index (if used)

If derived, evaluator must compute and report them in run_summary.json.

## 9) Anti-loophole checks (explicit)
Hard-fail:
- MISSING_METRIC
- BACKEND_DIVERGENCE
- NONDETERMINISTIC_ORDERING
- EXTINCTION_PERSISTENT
- BROKEN_ACCOUNTING

Anti-loophole:
- STORAGE_SMOOTHING_CHEAT
- TRANSPORT_CHEAT
- DEPLETION_IGNORED (if modeled)

### 9.1 Storage smoothing cheat
Compute:
- adequacy_var = var(world_food_adequacy_index over last adequacy_var_window_N checkpoints)
- loss_share = (spoilage_kcal + storage_loss_kcal) / max(available_kcal_before_losses, tiny)
(If kcal is abstracted, compute loss_share using the analogous food-unit variables per loss_share_definition.)

Condition (low capability):
- if tech_capability_index_median < capability_T1
- and adequacy_var < VarMin
Then require:
- storage_capability_index >= storage_S1
- and loss_share >= loss_L1
Else emit STORAGE_SMOOTHING_CHEAT with severity:
- severity = clamp(100 * (VarMin - adequacy_var) / VarMin, 0, 100)

### 9.2 Transport cheat
Definitions:
- long_distance_trade_proxy is the share of total trade volume occurring on routes with distance > trade_distance_threshold:
  - long_distance_trade_proxy = trade_volume_long / max(trade_volume_total, tiny)
Where:
  - trade_volume_long = sum(route_flow for routes with route_distance > trade_distance_threshold)
  - trade_volume_total = sum(route_flow for all routes)
(If using trade_value instead of flow, use value consistently for both numerator and denominator.)

Condition (low capability):
- if tech_capability_index_median < capability_T1
- and long_distance_trade_proxy > long_trade_share_max
Then require:
- logistics_capability_index >= logistics_R1
- and transport_cost_index >= transport_C1
Else emit TRANSPORT_CHEAT with severity proportional to exceedance.

### 9.3 Depletion ignored (if extractives modeled)
Check window length = depletion_monotonic_window_M checkpoints.
If extraction increases monotonically over the window while exploration/tech does not increase:
- emit DEPLETION_IGNORED with severity proportional to slope and duration.

## 10) Component scores (fully defined, 0..1 each)
Helper functions:
- clamp01(x) = clamp(x, 0, 1)
- relu(x) = max(0, x)
- closeness(x, target, tol) = clamp01(1 - abs(x - target) / tol)

### 10.1 Geography score (geography_score_t)
A) Non-empty habitable settlement:
- g_settle_t = clamp01(habitable_cell_share_pop_gt_small / settlement_target_share)

B) Access clustering (coast + river):
- g_access_t = clamp01((pop_share_coastal_vs_inland + pop_share_river_proximal) / access_target_sum)

C) Lat band plausibility:
- entropy = -sum(p_i * log(p_i)) / log(Nbands)
- g_lat_t = clamp01(entropy / lat_entropy_target)

Geography score:
- geography_score_t = 0.45*g_settle_t + 0.35*g_access_t + 0.20*g_lat_t

### 10.2 Constraint score (constraint_score_t)
A) Adequacy meets needs:
- c_adequacy_t = adequacy_score_t

B) No “super-abundance” calm:
- c_shocks_t = clamp01(shock_rate_t / shock_min_rate)

C) Growth plausibility in low capability:
If tech < capability_T1:
- c_growth_t = closeness(world_pop_growth_rate_annual, lowcap_growth_target, lowcap_growth_tol)
Else:
- c_growth_t = 1.0

Constraint score:
- constraint_score_t = 0.45*c_adequacy_t + 0.25*c_shocks_t + 0.30*c_growth_t

### 10.3 Coupling score (coupling_score_t) with normalized responses
Compute deltas over lag L checkpoints (L from coupling_lag_L):
- d_adequacy = adequacy_score_t - adequacy_score_(t-L)
- d_migration = migration_rate_t - migration_rate_(t-L)
- d_conflict = major_war_rate_t - major_war_rate_(t-L)
- d_market = market_access_median_t - market_access_median_(t-L)
- d_famine_exposure = famine_exposure_share_t - famine_exposure_share_(t-L)

Define:
- shock = relu(-d_adequacy)

A) Scarcity shock should raise migration proportionally:
If shock > 0:
- response_ratio_mig = relu(d_migration) / max(shock, tiny)
- k_mig_t = closeness(response_ratio_mig, response_ratio_target, response_ratio_tol)
Else:
- k_mig_t = 1.0

B) Scarcity shock should raise conflict proportionally:
If shock > 0:
- response_ratio_war = relu(d_conflict) / max(shock, tiny)
- k_war_t = closeness(response_ratio_war, response_ratio_target, response_ratio_tol)
Else:
- k_war_t = 1.0

C) Market access increases should buffer famine exposure proportionally (unambiguous if/else chain):
If d_market > 0 and d_famine_exposure > 0:
- k_buffer_t = 0.0
Else if d_market > 0:
- response_ratio_buffer = relu(-d_famine_exposure) / max(relu(d_market), tiny)
- k_buffer_t = closeness(response_ratio_buffer, response_ratio_target, response_ratio_tol)
Else:
- k_buffer_t = 0.5

Coupling score:
- coupling_score_t = 0.40*k_mig_t + 0.35*k_war_t + 0.25*k_buffer_t

### 10.4 Regime consistency score (regime_consistency_score_t) with explicit density proxy priority
Density proxy priority order:
1) If timeseries has cell_density_pop_per_km2_mean (preferred):
   - density_proxy_t = cell_density_pop_per_km2_mean
2) Else if timeseries has world_urban_share_proxy (acceptable):
   - density_proxy_t = world_urban_share_proxy
3) Else if run_summary has pop_weighted_mean_cell_density (derived from grid):
   - density_proxy_t = pop_weighted_mean_cell_density
4) Else:
   - if density_required_for_tuning true -> MISSING_METRIC hard-fail
   - else disable density-disease correlation check and emit DENSITY_PROXY_MISSING (severity <= 10)

A) Low capability: density should correlate with disease burden
If tech < capability_T1 and density_proxy available:
- r = corr(density_proxy over last corr_window_W checkpoints, world_disease_death_rate over last corr_window_W)
- r_score = closeness(r, lowcap_disease_corr_target, lowcap_disease_corr_tol)
Else:
- r_score = 0.5

B) Health capability reversal:
If health_capability_exists_t:
- h_score = closeness(world_disease_death_rate, disease_low_target, disease_low_tol)
Else:
- h_score = 0.5

Regime consistency:
- regime_consistency_score_t = 0.60*r_score + 0.40*h_score

## 11) Scoring formula (fully specified)
Per checkpoint:
- checkpoint_score_t = wG*geography_score_t + wC*constraint_score_t + wK*coupling_score_t + wR*regime_consistency_score_t

Per seed:
- base_score_seed = mean(checkpoint_score_t)

Penalties:
- penalty_points(v) = Pmax(v.id) * (severity/100)^2
- total_score_seed = 100*base_score_seed - sum(penalty_points)

Seed aggregation objective:
- score_median = median(total_score_seed over tuning_seeds)
- variance_penalty = lambdaVar * max(0, stddev(total_score_seed) - targetStd)
- hardfail_penalty = lambdaFail * hardfail_rate
- objective = score_median - variance_penalty - hardfail_penalty

## 12) Extinction and grace (precise)
Extinction:
- world_pop_total < extinction_pop_floor

Hard-fail EXTINCTION_PERSISTENT if:
- world_pop_total remains below extinction_pop_floor for more than extinction_grace_years simulated years after first dropping below the floor.

## 13) Optimization protocol and acceptance (holdout pinned)
Default mode:
- Mode A is used for tuning objective.
- Mode B is reporting only and weight <= 10%.

Acceptance requires:
1) No hard-fails on tuning seeds.
2) objective improves by at least min_delta.
3) Holdout:
   - no new hard-fails
   - compute baseline_holdout_median_objective from the last accepted config
   - require: holdout_median_objective >= baseline_holdout_median_objective + holdout_objective_min_delta
   - per-metric regressions within holdout_metric_eps
4) Canary reproducibility passes.
5) Backend parity passes.
6) No new anti-loophole violations.

If STRUCTURAL_CHANGE_SIGNAL triggers:
- Stop tuning.
- Open mechanism-gap ticket.
- No further parameter edits until addressed.

## 14) Versioning
- definitions_version increments whenever realism_definitions changes.
- scoring_version increments when component formulas or weights change.
- Never compare scores across version changes.


15) Add early termination for hard-fail trajectories

If a run hits a “no chance” state (broken accounting, invariant failure, persistent extinction), stop simulating further years and still write the required artifacts with a hard-fail reason. That can cut a lot of wasted compute in bad configs, especially early in search. This aligns with your hard-fail gates and extinction grace logic.