# Supervisor decision brief — corrected scope

RouteBudget Lab's primary contribution is constraint-validated resumption of interrupted
MAPF repairs. Selective recovery is an optional evaluated extension. The current
[technical report](selective_recovery_technical_report.md), generated from the final
machine-readable analysis, supersedes numerical and completion claims in the earlier brief.
Historical versions remain available at the audited Git commit.

The implementation uses an authoritative arrival-cost contract, checked accumulation,
private search reservations, staged solution publication, current-incumbent compatibility
checks and discrete spatio-temporal collision validation. Recovery uses a checked gain
bound and deterministic dependency/conflict-aware greedy subsets. Greedy is not globally
optimal. Cooperative deadlines do not provide hard real-time guarantees.

Keep RESUMPTION_NO_PA as the default. Do not interpret within-run recovered gain or
isolated differences also seen in the reserve control as proof of recovery superiority.
Report both primary resumption and optional recovery results, including losses, neutral
outcomes, opportunity scarcity and actual overruns. The preserved Phase 7 final plans
remain valid after authoritative rescoring, but historical timing/provenance limits remain.

No parallel workers, learned policy, new confirmation maps or manufactured benchmark
evictions are needed to complete this scope. Confirmation testing and additional controller
studies require a later frozen protocol. This brief makes no claim of degree classification,
publication, official submission, exhaustive verification or universal performance superiority.
See the final audit and assertion-level test matrix for executed gates and remaining limits.
