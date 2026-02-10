# 5000 BCE Baseline Research Notes (Earth-Like Start)

Date: 2026-02-10

Goal: provide an evidence-backed "anchor state" for simulation starts at 5000 BCE that feel plausibly Earth-like,
while still allowing divergence (not recreating the exact real timeline).

This document is written for WorldSimulation configuration and model design. It focuses on:
- What is broadly true about humans/societies at ~5000 BCE.
- Which simulation knobs those truths should constrain (primarily `data/sim_config.toml`).
- Which parts likely require tech-tree / mechanism refactors (e.g., splitting "Math" and "Writing").

## 1) High-Confidence Global Anchors (5000 BCE)

### 1.1 Population Magnitude (order-of-magnitude constraint)
- Our World in Data's long-run population series (HYDE v3.3 for 10,000 BCE–1799) gives:
  - World population at year -5000: 19,155,698 people.
- Continent aggregates in the same source at year -5000:
  - Asia: 9,452,731 (49.3%)
  - Europe: 1,609,853 (8.4%)
  - Africa: 1,453,426 (7.6%)
  - North America: 3,122,403 (16.3%)
  - South America: 3,129,050 (16.3%)
  - Oceania: 388,233 (2.0%)

Notes:
- These deep-history population estimates are uncertain; treat them as a calibration target band, not a single truth.
- The key constraint is that global population is in the "tens of millions", not billions, and is highly uneven spatially.

Sources:
- OWID population dataset and provenance (HYDE v3.3 for pre-1800): https://ourworldindata.org/grapher/sources-population-dataset
- OWID population download (World at -5000 can be found in CSV): https://ourworldindata.org/grapher/population.csv

### 1.2 Subsistence Regimes (mixed, uneven transition)
At ~5000 BCE the world is not uniformly "agricultural" or "foraging":
- There are core regions with established farming and large villages.
- There are regions with mixed cultivation + foraging + herding.
- There are many regions still primarily foraging or low-intensity horticulture.

Implication: the simulation should start with strong regional heterogeneity in food systems and risk exposure.

### 1.3 Urbanization and States
5000 BCE is earlier than the classic "first true city-states" phase.
In places like Mesopotamia, the Ubaid period includes large villages and proto-urban developments, but not
full "literate states" yet.

Source:
- The Met (Ubaid period overview): https://www.metmuseum.org/toah/hd/ubai/hd_ubai.htm

### 1.4 Writing: precursors exist; full writing is later
By ~5000 BCE:
- Accounting/record-keeping precursors exist (e.g., tokens/tallies; various mnemonic symbol systems).
- Full writing systems (e.g., proto-cuneiform / cuneiform administrative tablets) appear much later
  (late 4th millennium BCE, roughly 3350–3200 BCE in Mesopotamia).

Sources:
- The Met: "The Invention of Writing in Mesopotamia": https://www.metmuseum.org/toah/hd/wrtg/hd_wrtg.htm
- Schmandt-Besserat (UT Austin) on tokens and their multi-millennium role as accounting devices:
  https://sites.utexas.edu/dsb/tokens/the-evolution-of-writing/

Practical takeaway for WorldSimulation:
- In 5000 BCE presets, do NOT grant "Writing" as a default regional tech.
- If you want a "proto/basic writing" capability, model it as a separate lower-tech mechanism:
  "Accounting tokens / mnemonic notation" distinct from "Writing".

### 1.5 Metallurgy: early copper use exists; widespread metal economies are later
Around the 5th millennium BCE (and earlier in some regions):
- "Copper Age" / Chalcolithic technologies exist in parts of Eurasia, typically as sporadic copper use for
  ornaments/tools; stone tools still dominate subsistence production.
- Evidence for early copper smelting is documented in parts of southeast Europe around 5000 BCE.

Sources:
- Britannica (Copper Age framing, localized 5th millennium BCE copper-using cultures): https://www.britannica.com/topic/history-of-Europe/General-characteristics
- UCL project summary (Belovode early copper smelting, ~5000 BC): https://www.ucl.ac.uk/archaeology/research/directory/rise-metallurgy-eurasia
- Radivojević & colleagues review on early Balkan metallurgy (discusses 6200–5000 BC window): https://link.springer.com/article/10.1007/s10963-021-09155-7

