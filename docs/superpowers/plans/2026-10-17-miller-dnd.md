# Miller native drag-and-drop implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan inline, task-by-task. No per-task agents; one fresh whole-component reviewer after implementation.

**Goal:** Verify and complete genuine native file drags, temporary spring-loaded columns and edge scrolling without losing source identity, selection, focus or history.

**Architecture:** GTK/Nautilus continue to own drag payloads, negotiation, destinations and file jobs. Miller coordinates only provisional directory disclosure and snapshot restoration. Reuse the native hover timeout, current native drop acceptance boundary and existing Miller/navigation snapshots.

**Tech stack:** C, GTK4/libadwaita, GDK/Wayland, existing MillerTestInput/private Mutter, native Nautilus/GIO models/jobs, Meson, Python.

**Spec:** `docs/superpowers/specs/2026-10-17-miller-dnd-design.md`; full authority `docs/miller/finder-completion-goal.md`, especially sections3/6–9 and C04/C05/C06/C10/C16.

## Global constraints

- One real NautilusFilesView per directory, coordinated by NautilusMillerView and hosted by NautilusWindowSlot. Preserve Grid/List.
- Keep `org.gnome.Nautilus.Miller` development identity; no production installation, default change, push or real user-file/key mutation.
- Reuse native drag sources/targets, GTK vertical autoscroll, GFile identities, Nautilus transfer/conflict/progress/undo, directory models and immutable browsing snapshots. No competing operation, hover-timer, clipboard or history engine.
- The destination under the pointer wins independently of active action ownership. Genuine pointer/keyboard session input is required; direct transfers and emitted drag signals are supplemental only.
- Hover branches are provisional: no committed slot/history changes until native acceptance. Cancel/rejection/leave restores the old snapshot unless newer navigation superseded it; accepted input retains the destination branch. Later job failure remains native job behavior.
- All work uses guarded disposable Home/XDG/private compositor and owned files. Remote disconnect acceptance requires actual Stage5 SFTP/SMB, never relabel local deletion.
- Missing environment is open/blocked evidence, never a passing skip. Preserve baseline failures and every failed fixture attempt.

## Review focus

- A pending origin navigation/late directory completion arrives after spring-loading: it cannot overwrite the current drag branch or restore an obsolete selection (Task2 deliberate delay case).
- An old drag terminates after a new drag/navigation/tab activation starts: it cannot restore the old snapshot over newer state (Task2 repeated/newer-navigation cases and Task3 tab lifetime).
- Pointer crosses a folder row, an empty background and an inactive column while modifiers change: final native pointer destination and negotiated action win, no duplicate job (Task1 negotiation/destination cases).
- Source or target disappears while native hover/edge scrolling is pending: clean cancellation/error, no stale callback or wrong-directory operation (Task3 actual GIO removal).
- Incoming foreign drops have no local GdkDrag object: native acceptance versus leave still decides branch retention correctly, without creating a replacement transfer path (Task3 owned external GTK source/sink).

### Task 1: Shared fixture and genuine native drag characterization

**Files:** create `test/automated/miller-test-operations.[ch]`, `test/automated/display/test-miller-dnd.c`; modify existing operations test and display Meson table; evidence/parity docs.

**Consumes:** existing OperationFixture and its owned setup/teardown/input/model/job helpers, MillerTestInput and native GtkDragSource/GtkDropTarget.
**Produces:** move the existing OperationFixture declaration and genuinely shared operations_* helpers unchanged to the test-only helper. Export `void operations_set_input(MillerTestInput *input)` to bind the already verified borrowed input instance. Preserve exact existing helper signatures, including setup/teardown, owned/bytes, pump/settle, view lookup/selection/focus, native keys/chords and widget lookup. New `void operations_pointer_to(OperationFixture *f, GtkWidget *widget, double x, double y)` moves to a controlled point in the owned fullscreen window; use the existing pointer reset/surface-position proof. Existing operations_click delegates its positioning to this helper. The operations executable retains its independent app/clipboard/receiver roles; shared teardown keeps retained child cleanup balanced.

