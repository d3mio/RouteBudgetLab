# Verified pre-cleanup recovery backup

Location: `/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818`. `verification.json` confirms all snapshot bytes and Git refs.
The snapshot includes the complete working tree, ignored files, root `.git`, and four
nested candidate repositories. Bundles and binary patches are also stored separately.
No original research evidence was discarded.

## Restore the exact pre-cleanup repository to a NEW location

```sh
BACKUP='/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818'
RESTORED='/Users/disasgamage/Documents/RouteBudgetLab_restored_20261103'
test ! -e "$RESTORED" && cp -a "$BACKUP/snapshot" "$RESTORED"
git -C "$RESTORED" status --short
```
This preserves original branch refs, local Git configuration, staged/unstaged state,
untracked audits and ignored files. Do not copy over your active repository.

## Recover committed history without the working snapshot

```sh
git bundle verify '/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818/repository.bundle'
git clone '/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818/repository.bundle' /Users/disasgamage/Documents/RouteBudgetLab_bundle_restore_20261103
```
`refs.txt` maps every original ref to its exact ID. `staged.patch` and `unstaged.patch`
are binary-capable, but do not contain untracked files; those are in `snapshot/`.
Apply patches only to a matching clean checkout: `git apply --index staged.patch`, then
`git apply unstaged.patch` (skip zero-byte patches). The full snapshot is simpler here.
Nested repositories have their own bundles, ref lists, status and patches in `nested/`.

## Restore archived audit builds at their original paths

From the active repository root, each destination must be absent:

```sh
BACKUP='/Users/disasgamage/Documents/RouteBudgetLab_Backups/20261103_083818'
for relative in \
  audit_20261101_independent_1548/build-release \
  audit_20261101_independent_1548/build-option-check \
  audit_20261101_independent_1548/build-asan \
  audit_20261101_verification_1602/build-release \
  audit_20261101_verification_1602/build-documented-option \
  audit_20261101_verification_1602/build-asan
do
  test ! -e "$relative" || exit 1
  cp -a "$BACKUP/archived/$relative" "$relative"
done
```
Copied build caches embed their original paths/toolchain; rebuild if the environment
changes. Their original logs and compiler evidence remain in the repository.
`snapshot_manifest.json` provides SHA-256/size or symlink target for every copied entry.

## Restore an archived branch locally

Use `git branch ORIGINAL_NAME ARCHIVE_TAG`; this refuses to overwrite an existing branch.

`git branch research/partial-acceptance archive/20261103_083818/research/partial-acceptance`

`git branch research/partial-prefix-recovery archive/20261103_083818/research/partial-prefix-recovery`

`git branch research/selective-recovery archive/20261103_083818/research/selective-recovery`

`git branch test archive/20261103_083818/test`

The backup is local, not pushed or published. Retain it before reclaiming disk space.