Practical takeaway:
- "Bronze Working" and "Iron Working" should not be baseline in 5000 BCE presets.
- A better split is "Native copper + cold-working" vs "Smelting" vs "Alloying (bronze)" vs "Iron".

### 1.6 Oceania and independent agriculture in New Guinea highlands
Independent agriculture with wetland transformation is evidenced in New Guinea's highlands.

Source:
- UNESCO Kuk Early Agricultural Site (independent agricultural development; wetland drainage and cultivation over millennia):
  https://whc.unesco.org/en/list/887/

Practical takeaway:
- Oceania should not be modeled as uniformly "sailing + seaborne trade network" at 5000 BCE.
- But some regions (e.g., New Guinea highlands) plausibly have early cultivation without states/writing.

## 2) Regional Snapshots (what "feels right" in 5000 BCE)

This section is NOT asking the sim to mirror real history. It provides sanity constraints.

### West Asia (Fertile Crescent / Mesopotamia / Anatolia corridor)
- Strong early agriculture foundation; large villages; increasing craft specialization.
- Accounting precursors plausible (tokens/tallies), but not full writing.
- Some copper use/smelting possible (varies by sub-region), but bronze is not.

Sources:
- Ubaid period overview (Met): https://www.metmuseum.org/toah/hd/ubai/hd_ubai.htm
- Tokens -> writing progression (Met / Schmandt-Besserat): links above.

### South Asia (Mehrgarh and surrounding early farming zones)
- Early farming in the region predates 5000 BCE, with long sequences of Neolithic settlement.

Source:
- Mehrgarh paper (Nature): https://www.nature.com/articles/nature02586

### East Asia (Neolithic cultures; millet/rice trajectories)
- Farming villages exist; complex pottery traditions; regional diversity.

Source:
- Britannica (Yangshao culture context within Neolithic China): https://www.britannica.com/topic/Yangshao-culture

### Europe (Neolithic; local Chalcolithic pockets later)
- Wide spread of agriculture in many regions, but highly varied density and institutional complexity.
- Copper use begins in localized ways; not a continent-wide metal economy.

Sources:
- Britannica Copper Age framing: https://www.britannica.com/topic/history-of-Europe/General-characteristics
- Early Balkan metallurgy review: https://link.springer.com/article/10.1007/s10963-021-09155-7

### Africa (strong heterogeneity; Nile corridor special-case)
- Some zones support dense settlement; many zones remain low-density pastoral/foraging.
- Avoid "one Africa model".

### The Americas (domestication exists; "state tech" does not)
Agriculture/domestication processes exist far earlier than 5000 BCE in parts of Mesoamerica, but 5000 BCE
should still look like:
- Very limited urbanization and institutional capacity compared to Old World core farming basins.
- No writing, no metallurgy-driven economies, and weaker long-range trade intensity.

Sources (maize domestication is early-Holocene; indicates that 5000 BCE can include cultivation without states):
- Piperno et al. 2009 (maize present by ~8,700 cal BP): Smithsonian-hosted PNAS PDF/record:
  https://repository.si.edu/handle/10088/14850
- Ranere et al. 2009 (context and radiocarbon, Xihuatoxtla Shelter): https://repository.si.edu/handle/10088/14853

### Oceania (New Guinea highlands cultivation; no early "maritime empires")
- Independent agriculture plausible in New Guinea highlands (Kuk).
- Most Pacific island colonization is later; don't force early oceanic shipping networks.

Source:
- UNESCO Kuk: https://whc.unesco.org/en/list/887/

## 3) What This Implies for WorldSimulation Parameters (no edits yet)

This maps "facts" into recommended constraints for `data/sim_config.toml`.

### 3.1 Scenario framing
- `world.startYear` should default to `-5000` for the "Earth-like start" mode.
  - Current default is `-20000` (`data/sim_config.toml:3`), which is correct for a deep-start mode but
    causes accidental warmup dynamics and wrong checkpoint weighting for 5000 BCE tuning.