- [ ] Run the existing63 operation cases as the refactor baseline. Extract shared helper code without production API changes or changed assertions; register extra fixture sources only for the consuming display executables. Both parent and normal-app child initialize the borrowed input explicitly.
- [ ] Register test-miller-dnd with GTK_A11Y=test, is_parallel:false and the existing480s limit. Build a DndFixture around OperationFixture, with real native drag begin/selected-action/cancel/end/drop observations, not synthetic emission. Use uniquely named owned files/folders and literal known bytes.
- [ ] Add `grid-folder-copy`, `list-folder-copy`, `column-folder-copy`, `between-visible-columns-copy`, `empty-column-background`, `inactive-pointer-destination`, `move-modifier`, `link-modifier`, and `reject-self-drop`. Source selection uses native model setup; press, threshold motion, modifiers and release are genuine MillerTestInput events. Before interpreting a result, prove a real GdkDrag began and a native drop was accepted/rejected. Assert exact bytes/link identity, source retention/removal, exactly one native job where applicable, destination model rows and untouched child/sibling sentinels.
- [ ] Characterize inherited passes first; preserve RED for genuine defects, find the cause and make only narrow native integration corrections. Direct transfer APIs do not close these cases. Keep hover duration short here; spring browsing belongs to Task2.
- [ ] Run full regressions: both builds/profiles, expected40 Meson executables after the new executable, all63 operations, new real DnD cases, actual inspector53/navigation10/delayed4 and maintained Python90. Use the established session/Home dispatch and strict count/no-skip/source-fingerprint verification. Update evidence and commit.

### Task 2: Native spring-loaded disclosure and cancellation

**Files:** native ListBase hover/drop boundary, Miller coordinator and new DnD test; interaction/evidence docs. Modify FilesView/slot only for a reproduced integration need.

**Consumes:** Task1 actual drag/input/job fixture; existing LOCATION_CHANGING/LOCATION_REQUESTED, capture_state/restore_state and native hover machinery.
**Produces:** Miller-only handled spring disclosure with one owned pre-drag immutable snapshot and current native drop identity. Keep native Grid/List fallback and native hover timing. Expose only small native lifecycle signals if needed by the demonstrated regression: handled `spring-load(NautilusFile*)` at existing hover callback, and `drop-accepted(GdkDrop*,GFile*)` after existing PERFORM_DROP returns acceptance. Coordinator helpers remain private; no new file-operation API.

- [ ] First add `hover-preserves-source-selection`, `nested-spring-cancel`, `spring-drop-retains-destination`, `spring-backtrack-ancestor`, `leave-restores-branch`, `newer-navigation-wins`, `old-drag-cannot-restore-new-drag`, and `late-origin-completion`. Assert actual hover-opened directory models, unchanged original drag URIs/selection, no native file jobs before release, no provisional history entries, stable focus, exact cancellation restoration and correct accepted destination/history behavior. Use the maintained intentionally delayed backend where actual late completion is needed; do not substitute a fast local path.
- [ ] Watch missing behavior fail before adding integration. Reuse native hover timing and existing real FilesView models; intercept only Miller disclosure before native selection replacement. Preserve the origin bookmark through the existing location-changing path before provisional reconciliation. Ordinary slot navigation/history commit occurs only after native drop acceptance; cancellation uses the original snapshot while identity/generation still owns it.
- [ ] Own/release drop references, signal connections and snapshots explicitly on completion/leave/unmap/disposal. Ignore stale end/leave/completion after a newer interaction. Source-tab and monitor changes cannot roll back newer navigation. Keep entries/menus/preview controls and native keyboard behavior intact.
- [ ] Rerun every Task1/Task2 case and existing Grid/List controls, then the complete40-executable/two-profile gate plus63 operations, inspector53/navigation10/delayed4/Python90. Record RED/GREEN and native limitations; commit.

### Task 3: Scrolling, cross-client destinations and lifetime acceptance

**Files:** DnD test and test-only external GTK role, Miller strip scrolling/lifecycle boundary if needed, existing pending-clipboard test, docs.

**Consumes:** Task1 shared fixture/native real drags and Task2 snapshot ownership.
**Produces:** actual vertical/horizontal drag scrolling and public GTK interoperability/lifetime evidence. Foreign source/sink use public GdkFileList, retained owned GSubprocesses, private proof and exact URI/action receipts. Incoming copy/move/link work is done by Nautilus. Outgoing foreign URI receipt is not claimed as a transfer job; native cross-window moves independently prove real filesystem movement.

