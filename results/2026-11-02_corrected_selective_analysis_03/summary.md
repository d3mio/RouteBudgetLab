# Corrected selective recovery analysis

{
  "total_runs": 192,
  "matched_cells": 32,
  "distinct_instances": 8,
  "distinct_layouts": 4,
  "baseline": "RESUMPTION_NO_PA",
  "delta_definition": "policy deadline cost minus baseline deadline cost; negative is better",
  "inference": "Descriptive development evidence. Seeds, budgets and nested agent sets are correlated. Few independent layouts limit inference. Non-significance is not equivalence.",
  "multiple_comparison": "Holm adjustment across 5 predeclared policy comparisons, separately for sign and signed-rank tests."
}

| Policy | Pairs | W/T/L | Mean delta | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign p | Holm rank p |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 1/28/3 | 10.0625 | 80.5 | 75.0711 | 13.2708 | 0.625 | 0.625 | 1 | 1 |
| PA_EVICTION_ONLY | 32 | 2/29/1 | -10.125 | -108 | 44.8451 | 7.92756 | 1 | 0.5 | 1 | 1 |
| PA_TERMINAL_ONLY | 32 | 3/29/0 | -14 | -149.33333333333334 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 | 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 3/29/0 | -14 | -149.33333333333334 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 | 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 3/29/0 | -14.5312 | -155 | 50.2202 | 8.87775 | 0.25 | 0.25 | 1 | 1 |

All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.
See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.