- `world.endYear` can remain runtime-configurable, but the "full realism gate" run should include 2025 CE
  per FineTuning.MD addendum.

### 3.2 Population range
Given OWID/HYDE's world total ~19.2M at -5000, a reasonable *Earth-like* baseline tuning band is:
- `world.population.mode = "range"`
- `world.population.minValue` and `maxValue` bracket "tens of millions", e.g. ~12M..30M.

Current config is `5M..20M` (`data/sim_config.toml:12`-`13`), which is close but may be too low on the upper end.

### 3.3 Spawn-region world shares (distribution constraint)
The start should concentrate population in a few highly suitable basins and spread the remainder thinly.
If spawn regions are used as a proxy for that:
- Reduce any region shares that imply "everyone starts in one continent" unless supported by your chosen baseline.
- Make shares consistent with whichever baseline dataset you choose (HYDE/OWID, or a curated archaeology prior).

Recommended process:
1) Choose a baseline source for region shares (HYDE/OWID continents; or a curated "core basins" map).
2) Translate to your sim's region taxonomy (west_asia, nile_ne_africa, etc.).
3) Rebalance `spawn.regions[*].worldShare` so the mapping is internally consistent.

### 3.4 Start-tech presets: remove anachronisms; add "precursor" techs
Current default 5000 BCE start-tech presets include `Writing (11)` and `Mathematics (14)` in multiple regions
(`src/simulation_context.cpp:483`), which is not plausible for 5000 BCE.

Guidelines for a 5000 BCE preset:
- Prefer "subsistence and craft primitives" (pottery, basic agriculture/husbandry, archery, basic mining).
- Model "administrative capacity precursors" as distinct from full writing:
  - Use existing tech `Counting Tokens and Tallies (115)` as the "proto-administration" baseline, not `Writing (11)`.
- Keep `Irrigation (10)` restricted (irrigation systems exist in some basins but are not globally default).
- Avoid `Bronze Working (9)` as a default. If you want early copper, split the tech (see Section 4).

Concrete `data/sim_config.toml` knobs involved:
- `startTech.triggerYear`
- `startTech.requireExactYear`
- `startTech.presets[]` (regionKey -> techIds)

### 3.5 Disease/plague scheduling: remove periodic world plagues
If realism is the objective, "Great Plague every ~600-700 years" is an anachronistic rail and should be replaced by:
- spillover-driven outbreaks (zoonoses + density + network connectivity),
- with epidemic frequency emerging endogenously from conditions.

This is mostly a code/mechanism change (see Section 5), but any available config knobs should:
- avoid hard periodic outbreak triggers,
- keep early disease burden mostly endemic/local with occasional shocks, not global synchronized cycles.

Related config knobs:
- `disease.*` parameters (endemic, zoonotic, spilloverShock*)
- `disease.tradeImportWeight` (network-driven spread)

### 3.6 Trade intensity (do not pre-load modern-style integration)
By 5000 BCE there are trade/exchange networks, but they should not behave like modern integrated markets.
The sim should start with:
- low long-range shipping route prevalence,
- high transport friction,
- and slow diffusion unless there is strong adjacency/contact.

Related config knobs:
- `economy.tradeIntensityScale`
- `economy.tradeIntensityMemory`
- `tech.diffusionBase`, `tech.culturalFrictionStrength`

### 3.7 War goals (early conflict is mostly localized/raiding/tribute)
Early polities should tend toward:
- raids, local border conflict, tribute extraction,
- not frequent annihilation wars producing total population removal.

Related config knobs:
- `war.objective*Weight`
- `war.earlyAnnihilationBias`
- `war.cooldown*`

## 4) Suggested Tech-Tree Splits (design notes)

This section answers: "Math is too broad" and "proto writing exists".
These are NOT parameter changes; they require tech-tree refactors (code + schema/presets).

### 4.1 Writing should be split into 2-3 stages
Why:
- Precursors (tokens/tallies) exist millennia before full writing systems.
- "Writing" as a single gate conflates record-keeping with full literacy/bureaucracy.

