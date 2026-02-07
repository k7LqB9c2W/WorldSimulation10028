• Run completed under FineTuning.MD with stop condition hit: STRUCTURAL_CHANGE_SIGNAL.

  Result

  - Stop condition: STRUCTURAL_CHANGE_SIGNAL (out/fine_tuning_parallel2/final_report.json)
  - Best objective (tuning): 40.724344571064734
  - Best objective (holdout): 38.731423353681365
  - Best top violation: STORAGE_SMOOTHING_CHEAT
  - Best config hash: f26dbf166d674f45
  - Best config file: out/fine_tuning_parallel2/best_sim_config.toml
  - Live config now matches best: data/sim_config.toml (same hash)

  Baseline (parallel2)

  - Metric availability: pass
  - Canary: pass
  - Backend parity: pass
  - Baseline tuning objective: 40.724344571064734
  - Baseline holdout objective: 38.731423353681365
  - Dominant violation IDs: ["STORAGE_SMOOTHING_CHEAT"]
  - Files: out/fine_tuning_parallel2/baseline_objective.json, out/fine_tuning_parallel2/baseline_gates.json

  Iteration-by-iteration (iteration.json content summary)

  - iter_001: food, food.baseForaging 19.5 -> 18.0, top3 before/after ["STORAGE_SMOOTHING_CHEAT"] -> ["STORAGE_SMOOTHING_CHEAT"], objective 40.00953973086017, delta -0.7148048402045646, gates: metric true, canary false, parity false,
    holdout false, accepted false.
  - iter_002: migration, migration.famineShockMultiplier 1.6 -> 1.65, top3 unchanged, objective 39.90435475183769, delta -0.8199898192270467, accepted false.
  - iter_003: disease, disease.endemicBase 0.002 -> 0.0018, top3 unchanged, objective 39.65461869919681, delta -1.0697258718679237, accepted false.
  - iter_004: war, war.overSupplyAttrition 0.1 -> 0.11, top3 unchanged, objective 40.553509307664406, delta -0.17083526340032762, accepted false.
  - iter_005: tech, tech.diffusionBase 0.015 -> 0.014, top3 unchanged, objective 40.11610486289898, delta -0.6082397081657547, accepted false.
  - iter_006: economy, economy.tradeIntensityScale 18 -> 18.75, top3 unchanged, objective 40.20035185731012, delta -0.5239927137546161, accepted false.
  - iter_007: polity, polity.demogShortageStabilityHit 0.01 -> 0.009, top3 unchanged, objective 39.92251611385825, delta -0.8018284572064829, accepted false.
  - iter_008: food, food.baseFarming 44 -> 46, top3 unchanged, objective 40.04757580927078, delta -0.6767687617939515, accepted false.

  Full per-iteration files:

  - out/fine_tuning_parallel2/iterations/iter_001/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_002/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_003/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_004/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_005/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_006/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_007/iteration.json
  - out/fine_tuning_parallel2/iterations/iter_008/iteration.json

  Mechanism-gap ticket emitted

  - File: out/fine_tuning_parallel2/mechanism_gap_ticket.json
  - Top violations: ["STORAGE_SMOOTHING_CHEAT"]
  - Evidence: objective plateau with same dominant violation for configured plateau window
  - Likely missing mechanism: persistent storage-smoothing behavior not correctable by current safe parameter perturbations
  - Code area hints: src/cli_main.cpp, src/map.cpp, src/country.cpp, src/trade.cpp

  Final best config

  - Active best config: data/sim_config.toml
  - Key best values (from accepted history): food.baseForaging=19.5, tech.diffusionBase=0.015 (plus existing baseline values that remained best).