# Native drag-and-drop verification

Authority: [full goal](finder-completion-goal.md), [drag design](../superpowers/specs/2026-10-17-miller-dnd-design.md) and [plan](../superpowers/plans/2026-10-17-miller-dnd.md). Source base for Task1 `b66a52f7b30cbc9508a449a3d209a9ea50031a5d`; full DnD base `deb79d68a`. This component is in progress. Full Stage4, C06 and release acceptance remain open.

## Reused fixture and baseline

The existing operation fixture/helpers now live in test-only `test/automated/miller-test-operations.[ch]`, linked only to their consuming display executables. Native model selection, guarded real input, private Home checks, progress/undo completion, exact bytes/sentinels and retained child cleanup remain the existing implementations. Both main and normal-app child bind their already verified borrowed input. The original operation application/clipboard roles remain separate.

Fresh actual operation baseline63/63 passed before extraction (`/tmp/nautilus-finder-dnd-task1-operations-baseline.log`) and again after extraction (`...-task1-shared-fixture-operations.log`), with real deletion/Trash and batch child markers, no skips. The helper extraction changes no product API. A new native DnD executable increases the Meson executable total from39 to40.

## Actual drag input and fixture corrections

The new test uses compositor-delivered primary press, threshold motion, modifiers, release/Escape, actual GtkDragSource/GdkDrag callbacks and the real native file-list provider. It checks exact initiating file identity/count, actual negotiated action, finished/cancelled native jobs, literal file bytes, unchanged siblings, destination model rows and post-drag normal navigation. Model selection and window/directory setup are controlled native APIs; the drag is genuine, with no emitted drag signals or direct transfer calls.

First Grid/List attempts moved once immediately after press and never began a drag. The noninterfering input observer proved the correct source cell, primary button, modifier state and movement18px beyond the native8px threshold. [GTK4.22.5 source](https://raw.githubusercontent.com/GNOME/gtk/4.22.5/gtk/gtkdragsource.c) additionally requires movement after its100ms minimum interval. Holding140ms before motion makes all3 native Grid/List/Columns controls pass; no product/GTK change. Logs `...-task1-controls-first.log`, `...-task1-input-first.log`, `...-task1-list-first.log`, then `...-task1-controls-held.log`3/3. Temporary raw event instrumentation was removed. First compile's missing existing file-utilities declaration was fixed by including its header; not a product defect.

## Demonstrated Columns destination disappearance

The genuine empty-column drop case retained its destination through primary hold, then native drag preparation selected the original source file. The queued Miller selection handler collapsed the displayed destination child before drop. Both provider count1 and exact original URI passed; target mapping then failed. RED `/tmp/nautilus-finder-dnd-task1-empty-phase.log` and combined `...-task1-nine-first.log`.

A small ListBase signal now forwards the existing native GtkDragSource begin with its real GdkDrag. Miller owns that one drag reference until native finished/cancel/disposal and suppresses selection-driven navigation while it owns the drag. Column cleanup disconnects the ListBase signal. No transfer, hover, selection or directory engine is replaced. The original case plus Grid/List/Columns copy and self-drop cancel passed5/5 (`...-task1-source-selection-green.log`). The later combined11/11 run also proves ordinary column navigation resumes after native finish/cancel (`...-task1-eleven-first.log`). Full aggregate and component review remain pending.

## Native Wayland LINK boundary

The initial Ctrl+Shift LINK expectation failed identically in Grid, List and Columns: the native session negotiated MOVE2. Logs `...-task1-link-modifier-first.log`, `...-task1-grid-link-modifier-baseline.log`, `...-task1-list-link-modifier-baseline.log`. The [Wayland data-device action enum](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_data_device_manager-enum-dnd_action) has COPY/MOVE/ASK and no LINK. Do not convert a protocol MOVE to a different file operation. This is not a Miller-only defect, and no protocol workaround was added.

Maintained link-unavailable-wayland cases in all3 modes observe a real non-LINK native negotiation, then use genuine Escape before release, proving no job/destination and intact source bytes. The fixture cancellation does **not** mean the application automatically rejects Ctrl+Shift; on the tested host its cursor/action is MOVE. Use existing native Create Link commands (separately tested in operations) to create links. X11 or future extension support is not claimed.

## Current Task1 cases and limits

Actual combined11/11 passed, no skips: grid-folder-copy, list-folder-copy, column-folder-copy, between-visible-columns-copy, empty-column-background, inactive-pointer-destination, move-modifier, link-unavailable-wayland, grid-link-unavailable-wayland, list-link-unavailable-wayland, reject-self-drop. Seven accepted transfers use exactly one finished native job with exact bytes; four native cancels have no jobs or filesystem mutation. Copy retains source; Move removes only original source. Native selected-action feedback is checked, and explicit post-finish/cancel model navigation verifies the guard releases.

These are Task1 focused results. Spring-loading/cancellation snapshots, scrolling, external apps, tabs/windows/bookmarks/volumes, source/target mutation, pending clipboard destruction, remote disconnect, whole-component review, and all later full-goal gates remain mandatory. Current session/Home fixtures are not claimed host-network isolated; integrate and test that protection before actual remotes.

## Task1 complete development gate

The source-frozen gate completed with exit0 for all9 commands, and the strict verifier independently rechecked the current source fingerprint and actual case/lifecycle markers. Fingerprint `fcb5682c1cdac6f50b9c27e192ed8c58f29636e2cda015a599344023695d3578`.

- Both development and canonical Meson40/40, including operations63/63 and genuine DnD11/11 per profile; no operation/DnD acceptance skips. Both native portal smoke tests passed.
- Separate actual inspector53/53, navigation10/10 and intentionally delayed history4/4, without external acceptance skips.
- Maintained Python90/90 in188.420s, including actual preview I/O, persistence and Sushi workflows across both profiles.

Driver `/tmp/nautilus-finder-dnd-task1-driver.log`, aggregate `/tmp/nautilus-finder-dnd-task1-final.log`, detailed `/tmp/nautilus-finder-dnd-task1-final-{build-miller,build-preview-canonical}-testlog.txt`. Runner and verifier are plan-owned under `.superpowers/sdd/2026-10-17-miller-dnd/`. This closes Task1 only. Task2/3 and whole-component review remain open; no host installation or push occurred.