Suggested stages:
1) `Accounting Tokens and Tallies` (already exists as TechId 115)
2) `Proto-writing / Administrative Notation` (new): enables limited state accounting and contracts.
3) `Writing` (existing TechId 11): full script systems, schools, archives; much harder and later.

### 4.2 Mathematics should be split into "numeracy/measurement" vs "formal math"
Why:
- Early societies widely had counting, measurement, calendars, and practical geometry long before
  advanced formal mathematics.

Suggested stages:
1) `Numeracy and Measurement` (new): counting systems, basic arithmetic, surveying, weights/measures.
2) `Formal Mathematics` (existing TechId 14 renamed/split): more abstract math, proof traditions, etc.
3) Optional late-game splits: `Calculus`, `Statistics`, `Computational Math` (if needed for modern tech pacing).

### 4.3 Metallurgy should be split (native copper -> smelting -> alloying)
Why:
- Sporadic copper ornaments/tools and early smelting do not imply a bronze economy.

Suggested stages:
1) `Native Copper Working` (new): cold-hammering, annealing, simple ornaments/tools.
2) `Copper Smelting` (new): extractive metallurgy.
3) `Bronze Alloying` (existing TechId 9 re-scoped): requires tin access/trade and smelting.
4) `Iron Working` remains later.

## 5) Model Risks (things that will look "historically wrong" if not gated)

If you start at 5000 BCE and allow these to appear too early, the run will feel anachronistic:
- Democracy/republic labels and regime types emerging from generic control/legitimacy thresholds
  rather than era/capability gates.
- Full writing and broad mathematics granted at start.
- Periodic "great plagues" on a fixed interval independent of ecology/network pressure.
- High-frequency global trade and rapid tech diffusion without transport/communication constraints.
- High annihilation war rates in very-low-capability contexts.

## 6) Actionable Next Steps (still no edits)

1) Decide whether 5000 BCE mode should become the default `data/sim_config.toml` start year,
   or whether we keep separate configs: `sim_config_5000_bce.toml` and `sim_config_deep_start.toml`.
2) Rewrite `startTech.presets` for 5000 BCE using the "precursor" principle:
   - remove `Writing (11)` and `Mathematics (14)` from presets,
   - add `Counting Tokens and Tallies (115)` where appropriate,
   - restrict metallurgy techs to early copper (if modeled) and remove bronze.
3) Queue tech-tree splits for Writing/Math/Metallurgy so presets can be truthful without nerfing
   progress later in the run.

## 7) Stability and Legitimacy for 5000 BCE (and 20000 BCE)

Your observation is correct: in very early societies, "stability" and "legitimacy" should not be tightly coupled
to a modern fiscal ledger ("gold"), because:
- many polities are not tax states,
- wealth is stored in land access, livestock, grain, kinship obligations, and prestige,
- the dominant failure modes are ecological shocks, fission/fusion, and local autonomy, not bond markets.

This section proposes definitions and a capability-gated mechanic that remains valid from 20000 BCE through modernity.

### 7.1 Definitions (what the variables should mean)

Stability (regime continuity / order):
- Probability that the polity remains coherent as a single political unit (no fragmentation, collapse, or persistent civil war),
  and can enforce basic internal order.

Legitimacy (acceptance / compliance):
- The degree to which governed people (and key elites) view the rulers as having a right to rule, producing compliance
  without needing constant coercion.

These are related but not identical. A polity can be:
- high stability, low legitimacy (coercive endurance),
- low stability, high local legitimacy (loose confederations that fracture easily),
- high on both (robust states),
- low on both (collapse).

Reference framing for legitimacy as a political concept:
- Stanford Encyclopedia of Philosophy: https://plato.stanford.edu/entries/legitimacy/

### 7.2 Capability Regimes (why one formula is wrong across eras)

The simulation spans regimes with different “dominant constraint sets”:
- 20000 BCE (bands / small-scale foragers):
  - Stability is primarily group cohesion and fission/fusion dynamics.
  - Legitimacy is primarily prestige, reciprocity, and norm compliance.
  - Material scarcity matters, but there is little “fiscal state” mediation.
