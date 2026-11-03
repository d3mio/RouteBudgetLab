#!/usr/bin/env python3
"""Generate current research documents from one assembled machine-readable analysis.
Run from repository root after both frozen experiments and their analyses finish.
"""
import argparse,csv,hashlib,json,statistics
from pathlib import Path

REPAIR=Path('results/repair_20261101_corrected_01')
SELECTIVE=Path('results/2026-11-02_corrected_selective_pilot_03')
PRIMARY=Path('results/2026-11-02_corrected_primary_pilot_03')
SA=Path('results/2026-11-02_corrected_selective_analysis_03/analysis.json')
PA=Path('results/2026-11-02_corrected_primary_analysis_03/analysis.json')

def read(p):return json.loads(p.read_text())
def num(v):return 'N/A' if v is None else f'{v:.6g}'
def table(headers,rows):return '\n'.join(['| '+' | '.join(headers)+' |','|'+'|'.join(['---']*len(headers))+'|']+['| '+' | '.join(map(str,row))+' |' for row in rows])
def comparisons(a):
    return table(['Policy versus '+a['metadata'].get('baseline','RESUMPTION_NO_PA'),'Pairs','Wins / ties / losses','Mean ΔSoC','Non-tie mean','Sample SD','SE','Exact sign p','Exact rank p','Holm sign / rank p'],
        [[p,c['n_pairs'],f"{c['wins']} / {c['ties']} / {c['losses']}",num(c['mean_delta_cost']),num(c['mean_non_ties']),num(c['sample_sd']),num(c['standard_error']),num(c['sign_two_sided']),num(c['wilcoxon_two_sided_exact']),num(c['sign_two_sided_holm'])+' / '+num(c['wilcoxon_two_sided_exact_holm'])] for p,c in a['comparisons'].items()])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--verify',action='store_true');args=ap.parse_args()
    if args.verify:
        expected=read(REPAIR/'document_tables.json')
        if expected.pop('_analysis_sha256') != hashlib.sha256((REPAIR/'final_analysis.json').read_bytes()).hexdigest():
            raise ValueError('Analysis changed since document generation')
        for file,tables in expected.items():
            content=Path(file).read_text()
            for t in tables:
                if t not in content:raise ValueError(f'Table mismatch in {file}')
        print('Generated document tables match machine-readable source');return
    selective=read(SA);primary=read(PA);historical=read(REPAIR/'historical_reanalysis/analysis.json');history=read(REPAIR/'primary_history_analysis.json')
    runs=read(SELECTIVE/'all_runs.json');pruns=read(PRIMARY/'all_runs.json')
    extra={}
    baseline={tuple(r[k] for k in ('map','scen','num_agents','time_budget_s','seed')):r for r in runs if r['experiment_policy']=='RESUMPTION_NO_PA'}
    for policy in sorted({r['experiment_policy'] for r in runs}):
        rr=[r for r in runs if r['experiment_policy']==policy];events=[e for r in rr for e in r['recovery_events']];outcomes={}
        for e in events:outcomes[e['outcome']]=outcomes.get(e['outcome'],0)+1
        diffs=[r['total_iterations']-baseline[tuple(r[k] for k in ('map','scen','num_agents','time_budget_s','seed'))]['total_iterations'] for r in rr]
        extra[policy]=dict(recovery_events=len(events),outcomes=outcomes,
            natural_capacity_evictions=sum(r['natural_capacity_evictions'] for r in rr),
            stagnation_discards=sum(r['stagnation_discards'] for r in rr),
            invalid_checkpoint_removals=sum(r['invalid_checkpoint_removals'] for r in rr),
            normal_checkpoint_consumptions=sum(r['normal_checkpoint_consumptions'] for r in rr),
            recovery_checkpoint_consumptions=sum(r['recovery_checkpoint_consumptions'] for r in rr),
            maximum_return_boundary_overshoot_s=max(r['return_timing']['return_boundary_overshoot_s'] for r in rr),
            recovered_gain=sum(e['verified_gain'] for e in events),
            recovery_wrapper_time_s=sum(r['pa_total_time_us']+r['eviction_total_time_us']+r['terminal_total_time_us'] for r in rr)/1e6,
            mean_iteration_difference=statistics.mean(diffs),
            stage_totals_us={k:sum(e[k] for e in events) for k in ('extraction_us','filter_us','graph_us','selection_us','validation_us','commit_us')},
            output_serialization_total_s=sum(r['output_timing']['serialization_and_output_s'] for r in rr),
            maximum_process_elapsed_s=max(r['process_return_elapsed_s'] for r in rr),
            late_commits=sum(e['outcome']=='COMMITTED' and not e['commit_on_time'] for e in events),
            deadline_return_cost_mismatches=sum(r['cost_at_deadline']!=r['final_cost'] for r in rr))
    package=dict(selective=selective,primary=primary,historical_selective=historical,historical_primary=history,
                 interim_repetitions={f'{kind}_{repeat}':read(Path(f'results/2026-11-02_corrected_{kind}_analysis_{repeat}/analysis.json')) for kind in ('selective','primary') for repeat in ('01','02')},
                 supplemental_metrics=extra,source_commits=dict(selective=runs[0]['provenance']['git_commit'],primary=pruns[0]['provenance']['git_commit']),
                 reserve=read(REPAIR/'reserve_decision.json'),default='RESUMPTION_NO_PA',
                 interpretation='Development evidence only. Recovery remains optional. No confirmation data accessed; no equivalence or universal superiority claim.')
    serialized=json.dumps(package,indent=2,allow_nan=False)+'\n'
    final=REPAIR/'final_analysis.json'
    if final.exists():
        if final.read_text()!=serialized: raise ValueError('Existing final analysis differs; refusing overwrite')
    else: final.write_text(serialized)
    comparison_rows=[]
    for dataset,a in [('corrected_selective',selective),('corrected_primary',primary),('historical_selective',historical)]:
        for policy,c in a['comparisons'].items():
            comparison_rows.append(dict(dataset=dataset,policy=policy,**{k:v for k,v in c.items() if not isinstance(v,dict)}))
    with (REPAIR/'final_comparisons.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(comparison_rows[0]));writer.writeheader();writer.writerows(comparison_rows)
    # All current numerical tables are derived below from this single assembled object.
    st=comparisons(package['selective']);pt=comparisons(package['primary']);ht=comparisons(package['historical_selective'])
    mt=table(['Policy','Capacity recovery attempts / commits','Terminal examined / commits','Recorded refinement overruns / runs','Maximum overshoot (ms)','Recovered gain','Recovery wrapper time (s)','Mean iteration Δ'],
        [[p,f"{m['eviction_attempts']} / {m['eviction_committed']}",f"{m['terminal_checkpoints_examined']} / {m['terminal_committed']}",f"{m['overrun_count']} / {m['n_runs']}",num(m['max_recorded_refinement_overshoot_s']*1000),extra[p]['recovered_gain'],num(extra[p]['recovery_wrapper_time_s']),num(extra[p]['mean_iteration_difference'])] for p,m in selective['metrics'].items()])
    primary_timing=table(['Primary policy','Refinement overruns / runs','Maximum overshoot (ms)'],[[p,f"{m['overrun_count']} / {m['n_runs']}",num(m['max_recorded_refinement_overshoot_s']*1000)] for p,m in primary['metrics'].items()])
    removal_table=table(['Policy','Capacity evictions','Stagnation','Invalid','Normal consumption','Recovery consumption','Max return-boundary overshoot (ms)'],[[p,m['natural_capacity_evictions'],m['stagnation_discards'],m['invalid_checkpoint_removals'],m['normal_checkpoint_consumptions'],m['recovery_checkpoint_consumptions'],num(m['maximum_return_boundary_overshoot_s']*1000)] for p,m in extra.items()])
    interim_table=table(['Preserved repetition','Comparison','Wins / ties / losses','Mean ΔSoC'],[[name,p,f"{c['wins']} / {c['ties']} / {c['losses']}",num(c['mean_delta_cost'])] for name,a in package['interim_repetitions'].items() for p,c in a['comparisons'].items()])
    hpt=table(['Historical comparison (final cost)','Pairs','Wins / ties / losses','Treatment − control mean'],[[label,v['summary']['n_pairs'],f"{v['summary']['wins']} / {v['summary']['ties']} / {v['summary']['losses']}",num(v['summary']['mean_delta_cost'])] for label,v in history['comparisons'].items()])
    all_ties=all(c['wins']==0 and c['losses']==0 for c in selective['comparisons'].values())
    wins=sum(c['wins'] for c in selective['comparisons'].values());losses=sum(c['losses'] for c in selective['comparisons'].values())
    decision=('All five selective/control comparisons tie resumption in every matched cell.' if all_ties else
              f'The five selective/control comparisons contain {wins} wins and {losses} losses in total; these correlated comparisons are not independent observations.')+' Recovery remains an optional evaluated extension; RESUMPTION_NO_PA remains the default.'
    if sum(m['eviction_committed']+m['terminal_committed'] for m in selective['metrics'].values())==0:
        decision += ' No eviction or terminal recovery committed a route; numerical wins in those policies cannot be attributed to recovered-route publication. The reserve control and timing variation must be considered.'
    repro='''```sh
cmake -S . -B build/corrected-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/corrected-release -j 4
ctest --test-dir build/corrected-release --output-on-failure
cmake -S . -B build/corrected-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build/corrected-asan -j 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/corrected-asan --output-on-failure
RBL_VALIDATOR=build/corrected-release/rbl_validate python3 tests/test_safe_validation.py
python3 tests/test_corrected_experiments.py
python3 scripts/run_selective_recovery_pilot.py --config configs/2026-11-02_corrected/pilot.json --build-dir build/corrected-release --output-dir results/reproduction_selective_01
python3 scripts/analyze_selective_recovery.py --input results/reproduction_selective_01/raw --output-dir results/reproduction_selective_analysis_01
python3 scripts/run_selective_recovery_pilot.py --primary --config configs/2026-11-02_corrected/primary.json --build-dir build/corrected-release --output-dir results/reproduction_primary_01
python3 scripts/analyze_selective_recovery.py --primary --input results/reproduction_primary_01/raw --output-dir results/reproduction_primary_analysis_01
(cd results/reproduction_selective_01 && shasum -a 256 -c manifest.sha256)
(cd results/reproduction_primary_01 && shasum -a 256 -c manifest.sha256)
python3 docs/run_offline_verification.py --output-dir results/reproduction_offline_01
python3 docs/render_corrected_package.py --verify
python3 docs/verify_corrected_package.py
```
Use a new output name for each reproduction; existing directories are deliberately rejected.
Leak checking is disabled here for the executed macOS arm64 sanitizer configuration; that is
not a blanket recommendation for supported Linux leak-sanitizer environments. The CMake
option instruments engine, vendored compilation and test executables; compiler warnings
are retained. No runtime sanitizer diagnostics were observed in the final executed tests.
'''
    contribution='RouteBudget Lab implements constraint-validated resumption of interrupted Multi-Agent Path Finding repairs and evaluates selective recovery of completed replacement routes under a shared global time budget. It combines fresh incumbent compatibility checks, a checked optimistic gain filter, dependency- and conflict-aware greedy subset selection, independent complete-plan validation and staged transactional publication. Controlled development experiments measure recovery against ordinary resumption after accounting for its work and displaced search.'
    report=f'''# Constraint-Validated Resumption of Interrupted Repairs for Deadline-Aware Multi-Agent Path Finding

Project: **RouteBudget Lab**. Corrected development package, 2026-11-02.
Numbers are generated from `results/repair_20261101_corrected_01/final_analysis.json`.

## 1. Research problem
Anytime MAPF must return a complete valid multi-agent plan when a planning budget ends.
A repair interrupted mid-neighborhood has useful completed routes but an unfinished
agent search. Discarding everything wastes work; publishing incomplete repairs is unsafe.
The objective is authoritative arrival Sum of Costs (SoC), not padded path length.

## 2. Research gap and scope
MAPF-LNS improves an initial solution through repeated subgroup replanning
([Li et al., IJCAI 2021](https://ijcai.org/proceedings/2021/568)). MAPF-LNS2 repairs
colliding initial routes through neighborhood search
([Li et al., AAAI 2022](https://ojs.aaai.org/index.php/AAAI/article/view/21266)).
The engineering question studied here is safe reuse of interrupted repair prefixes under
changing incumbents and short planning budgets. LNS, closure/min-cut and additive bounds
are established techniques. This package does not establish a first-ever combination,
a novelty score, or a publication guarantee. This is not an exhaustive prior-art review.

## 3. Research questions
1. Can completed-agent repair prefixes be validated against the current incumbent and resumed safely?
2. Does resumption outperform fresh-only interruption handling on exposed development instances?
3. Does recovery before natural eviction or near the deadline improve deadline SoC beyond resumption?
4. Where do recovery opportunities disappear, and what work does recovery displace?

## 4. Primary resumption method
Start from the same validated complete plan for all matched policies. Run sequential LNS
in bounded slices using Space–Time A*. Store completed prefix routes, neighborhood/order,
version context and restart information; discard the unfinished agent's search frontier.
Before reuse, validate neighborhood identity and compatibility with current trajectories.
Compatible completed routes rebuild reservations; an incompatible checkpoint is rejected.
The unfinished agent restarts. This is completed-prefix resumption, not arbitrary live
OPEN/CLOSED frontier persistence. The corrected primary controller resumes the oldest
compatible checkpoint or starts fresh. The estimator/scored adaptive controller remains
available and unit-tested, but is not active in these corrected pilots.

## 5. Selective-recovery extension
At an already-selected capacity victim, attempt recovery before overwrite—even when the
incoming checkpoint has zero completed routes. Failure preserves the incumbent and the
original save/eviction proceeds. No extra benchmark evictions are manufactured.
Terminal recovery scans at most K=2 eligible checkpoints in FIFO checkpoint-ID order.
Every remaining checkpoint is evaluated against the current incumbent after any commit.
Every-interruption filtered recovery is an ablation, not the recommended default.

## 6. Algorithms and pseudocode
For each eligible completed replacement, compute checked 64-bit
`w_i = arrival(P_i) - arrival(Q_i)` and `U = sum(max(w_i,0))` before graph work.
Every subset gain is at most U; consequently U=0 rules out strictly positive gain.
Malformed input/overflow is failure, not a zero-bound proof. Preserve negative candidates
when U>0 because positive replacements can require them.

```text
while before normal-search cutoff:
    checkpoint = oldest compatible suspended checkpoint, if any
    repair = resume checkpoint or start fresh neighborhood
    search using private reservations and bounded slice
    if complete: stage, independently validate, publish only strict improvement
    else: save completed-agent prefix; recover actual capacity victim before overwrite

recover(checkpoint, current incumbent, deadline):
    extract complete unique-agent replacements; recompute arrival costs
    if checked upper bound fails: reject
    if U == 0: stop before dependency/conflict construction
    build replacement-to-old dependencies and replacement-pair incompatibilities
    for positive candidates sorted by descending weight, then candidate index:
        closure = all transitive dependencies (cycles are permitted)
        reject missing/forbidden dependencies or internal/selected-route conflicts
        add closure only if checked marginal gain is positive
    independently validate the complete selected-plus-incumbent plan
    stage paths, full reservations, versions, authoritative cost and cache
    check expected version and deadline; publish by nonthrowing swaps
    if deadline crossed at publication: swap back; do not record improvement
```

Greedy is deterministic but not globally optimal. Dinic maximum-weight closure and its
nonnegative specialization are accepted only without pairwise incompatibilities. A small
exhaustive graph oracle includes both constraints; the path-mixture oracle independently
checks complete plans. Complete-route arrival cost, completed-agent prefix aggregate and
search-node g are distinct quantities.

## 7. Correctness and state invariants
For production accepted/recovered plans, stored SoC equals independently recomputed
arrival SoC. Arrival is one step after the last non-goal position; terminal padding does
not contribute. Initial installation, normal repair, resumption, external diagnostic
updates and recovery use this definition. Candidate SoC is published directly.
Validation caches bind both incumbent version and authoritative cost.
Production improving publications require a complete plan with valid starts/goals, traversable
cardinal/wait moves, no vertex collision, no opposite edge swap and persistent final-goal
occupancy: **discrete spatio-temporal collision validity**, not physical robot safety.

Preparation allocates private paths/reservations/versions. Publication uses nonthrowing
swaps and scalar writes in this single-threaded engine; it is not a concurrent atomic
operation. Failure injection covers preparation, prepublication and postpublication
rollback. Test-only trajectory/cost mutation APIs intentionally permit fault injection
and are not production acceptance paths. General allocator/system failure resilience
outside the publication boundary is not established by these tests.

## 8. Experimental methodology
Frozen protocol: `docs/corrected_experiment_protocol.md`. Selective pilot: 192 runs,
32 matched cells, two exposed maps × two scenarios × 50/100 agents × 1/5 seconds ×
seeds 0/1 × six policies. A separate primary comparison uses 64 runs over the same
32 cells with fresh-only and resumption. Shared initial plans are validated outside
refinement time. k=8, K=2 for resumption, 5 ms slices, 500-expansion cap.
The frozen terminal reserve is 13 ms, based on 60 expensive development recovery calls;
maximum observed call time was 6.042625 ms. This is a heuristic, not a guarantee.
The separate reserve control stops normal search at the same cutoff without recovery.
Policies execute sequentially in stored cyclic order. Thirty-two cells cannot perfectly
balance six first-position rotations. OS background activity is not eliminated.

All extraction, graph/selection, validation, staged publication, trigger bookkeeping and
recovery cleanup execute within measured refinement elapsed time. Actual successful
publication timestamps determine deadline quality; late publication is rolled back.
Final serialization/output duration, return-boundary time after local destruction, and
parent-observed process elapsed time are separate. The return-boundary timestamp excludes
writing its own timing sidecar and runtime exit; the parent measurement includes both.
The parent measurement includes initialization and process startup/teardown; it is not
interchangeable with refinement overshoot. Deadline checks are cooperative; allocations,
OS scheduling and cleanup can overrun. No hard real-time guarantee is claimed.

## 9. Baselines
The primary comparison is `DISCARD_NO_PA` (fresh-only K=0) versus `RESUMPTION_NO_PA`.
The extension compares resumption against every-interruption filtered, eviction-only,
terminal-only, combined eviction/terminal, and terminal-reserve-control policies.
Historical Phase 7 additionally includes scored/fixed/adaptive controller and estimator
ablations and BALANCE. Those original raw artifacts remain unchanged. They are not
mixed with corrected binary runs in any paired comparison.

Both earlier full repetitions (`_01`, `_02`) remain preserved, with separate analyses.
Final `_03` follows telemetry-only corrections documented in the frozen protocol; no
parameter or reserve tuning was performed in response to outcomes.

## 10. Corrected results
ΔSoC = policy deadline cost − baseline deadline cost; negative favors the policy.
Return-cost results and map/scenario/agent-scale/budget strata are in machine-readable
analysis. The following policy tables are generated without handwritten numerical edits.

### Primary resumption comparison

{pt}

{primary_timing}

### Selective extension

{st}

### Trigger, work and deadline measurements

{mt}

{removal_table}

Recovery wrapper time includes the policy's measured PA, eviction and terminal wrappers;
event stage sums omit some surrounding copying/bookkeeping. Full refinement timing
includes that work. Cumulative recovered gain is a decrease relative to each policy's own
intermediate incumbent, not its advantage over resumption. Iteration differences describe
changed search work; they do not establish a causal cost-per-displaced-iteration model.
Natural capacity evictions, stagnation discards, invalid removals, normal consumption
and recovery consumption are recorded separately. See `supplemental_metrics` for these counts and mutually exclusive outcomes.

## 11. Statistical limitations
Exact two-sided sign tests discard ties. Signed-rank tests discard zeros and enumerate
the random-sign distribution using doubled average ranks, including tied magnitudes.
Holm adjusts each test family over the five predeclared extension comparisons. All-tie
p=1 is a reporting convention with no informative signs/ranks, not evidence of equivalence.
Sample standard deviation and standard error are separately labeled. The 32 cells reuse
eight agent-count instances and four layouts; seeds/budgets and nested agent sets are
correlated. Few independent layouts limit inference, and these are development data.

## 12. Historical, negative and neutral findings
{decision}

Earlier complete corrected repetitions are retained as instrumentation-development
results; they are not pooled with the final repetition as independent samples.

{interim_table}

The original selective pilot is independently reanalyzed, not overwritten:

{ht}

The historical four nonzero deltas are +287, +2, +72 and +73; exact two-sided sign/rank
p=.125 and Holm-adjusted p=.625. Its four recorded refinement overruns contradict the
old all-192-zero claim. Original raw-JSON manifest hashes were stale because hashing
preceded enrichment. The original pilot had zero recorded capacity-recovery attempts,
23 terminal checkpoints examined and no terminal improvement; attempts are not a direct
capacity-eviction count. Historical timing does not establish a precise nanosecond latency
or a like-for-like percentage overhead reduction.

All 768 historical Phase 7 final plans were revalidated and re-scored with matching
arrival SoC. The following historical FINAL-cost comparisons use a corrected explicit
sign convention; the old script used control minus treatment. They do not establish
current deadline behavior after runtime-changing fixes.

{hpt}

## 13. Supported contribution
{contribution}
The supported result is an implemented and tested resumption mechanism plus an evaluated
recovery extension, with empirical outcomes limited to the configurations above.
No optimality, universal quality advantage, first-ever novelty or publication claim follows.

## 14. Threats to validity
Previously exposed layouts, small layout count, nested agent subsets, common initial-plan
construction, wall-clock jitter, instrumentation/logging overhead and changes in search
trajectories limit generalization and causal interpretation. The reserve stress cases
artificially delay valid paths to exercise costly recovery and are not benchmark wins.
Historical executable identities and some timing boundaries cannot be reconstructed.
Independent parsing/collision checks share the scalar arrival-cost contract; small oracle
and regression checks provide additional evidence, not a formal whole-program proof.
Coverage and deliberate test limits are listed in `requirements_to_tests_matrix.md`.
Natural interrupted-repair snapshot replay and independent path-mixture oracle checks
are recorded separately in `results/repair_20261101_corrected_01/offline_replay_01/`.
Those mechanism checks are not additional matched performance observations.

## 15. Future work
Use untouched confirmation instances only in a later authorized study with frozen code
and protocol. Reevaluate scored/adaptive controllers after runtime-changing publication
costs. Study naturally occurring checkpoint pressure without manufactured evictions.
Improve measurement of cleanup/allocator tails and test additional adversarial schedules.
No parallel worker or learned policy was added to seek a favorable recovery result.

## 16. Reproduction and AI-assistance declaration
{repro}

AI assistance in this correction included source inspection, implementation, test design,
execution of local tools, statistical analysis and drafting/generation of documentation.
The assistant generated substantial code and prose; these are not independently human
certified. The preserved independent audit is separate starting evidence. No document
was officially submitted, and no commit was pushed, merged, published or emailed.
See `ai_assistance_and_sources.md` and `repair_log.md` for scope and sources.
'''
    Path('docs/selective_recovery_technical_report.md').write_text(report)
    Path('docs/selective_recovery_analysis.md').write_text('# Corrected development analysis\n\n'+decision+'\n\n'+pt+'\n\n'+st+'\n\n'+mt+'\n\nPrimary outcome is deadline arrival SoC. Negative deltas favor the policy.\nSee [technical report](selective_recovery_technical_report.md) for timing boundaries,\ncorrelated-cell limitations, historical corrections and primary-method evidence.\nAll numbers derive from `results/repair_20261101_corrected_01/final_analysis.json`.\n')
    Path('docs/selective_recovery_presentation_outline.md').write_text(f'''# Presentation outline — RouteBudget Lab

1. Problem: complete valid MAPF plans under bounded planning time.
2. Primary method: completed-prefix checkpointing, fresh compatibility validation, safe resumption.
3. Contract: authoritative arrival SoC, complete-plan validation, staged publication and late rollback.
4. Extension: U bound, signed dependencies, replacement conflicts and deterministic greedy closures.
5. Trigger diagram: real FIFO victim before overwrite; bounded FIFO terminal recovery.
6. Methodology: exposed development layouts, matched shared initial plans, sequential cyclic order,
   frozen 13 ms heuristic reserve, control cutoff, all work charged to refinement.
7. Primary results (negative delta favors resumption over restart):

{pt}

8. Selective results (negative delta favors policy over resumption):

{st}

9. Recovery funnel and measured overshoot:

{mt}

10. Interpretation: {decision}
11. Limitations: four layouts, correlated cells, wall-clock jitter, no equivalence/novelty guarantee;
    saved-plan validation is distinct from historical publication-time evidence.
12. Reproduction, AI assistance, and future untouched confirmation work.

Tables are generated from the same final_analysis.json as the report. This outline has not been submitted.
''')
    Path('README.md').write_text(f'''# RouteBudget Lab

**Constraint-Validated Resumption of Interrupted Repairs for Deadline-Aware Multi-Agent Path Finding**

{contribution}

Default: `RESUMPTION_NO_PA`. {decision}

This corrected development package preserves the original Phase 7, partial-acceptance,
selective pilot and independent audit artifacts. It includes 192 corrected selective runs,
a separate 64-run primary resumption comparison, authoritative revalidation of 768
historical final plans, and executed Release/ASan/UBSan tests. It does not claim a hard
real-time guarantee, global greedy optimality, physical robot safety or publication novelty.

- [Technical report](docs/selective_recovery_technical_report.md)
- [Generated analysis](docs/selective_recovery_analysis.md)
- [Presentation outline](docs/selective_recovery_presentation_outline.md)
- [Frozen protocol](docs/corrected_experiment_protocol.md)
- [Assertion-level test matrix](docs/requirements_to_tests_matrix.md)
- [Repair log](docs/repair_log.md) and [final audit](docs/final_completion_audit.md)
- [Machine-readable final analysis](results/repair_20261101_corrected_01/final_analysis.json)
- Corrected raw data/manifests: `results/2026-11-02_corrected_selective_pilot_03/` and
  `results/2026-11-02_corrected_primary_pilot_03/`.
- Historical evidence: `results/phase7/`, `results/partial_acceptance_pilot/`,
  `results/2026-11-01_selective_recovery/`, `audit_20261101_verification_1602/`.

C++17, CMake, Boost and Python 3 are required. Eigen and the upstream MAPF sources are
vendored. Corrected experiment/analysis scripts use the Python standard library.

## Build, test and reproduce (repository root)

{repro}

The package verifier above checks the stored corrected runs and their original build identity;
a reproduction build may differ in binary hash despite matching source. It also verifies
the originally supplied untracked audit evidence, which must be retained locally.

The offline historical viewer remains available through `python3 viewer/server.py --port 8080`.
Its replays are historical saved trajectories, not live execution of the corrected solver.
Scheduling was not registered by this repair session; see repair_log.md for continuation notes.
''')
    Path('IMPLEMENTATION_STATUS.md').write_text(f'''# Implementation status — corrected development package

Primary contribution: constraint-validated checkpoint resumption. Optional extension:
selective recovery of completed routes. Default remains RESUMPTION_NO_PA.

## Executed verification

- Release: 13/13 CTest entries passed; ASan/UBSan: 13/13 passed after correction of a
  machine-dependent latency assertion. The original failed sanitizer log is preserved.
- 59 compilation units have sanitizer flags; linked sanitizer symbols verified.
- Intentional Release harness failure returns 17 under -DNDEBUG.
- Independent-validator Python tests: 6/6; experiment/statistics tools: 7/7.
- Historical Phase 7: 768/768 final plans valid with authoritative cost matches.
- Corrected selective/primary pilots: 192/64 runs respectively; every final plan independently
  validated, event counts reconciled, final manifests verified.
- Costs, staged reservations/versions/cache, conflicting closures, real eviction hooks,
  adverse terminal cascade and injected stage cancellation have direct assertions.

Test counts are not a claim of exhaustive correctness. See the assertion-level matrix and
final audit for coverage boundaries. General OS scheduling/allocator tails remain outside
hard real-time guarantees. The adaptive estimator/controller is retained and unit-tested;
its historical final-plan results remain historical, not corrected-current runtime evidence.

## Generated deadline-quality results

{pt}

{st}

{decision}

## Provenance and package

Starting audited commit fb6ba907e4ccba9ec882537c2dce2be970c91781 on research/selective-recovery.
Repair branch research/selective-recovery-corrected. Experiment source commit:
`{package['source_commits']['selective']}`. Each raw run records dirty status, source/build/input
hashes and complete compiler/linker commands. The pre-existing .DS_Store edit and two
untracked audit directories were preserved. No push, merge, submission or publishing occurred.

Current tables and explanations supersede the audited completion claims. Historical raw
artifacts remain unchanged; old document versions remain available at the audited Git commit.
''')
    doc_tables={
        'docs/selective_recovery_technical_report.md':[pt,primary_timing,st,mt,removal_table,interim_table,ht,hpt],
        'docs/selective_recovery_analysis.md':[pt,st,mt],
        'docs/selective_recovery_presentation_outline.md':[pt,st,mt],
        'IMPLEMENTATION_STATUS.md':[pt,st]}
    doc_tables['_analysis_sha256']=hashlib.sha256((REPAIR/'final_analysis.json').read_bytes()).hexdigest()
    (REPAIR/'document_tables.json').write_text(json.dumps(doc_tables,indent=2)+'\n')
    print('Rendered report, analysis, presentation, README and status from final_analysis.json')
if __name__=='__main__':main()
