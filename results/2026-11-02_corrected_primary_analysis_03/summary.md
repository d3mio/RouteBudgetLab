# Corrected selective recovery analysis

{
  "total_runs": 64,
  "matched_cells": 32,
  "distinct_instances": 8,
  "distinct_layouts": 4,
  "baseline": "DISCARD_NO_PA",
  "delta_definition": "policy deadline cost minus baseline deadline cost; negative is better",
  "inference": "Descriptive development evidence. Seeds, budgets and nested agent sets are correlated. Few independent layouts limit inference. Non-significance is not equivalence.",
  "multiple_comparison": "Holm adjustment across 1 predeclared policy comparisons, separately for sign and signed-rank tests."
}

| Policy | Pairs | W/T/L | Mean delta | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign p | Holm rank p |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| RESUMPTION_NO_PA | 32 | 22/9/1 | -212.812 | -296.0869565217391 | 329.516 | 58.2508 | 5.72205e-06 | 4.76837e-07 | 5.72205e-06 | 4.76837e-07 |

All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.
See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.
