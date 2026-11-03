# Local repository cleanup — 2026-11-03

No scientific findings, tracked historical experiment bytes, experiment source hashes or
historical manifests were changed. No remote setting/ref, push or publication was performed.

## Inspected and preserved

Starting branch: research/selective-recovery-corrected at
013a2381b78ee6a385fd8666eff0ce08efd363d3. Before: 12,920 tracked files,
0 staged changes, 1 unstaged OS metadata file and 1,981 untracked audit entries.
No running solver/build/experiment process was found. One worktree, no stashes,
no submodules; four clean ignored nested candidate repositories were separately bundled.

Verified external backup: `/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818`.
The complete snapshot has 19,083 entries and 3,154,365,676 bytes of regular-file content,
including ignored results, local builds, root Git state and nested repositories. All copied
bytes/symlinks and all original refs were checked. Binary-capable index/worktree patches,
ref maps, manifests and restoration instructions are included. See [LOCAL_ARCHIVE.md](LOCAL_ARCHIVE.md).

## Classification and disposition

- A: source, tests, scripts, configs, dependencies, licences, fixtures and required docs retained.
- B: all tracked research evidence retained in place, including negative results and all final
  deliverables. Added 347 original audit evidence files at their original paths, byte-preserved.
  This includes probe binaries/objects and one historical Python cache referenced by preservation
  checks; they are deliberate evidence exceptions, not current build output.
- C: six audit build directories archived externally: 1,629 regular files plus 6 symlinks,
  478,576,463 regular-file bytes. Their original log/compiler evidence stays local. No other build
  directories were removed; current corrected binaries still support package verification.
- D: removed the tracked root .DS_Store after backing it up; existing ignore prevents recurrence.
- E: ignored candidate repositories, raw/intermediate results and distribution artifacts were
  fully backed up and retained; no assumption that their contents are disposable.

Added only scoped build/environment/editor ignore rules, with an explicit exception for the
historical cache dependency. No substantive uncommitted source existed. All 12,916 original
tracked files other than README, .gitignore, .gitattributes and .DS_Store were checked unchanged.

## Branch disposition

Four branches have no unique commits relative to the corrected branch and are archived:

| Former local branch | Exact tip | Verified archive tag |
|---|---|---|
| research/partial-acceptance | b4d9f9ef198cb2af1a14f484b0dab1099f1f63aa | archive/20261103_083818/research/partial-acceptance |
| research/partial-prefix-recovery | c00b1984957971d6c4b76407fbc8439a0a37a8bb | archive/20261103_083818/research/partial-prefix-recovery |
| research/selective-recovery | fb6ba907e4ccba9ec882537c2dce2be970c91781 | archive/20261103_083818/research/selective-recovery |
| test | 578a3a427650dbb5537c37d1d65664d4fc175470 | archive/20261103_083818/test |

Their exact tips also exist in the pre-cleanup bundle. `branch -d` initially refused an
upstream-ahead branch despite its containment in HEAD; explicit local deletion followed only
after ancestry, tag and bundle checks. Remote branches were not touched.

Retained research/preemption-aware-ordering at b9f1ce8821d15f958307ddae9244e8bfc8ba0e0c:
it has one independent unmerged agent-ordering implementation, tests and development results.
Its claims were not adopted into the corrected project. main had no unique commits and is
fast-forwarded to the cleanup commit. The working branch remains research/selective-recovery-corrected.

## Verification

- Root/nested bundle verification and every original ref checked; complete snapshot hashes checked.
- Original audit ZIP readability checked and archived build bytes checked after the move.
- Existing document/package verifier passes, including 5,354 preserved historical/audit hashes,
  all six complete matched experiments, manifests, raw statistical reconstruction and source identity.
- Historical project_files.sha256 verified against commit 013a238, without changing that manifest.
- README two-agent example ran; independent validator reports VALID, authoritative cost 5.
- Release CTest: 13/13, 2.12 seconds. No long benchmark rerun and no new sanitizer claim.
- Source/document diff checks passed; raw audit logs retain their original whitespace.
- Remote settings verified unchanged; archive tags and staged audit bytes checked.

After cleanup, expected index size is 13,269 tracked files; the added files are necessary audit
evidence and navigation records. Final status/counts and final branch IDs are recorded in the
external backup after commit. A clean working tree means no pending changes, not fewer historical
results. GitHub Desktop may still list remote branches; those were intentionally preserved.
