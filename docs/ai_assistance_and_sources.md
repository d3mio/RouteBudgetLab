# AI assistance and sources — corrected package

## Assistance actually provided

The OpenAI coding assistant inspected the repository and preserved independent audit,
implemented substantial C++ and Python corrections, authored regression/property tests,
ran local builds/tests/experiments, analyzed raw output, and drafted/generated technical
documents and presentation tables. Assistance was not limited to brainstorming. The
assistant also made local Git commits authorized by the user. No subagents were used.

The first sanitizer rerun had a test failure, retained in the logs; its machine-dependent
latency assertion was corrected. Earlier AI-authored documents contained unsupported or
incorrect claims identified by the independent audit. This correction does not retroactively
certify those documents or historical executable provenance. Human academic review,
authorship responsibility and institutional requirements remain separate. Nothing here
claims formal academic approval, submission, publication, novelty score or a guarantee.

## Starting evidence and reused implementation

- Independent repository audit: audit_20261101_verification_1602/, retained byte-for-byte.
- Audited completion commit: fb6ba907e4ccba9ec882537c2dce2be970c91781.
- Vendored anytime-mapf tree: 47f49b0dd95165c1f84ed2a857f47744bf016ff7 at the audited commit;
  verify_upstream.sh identifies its pinned upstream prefix 47bb25e9 and checks file hashes.
  The vendor README credits MAPF-LNS2, BALANCE and reused rule-based solvers. No vendor
  source was changed for this repair. Retain vendor licenses and attribution.
- Vendored Eigen 3.4: tree dc380f209d58ee1b72049374e3990ba3863a9728, at third_party/eigen/.
- Actual Boost/compiler/platform identities and library hashes are in experiment provenance;
  historical identities are not inferred from today's environment.
- Corrected paired analysis uses Python standard-library exact arithmetic/distributions.
  Existing unrelated historical analysis scripts may still use NumPy/SciPy.

## Primary sources consulted for context

- Li, Chen, Harabor, Stuckey and Koenig (2021), *Anytime Multi-Agent Path Finding via
  Large Neighborhood Search*: [IJCAI proceedings](https://ijcai.org/proceedings/2021/568).
  Establishes the existing anytime subgroup-replanning LNS context.
- Li et al. (2022), *MAPF-LNS2: Fast Repairing for Multi-Agent Path Finding via Large
  Neighborhood Search*: [AAAI article record](https://ojs.aaai.org/index.php/AAAI/article/view/21266).
  The indexed publisher abstract was available; direct article fetch returned access denied.
  This correction does not claim a full-paper review or exhaustive novelty search.
- [Moving AI MAPF benchmark collection](https://movingai.com/benchmarks/mapf/index.html).
  Existing repository fixtures were used; no new benchmark downloads or untouched
  confirmation instances were introduced.
- [Official scheduled-task documentation](https://learn.chatgpt.com/docs/automations)
  was consulted using the OpenAI Docs skill for the user's continuation request. No
  general scheduling tool was available in the session, so no 21:00 automation was registered.

Empirical claims come from preserved local outputs and generated analyses, not the sources
above. Source links provide context and attribution, not proof of this implementation's
correctness or a first-ever research contribution.