- 5000 BCE (tribes, chiefdoms, early complex settlements):
  - Stability depends on surplus buffering (food storage), local autonomy, factionalism, and violence capacity.
  - Legitimacy often derives from tradition, ritual authority, lineage, and redistribution.
- Later state regimes (bureaucratic states, empires, modern states):
  - Stability and legitimacy become more directly mediated by fiscal capacity (tax extraction, service provision),
    institutional capacity, and macroeconomy.

Useful high-level reference on differences between non-state and state political forms:
- Britannica on chiefdoms: https://www.britannica.com/topic/chiefdom
- Britannica on the state (as a sovereign political entity): https://www.britannica.com/topic/state-sovereign-political-entity

### 7.3 A Mechanism That Works Across 20000 BCE -> 2025 CE: Capability-Gated Fiscal Coupling

Core idea:
- Keep "material performance" as a driver at all eras (food security, disease burden, war losses).
- Only let *fiscal ledger state* (treasury/gold, debt, tax rate) strongly affect stability/legitimacy when the polity has
  sufficient state capacity (admin + control + institutional capacity).

This avoids anachronism:
- 5000 BCE polities can be stable/legitimate via kinship/ritual/redistribution even with minimal treasury.
- Modern polities’ stability/legitimacy is heavily mediated by fiscal solvency and service provision.

Concrete implementation sketch (conceptual, not code):
- Let `capability` be a 0..1 index derived from observed state capacity:
  - proxy candidates already in the simulation: `adminCapacity`, `avgControl`, `institutionCapacity`, `marketAccess`, `urbanShare`.
- Define a smooth gate `g = sigmoid((capability - cap_mid) / cap_width)`.
  - For very low capability: `g ~ 0` (fiscal coupling weak).
  - For high capability: `g ~ 1` (fiscal coupling strong).
- Then combine:
  - Stability update = (non-fiscal drivers) + g * (fiscal drivers)
  - Legitimacy update = (norm/performance drivers) + g * (fiscal drivers)

### 7.4 What Early-Era Drivers Should Dominate (5000 BCE focus)

Non-fiscal stability drivers (should matter strongly early):
- Food security volatility and storage/spoilage (buffering capacity): repeated shortages increase fragmentation risk.
- Local autonomy / control loss (distance, terrain, weak admin reach): low control increases instability.
- Elite faction pressure: inequality + low legitimacy increases coup/civil-war likelihood.
- War exhaustion and demographic loss (but avoid making early wars look like modern total war).
- Exit option (migration / fission): when land is available, dissatisfied groups can leave, reducing violence but fragmenting polities.

Non-fiscal legitimacy drivers (should matter strongly early):
- Redistribution performance under shock (can you feed people in bad years).
- Ritual/tradition proxy (can be approximated by cultural cohesion / low trait distance internally).
- Procedural “fairness” proxy (low extraction when capability is low, i.e., don't model high tax compliance as default).
- Coercion is not legitimacy: coercion may increase stability but should not increase legitimacy.

Practical note:
- If you must keep a single stored scalar like `gold`, interpret early-era "treasury" more like "stored surplus" (grain/livestock)
  and keep it small and local; do not treat it as a convertible currency budget in 5000 BCE without Currency/Markets state capacity.

### 7.5 20000 BCE Compatibility

For deep-starts, most polities are small and mobile:
- Stability: cohesion and fission/fusion. It should be easy to split, hard to maintain large territories, and hard to accumulate
  durable high control.
- Legitimacy: prestige norms and reciprocity. A group can be "legitimate" without state capacity, but legitimacy is local and fragile.

The same capability-gated approach works:
- capability stays near 0 for most groups, so fiscal coupling stays off.
- stability/legitimacy respond mostly to food variance, migration pressure, disease, and local conflict dynamics.

### 7.6 Implications for Tuning and Evaluation

If the evaluator currently rewards "fiscal health -> stability/legitimacy" too strongly in the early era, it will push the optimizer
toward anachronistic outcomes (e.g., early societies acting like modern states with strong money-mediated legitimacy).

The simplest realism requirement:
- Introduce separate "early-era" sensitivity bands (or capability-gated blending) so 5000 BCE polities can be stable
  for reasons other than gold balances.