- [ ] Add `vertical-edge-scroll`, `horizontal-edge-scroll`, `horizontal-edge-rtl`, `edge-stop-on-cancel`, `between-tabs`, `between-windows`, `sidebar-bookmark`, `mounted-volume-destination`, `external-files-in`, `external-files-out`, `source-removed`, `destination-removed`, `tab-closed-during-drag`, and `repeat-drag-lifecycle`. Use actual native tab/sidebar/volume targets where supported and guarded native window management to expose distinct windows. Missing volume/provider remains open acceptance and cannot be counted as pass.
- [ ] Add `clipboard-after-tab-destruction` to the maintained operations executable using its real delayed independent provider: start Paste while data is paused, close the initiating tab, resume the retained provider, prove safe cancellation/owner release and untouched surviving directories. Preserve the existing retained-tab delayed case; the new operation count is64.
- [ ] Characterize native GTK vertical scrolling before changing code. If horizontal scrolling is missing, add only a strip-owned frame callback driven by native drag motion, bounded at the adjustment limits, respecting physical RTL geometry. Define a32-logical-pixel edge zone and maximum600px/s with clamped frame delta; cancel on leave/end/unmap/disposal. No synchronous model/remote query or replacement vertical engine.
- [ ] Capture incoming/outgoing public URI and chosen COPY action receipts; real native cross-window/cross-column operations prove bytes and source removal. Actual GIO source/destination removal must not produce stale callbacks or wrong target writes. Preserve source payloads/siblings on cancel/error. Keep actual remote disconnect explicitly open for the mandatory SFTP/SMB fixtures.
- [ ] Run fresh complete regressions: both40-executable Meson suites, all64 operations after the new clipboard-destruction case, every DnD case, actual inspector53/navigation10/delayed4 and maintained Python90 plus any new fixture guards. Record exact genuine case counts/no-skips and update parity/acceptance. Obtain one fresh read-only whole-component review from the most capable available reviewer, re-grade every finding/declined item, perform one RED→GREEN correction pass for Important/Critical issues and a fresh full green gate. No second review. Record all minors/rulings permanently, commit and task-done.

## Execution rulings and self-review

This is focused integration against the user-approved full written goal. Its direction to continue stages, make narrow reversible choices and avoid repeated preference questions governs routine fixtures and the explicit temporary-branch policy. Native inline execution and the same finder-completion worktree continue; integration/deployment decisions belong to Stage6.

The helper extraction has two real consumers and preserves existing assertions; it is not a new test automation framework. All newly listed test cases must execute with genuine session input and observed native drag/drop, or remain explicitly open. The provisional/committed distinction is limited to a live drag and requires delayed/navigation/lifetime regressions. Real remote disconnect, extension/OpenPGP, broad accessibility/performance, observed CI and disposable release transactions remain required by subsequent full-goal work, with no waiver inferred from this component.

## Verification commands

Build: `meson compile -C build-miller` and `meson compile -C build-preview-canonical`. Expected: exit0. Check development identity with `python3 tools/miller/check-profile.py build-miller`; expected: exit0.

Genuine case family, from the worktree (use the appropriate absolute build/schema paths when wrapped):

```sh
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- \
  env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" \
  "$PWD/build-miller/test/automated/display/test-miller-dnd" -p /miller/dnd
```

Task1 expected:9 actual passing DnD cases/no skips, plus existing operations63. Task2 expected:17 actual cases in the separate delayed-backend run, plus operations63; ordinary Meson must mark any external-only case explicitly and the separate backend run must pass it. Task3 adds14 DnD cases (expected31 actual) and one operations case (expected64); exact counts may increase only for recorded additional cases, never through silent skips. All genuine cases require observed GdkDrag/native drop/cancel and filesystem/receipt assertions.

The complete gate runs both full40-executable Meson suites through the established input-guard/LocalSearch/mounted-Home dispatch, separate actual inspector53/navigation10/delayed4 and actual delayed DnD when needed, plus every maintained Python test (baseline90). The executor creates a plan-owned runner/verifier from the preserved operations evidence bundle, updates exact new case counts and source fingerprints, and records the exact command/log paths before running. Expected: every command exit0, every required external case actually passes, fingerprint unchanged throughout; missing environment or skips never substitute for actual acceptance.
