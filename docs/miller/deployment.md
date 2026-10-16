# Deployment and recovery

The expanded completion goal does **not** authorize replacing the installed host file manager. These commands are for a separately authorized deployment after release acceptance. Unit tests use mocked commands and temporary state; real disposable transactions remain required by C15.

## Phases

1. Validate main/matching extension and optional installed devel RPMs, package digests and native architecture. Refuse missing/corrupt original state, mismatched original split versions, and prepared/unknown DNF offline state.
2. Hold the per-user deployment lock. Preserve the original Fedora NEVRAs, folder preference and directory handler. A fresh installation requires a defined original handler for verifiable rollback; it refuses an empty handler rather than inventing one. Adopt legacy saved state only after validating its contents.
3. Validate cached stock identities, digests, signatures and SHA-256 checksums. Require valid digests and signatures with an isolated RPM keyring containing only `/etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-<host-release>-primary`; refuse a missing key. This overrides a host verification policy that permits unsigned packages and does not trust unrelated third-party RPM keys. Download only missing artifacts during installation; a failed/interrupted download cannot reach drain or transaction. Restore never silently downloads or substitutes another package version.
4. Request sudo credentials, then use normal Nautilus Quit and wait up to 25 seconds for held operations. Never use CLI --quit/force-kill. A timeout stops before RPM changes.
5. Recheck offline state and artifact hashes immediately before the transaction. DNF presents its normal confirmation unless --yes was explicitly provided. Concurrent package managers remain a host concern; do not run overlapping administrative transactions.
6. Set compatible user preferences, launch, and verify the running executable inode belongs to the installed binary, all split NEVRAs match, RPM verification passes, both canonical D-Bus services share that process, and handler/view match. Only then write success.

Restore sets a stock-compatible folder enum before downgrading its schema. It restores only the recorded folder-view/default-handler settings. Original snapshot and checksums remain; extra rollback data for an optional currently installed devel package does not replace original history.

## Commands

Run as the normal desktop user from this checkout:

```sh
./tools/miller/build-rpm.sh
./tools/miller/install-default.sh build-rpm/RPMS/x86_64/nautilus-<reviewed-version-release>.x86_64.rpm
./tools/miller/restore-stock.sh
```

Both wrappers accept `--yes` for an already reviewed transaction and `--allow-offline-reset` only after explicit authorization to discard the queued offline operation. Do not freeze security updates to keep the fork: review and rebase, increment the release, rebuild and repeat acceptance after upstream changes.

## Failure recovery

Read `~/.local/state/nautilus-miller/deployment.json` (or the selected XDG state root). It records operation, attempt identity, exact phase and error. Original `before-install.json`, stock RPMs and checksum manifest remain available.

- **Preflight/rollback/authorize/drain:** no package transaction has run. Fix inputs/cache/credentials or finish active file work; reopen the existing Nautilus if it was already drained. A corrupt cache must be replaced with the exact recorded, signed Fedora artifact; do not delete the manifest to hide a checksum mismatch.
- **Transaction:** DNF is authoritative for its result. Inspect installed NEVRAs and package integrity before retrying. Original cached packages support an explicit stock restore. Never assume a failed transaction made no changes.
- **Settings/launch/verify:** packages may already be replaced. No new success is recorded. Finish/retry activation or run the explicit stock restore with its saved artifacts. Previous successful metadata is `last-success.json`, not proof that this attempt succeeded.
- **Restore failure:** the original snapshot remains. A failure before downgrade may have restored a stock-compatible folder preference while the Miller binary remains installed; this is intentional and usable. Inspect the recorded phase, correct the failure and retry restore. Missing saved artifacts are an error, never permission to downgrade to an unrelated available version.

The filesystem/package operation is not globally atomic with user preference writes. The phase journal makes interruption recoverable; actual power-loss/reboot and injected-failure behavior still needs the disposable-system acceptance gate.
