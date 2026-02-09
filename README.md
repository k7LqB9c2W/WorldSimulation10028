# WorldSimulation

This project simulates open-ended societal development starting in 20,000 BCE by default.
The model is free-form: outcomes should emerge from mechanisms (food, logistics, disease, institutions, trade, war), not scripted historical timelines.

## Quick Start

### GUI build/run
- Build your normal GUI target (`WorldSimulation`) with your existing CMake/Visual Studio workflow.
- Optional runtime config override:
  - `WorldSimulation --config data/sim_config.toml`
- On launch, the GUI shows an ImGui start-year prompt before simulation begins.
  - Allowed range: `-20000` to configured `world.endYear`
  - Default input: configured `world.startYear` (now `-20000`)
  - The same modal now also configures:
    - world population mode (`fixed` or `range`)
    - population value/range
    - regional spawn mask toggle
    - 5000 BCE regional start-tech preset toggle

### CLI build/run
- Build target: `worldsim_cli`
- Example run:
  - `worldsim_cli --seed 42 --config data/sim_config.toml --outDir out/cli_runs/seed_42`

## Runtime Config (`data/sim_config.toml`)

The simulation now loads tuning values at runtime (no recompile needed).

### Main sections
- `world`: timeline bounds, tick size, deterministic mode knobs.
- `food`: foraging/farming productivity, coastal/riverland effects, spoilage/storage.
- `resources`: typed potential weights, normalizations, depletion rates.
- `migration`: shock thresholds/multipliers, corridor weights, refugee half-life.
- `war`: supply model, exhaustion, objectives, cooldown and peace aftermath weights.
- `polity`: region count and succession/elite-fragmentation sensitivities.
- `tech`: capability thresholds, diffusion base, cultural friction, resource requirements.
- `spawn`: regional spawn mask settings (`enabled`, `maskPath`, color tolerance, duplicate-color mode, optional `regions` list).
- `startTech`: optional region-based starting tech presets for specific trigger years (defaults target `-5000`).
- `economy`: elasticities/intensities and `useGPU` toggle.
- `scoring`: checkpoint years and stylized-fact weights for evaluation harness.

### How config is applied
- GUI and CLI both load config at startup.
- Default path: `data/sim_config.toml`
- Override path with: `--config <path>`

### Deterministic calibration recommendations
- Use a fixed `--seed`.
- Use the same config file contents (hash is written to output).
- For calibration/regression runs, disable GPU path:
  - `--useGPU 0` (CLI)
  - or set `economy.useGPU = false` in config.

## CLI Reference (`worldsim_cli`)

Supported flags:
- `--seed N`
- `--config path`
- `--startYear Y`
- `--endYear Y`
- `--checkpointEveryYears N`
- `--outDir path`
- `--useGPU 0|1`
- `--world-pop N`
- `--world-pop-range MIN MAX`
- `--spawn-mask path`
- `--spawn-disable`
- `--spawn-region-share KEY VALUE` (repeatable; `VALUE >= 1` is treated as percent, otherwise fraction)

`--startYear` supports values down to `-20000` (20,000 BCE).

Example:
- `worldsim_cli --seed 7 --config data/sim_config.toml --startYear -20000 --endYear 2025 --checkpointEveryYears 50 --outDir out/cli_runs/seed_7 --useGPU 0`
- `worldsim_cli --seed 7 --config data/sim_config.toml --world-pop-range 8000000 12000000 --spawn-region-share south_asia 28 --spawn-region-share east_asia 24 --outDir out/cli_runs/seed_7`

### CLI outputs
Each run writes:
- `run_summary.json`
- `timeseries.csv`

These include core metrics (population, urban proxy, war frequency, trade intensity, capability tiers, food security, disease burden, collapse count) plus invariant checks (NaN/inf, negative population/stocks, etc.).

## Technology Model (20,000 BCE-ready)
- Technology is now split into:
  - `known` (idea exists in-country)
  - `adoption` (`0..1`, practiced depth)
- “Unlocked”/`hasTech` is derived from adoption threshold (default `tech.adoptionThreshold = 0.65`), not instant unlock.
- Yearly flow:
  - domain knowledge innovation/diffusion
  - stochastic discovery (known=true) with feasibility gates
  - known-tech diffusion + slow adoption seeding from connected neighbors
  - adoption growth/decay (institutions, specialization, stability, connectivity, shocks)
  - rare forgetting in tiny isolated collapses
- Early Paleolithic/Mesolithic transitions were added (e.g., cordage, storage, proto-cultivation, sedentism), and agriculture/husbandry/pottery/writing/metallurgy now depend on realistic prerequisites + environment/resource gates.
- Technology effects are applied proportionally from adoption (smoothed), not as binary jumps.
- Mega time jump end-summary now includes median first-adoption years for milestone techs.

## Deep-History Realism
- Climate now includes a deterministic paleoclimate baseline shift:
  - colder/drier around 20,000 BCE
  - gradual warming/wetting toward Holocene
- City formation is gated by tech transition:
  - requires adopted Sedentism and Agriculture in both legacy and population-grid city founding paths.

## Python Evaluation Harness

### File: `tools/evaluate_run.py`

This script scores multiple seed runs against stylized facts and can enforce regression tolerance against a stored baseline.
Current automation script set includes this evaluator and the baseline JSON file.

### Inputs
- Directory containing per-seed run outputs (`run_summary.json` + `timeseries.csv`), e.g. `out/cli_runs/`

### Basic usage
- `python3 tools/evaluate_run.py --runsDir out/cli_runs --config data/sim_config.toml`

### Useful flags
- `--baseline tools/baseline_score.json`
- `--tolerance 0.03`
- `--out out/cli_runs/evaluation_summary.json`
- `--write-baseline` (writes current aggregate score as baseline)

### Typical workflow
1. Generate multiple seeds:
   - `worldsim_cli --seed 1 ... --outDir out/cli_runs/seed_1`
   - `worldsim_cli --seed 2 ... --outDir out/cli_runs/seed_2`
   - repeat for N seeds.
2. Evaluate:
   - `python3 tools/evaluate_run.py --runsDir out/cli_runs --config data/sim_config.toml`
3. If results are accepted, set/update baseline:
   - `python3 tools/evaluate_run.py --runsDir out/cli_runs --config data/sim_config.toml --write-baseline`

## Baseline File
- `tools/baseline_score.json`
- Stores baseline aggregate score used by evaluation regression checks.

## Notes
- Existing keyboard controls/features in GUI still apply.
- If you need fast parameter iteration, prefer CLI + config + evaluation harness over manual GUI tuning.
