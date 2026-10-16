# Completion acceptance ledger

Full authority: [goal](finder-completion-goal.md). Historical results stay in [verification](verification.md); no historical pass closes an expanded requirement without new evidence. Status **open** means incomplete, not failed or unsupported. **Blocked** requires a concrete missing environment and is never a pass.

Development branch: `finder-completion`; baseline `8618e792713ae6b248c4ca7d740ae78ce9d3040b`. Worktree `.worktrees/finder-completion`. Installed package at start: `nautilus-50.4-1.miller1.fc44`; no production changes authorized by this goal.

| ID | Required scenario | Owning stage | Status | Revision / procedure / evidence |
|---|---|---|---|---|
| C01 | Late branch replacement | Stage 1/2 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C02 | Paste/create in focused parent | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C03 | Empty-column destination and Left | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C04 | Captured context menu/dialog destination | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C05 | Real clipboard, conflicts, undo/redo | Stage 4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C06 | Nested spring-loaded drag/cancel | Stage 4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C07 | Safe stale-proof inline preview | Stage 3 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C08 | External preview both profiles/restart | Stage 1/3 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C09 | Layout, history, per-tab state | Stage 2 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C10 | Ancestor mutation, remote disconnect | Stage 5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C11 | Special views and return state | Stage 2/5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C12 | External reveal last row | Stage 1/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C13 | Accessibility, keyboard, RTL/scaling | Stage 2/5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C14 | Large/deep/lifetime benchmarks | Stage 5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C15 | Disposable install/update/restore failures | Stage 1/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C16 | Grid/List, extensions, portal | Stage 0/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |

## Evidence requirements

Each result records source revision, named test or reproducible manual steps, session/backend, pass/fail/blocked status, log/artifact path and remaining limitation. Before/after logs accompany defect fixes. Real clipboard/drag needs compositor-supported input; signal emission and direct transfer calls do not satisfy it. Real SFTP/SMB must contact disposable servers and cover authentication cancellation, delays, disconnect/reconnect and unavailable monitoring. Packaging tests must include real disposable transactions, not only mocks. CI requires an observed run, not only a YAML file.

## Baseline environment discovery

Fedora 44 / GNOME Wayland; GTK 4.22.5, libadwaita 1.9.4, Meson 1.11.2, Sushi 50.0. Native Mutter and Podman are present. `/dev/kvm` exists. `/dev/uinput` is root-only; no ydotool/dotool, sshd or smbd executable found on PATH. These are harness discovery facts, not yet blockers: nested compositor/libei, user containers and downloadable disposable VM/service tools remain options. Never use the real desktop's files for fixtures.

## Fresh Stage 0 results — 2026-10-16

Source code matches baseline `8618e7927` (documentation/fixture commit `0b7fa22da`). Newly configured Miller build compiled successfully. In a clean GTK configuration on the host Wayland session, all 28/28 discovered Meson executables passed, including `test-portal-file-chooser` (1.22 s). Log: `/tmp/nautilus-finder-baseline-tests.log`; build: `/tmp/nautilus-finder-baseline-build.log`. This is a fresh passing observation, not a rewrite of the historical timeout and not yet the full C16 extension/portal release gate. Profile validator passed; existing deployment tests 3/3 and disposable fixture tests 4/4 passed. Fixture rejection tests failed before implementation.

Mutter supports headless virtual monitors; rootless Podman reports crun available. Real input, remote services and disposable package transactions are not yet exercised.

## Foundation correction evidence

- Preview discovery, `7f9df5449`: canonical-only provider failed in Miller before correction. Eight private-bus cases now pass: production/Miller/Devel canonical discovery, normal profile startup, private-provider preference, absent-then-appearing provider, restart with a new unique bus owner, and teardown during discovery. Logs `/tmp/nautilus-finder-previewer-red.log` and `/tmp/nautilus-finder-previewer-green.log`. This closes the discovery subproblem, not full C08 Space/focus acceptance.
- Action ownership, `65814773a`: forced unchanged-parent selection notification after Right reproduced action ownership returning to parent. After deduplicating observed selection sets, the regression and seven Miller integration cases pass. Comparison of selection sets is linear; snapshots are freed with the column. Full C02/C03/C04/C05 genuine input acceptance remains open. Log `/tmp/nautilus-finder-parent-rebroadcast-red.log`, `/tmp/nautilus-finder-focus-green.log`.
- Private session harness isolates D-Bus, Wayland and all XDG test state; absolute socket works inside Tracker sandbox. Two helper tests pass for isolation/cleanup and exit status. Full isolated Meson run: **29/29 passed**, including portal (1.14 s). Log `/tmp/nautilus-finder-isolated-suite.log`. The reveal test now requests a post-selection layout frame and also asserts selected-file identity; the old unchanged binary reproduced the timing failure. This records a harness timing correction, not a new product scrolling fix.

## Additional Stage 1 evidence

- Selector, `aeff11088`: accessible label was undefined and Grid tooltip said List before the fix. Regression now checks icon, tooltip, explicit accessible label and resulting List→Grid→Columns→List cycle; search skips unsupported Columns in the toggle while explicit Columns retains its preference. Miller tests use GTK's test accessibility backend in Meson. This does not substitute for real screen-reader C13 acceptance. Logs `/tmp/nautilus-finder-selector-{red,tooltip-red,green,suite}.log`.
- Context/destination, `df4512135`: existing per-FilesView action groups already bind menu actions correctly; retained without a duplicate group. Closing an obsolete menu could reclaim parent focus after child entry (red). Inactive managed views now dismiss old menus and restore focus only while still active. New tests exercise captured-menu action routing, root actions while an entry is focused, parent New Folder acceptance after child focus changes, empty-child New Folder, unchanged sibling content, inactive-tab root ownership, newer multiselection and discarded callbacks. Ten Miller cases plus existing files-view and path executables pass. Logs `/tmp/nautilus-finder-context-close.log` and `/tmp/nautilus-finder-actions-suite.log`. These C-level controller/dialog checks do not close genuine-input C05/C06. The transient empty non-rightmost ancestor case remains open for Stage 5 invalidation/backend testing.
- Deployment (this foundation): mocked ordering regression failed before correction (transaction occurred before drain). Thirty Python cases now pass, including package/architecture/Fedora mismatch, missing/corrupt state, drain refusal, download/transaction/launch/verification failures, immutable original snapshot, Miller update, cached rollback corruption, optional devel consistency, concurrent attempts, offline-state opt-in and race recheck, changed artifacts, and previous-success archival. Logs `/tmp/nautilus-finder-deploy-order-red.log`, `/tmp/nautilus-finder-deploy-green.log`. Read-only real stock RPM metadata, digest and Fedora signature validation passed. Real disposable transactions and release candidate acceptance remain open.

Production installation has not been changed by this completion work. The installed Miller 1 and its original rollback record remain separate from the development worktree.
