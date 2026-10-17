# Native File Operations and Clipboard Acceptance Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline, with one fresh whole-plan review after the final task. No implementer or reviewer per task.

**Goal:** Prove actual clipboard/native file-operation behavior and fix demonstrated Columns integration defects without replacing Nautilus operations.

**Architecture:** Every directory remains a real NautilusFilesView. Reuse its action group, clipboard serialization/GDK transfer, native dialogs, GIO/file-operation jobs, progress/cancellation and undo manager. Add an owned-session integration fixture; production changes are limited to demonstrated ownership, lifetime or presentation defects at existing boundaries.

**Tech Stack:** Existing C/GTK4/libadwaita, MillerTestInput, NautilusFilesView/GIO/Autoar, Meson, current Fedora44 GNOME/Wayland dependencies.

**Spec:** User-supplied [completion contract](../../miller/finder-completion-goal.md), sections3,6,9,11 and C02–C05/C10/C16; [interaction contract](../../miller/interaction-contract.md); [native parity matrix](../../miller/finder-parity.md). Preview component completed at82e42bb67; its deferred minors and every ruling remain in [preview review](../../miller/preview-review.md).

## Global constraints

- Continue the existing isolated `finder-completion` worktree. No production installation, user file/key mutation, push, or messages. The full goal explicitly authorizes narrow reversible choices and continued stages without repeated preference questions.
- Reuse existing code. No transfer engine, clipboard format implementation, conflict policy, destination chooser, archive engine, directory indexer or production service.
- Clipboard acceptance must send genuine compositor Ctrl+C/Ctrl+X/Ctrl+V. Direct `nautilus_files_view_move_copy_items` calls remain supplementary and cannot pass these cases.
- Use the existing guarded private bus/compositor and mounted disposable Home. All mutation targets and content assertions must be proved owned fixture paths or the owned XDG Trash. Run unprivileged. Root/source mounts remain read-only.
- Correct bytes/source retention/removal and untouched sibling sentinels are mandatory. Observe native job completion and directory-model results; a filename or action-enabled check alone is insufficient.
- Existing mode IDs in `nautilus-view-info.h`: List1, Grid2, Columns4. Select through native `slot.files-view-mode`. Preserve profile isolation, Grid/List behavior, native permission/capability checks, conflict/error/cancel semantics and supported undo rules.
- Treat passing native behavior as characterization; leave production code alone. For every demonstrated defect, add/watch a focused RED, make the smallest native-boundary correction, rerun it and the full suite before committing. Distinguish fixture errors from product defects.
- Real spring-loaded DnD/external-app drags and installed extension/OpenPGP workflows remain separate mandatory Stage4 plans. This plan does not close them or later remote/a11y/performance/release gates.

## Review Focus

- Clipboard data arrives after its initiating view/tab changes: original destination or safe cancellation, never the new action directory (Tasks1–2).
- Native dialog remains open while a monitor removes/replaces its source view: no stale widget access, captured selection retained or a truthful cancel (Task2).
- Native conflict/error/cancel follows partial work: existing completed results remain truthful and unrelated siblings remain untouched (Task3).
- Toolbar/dialog focus is outside the listing while a child branch remains open: captured context wins, without overwriting keyboard focus or history (Tasks2/4).
- Clipboard formats or explicit Open handlers come from another application: native interoperability, exact selected URIs, no duplicate launch or filename execution (Tasks1/4).

## Files and shared test interfaces

Create `test/automated/display/test-miller-operations.c`; register it in the existing display Meson table with `GTK_A11Y=test` and `is_parallel:false`. Keep local fixture helpers in that test until a real shared boundary warrants extraction. Reuse `test-utilities`, `miller-test-input.[ch]`, native FilesView/model queries and existing fixture patterns rather than building a UI automation framework.

Task1 produces a local `OperationFixture` holding the owned root, source directory/file, destination parent/child/sibling, NautilusWindow and active slot; helpers `operations_setup`, `operations_teardown`, `operations_view_for_directory`, `operations_settle`, `operations_assert_bytes`, and native input chords. Helpers validate owned paths before mutation/assertion, use native view discovery and progress/undo readiness, and identify actual GTK focus/action owner. Task2–4 consume these same local helpers. Test-only external clipboard/open-receiver roles must verify private session/Home before reading any fixture path and use retained GSubprocess children for cleanup, never discovered host PIDs.

Production audit boundaries: `src/nautilus-files-view.c` (copy/cut/paste, dialogs, actions, pre/post operation lifetime), `src/nautilus-file-operations.c` and existing conflict/progress/undo implementations (reuse), `src/nautilus-miller-view.c`/slot action ownership (narrow fixes only). Native PasteCallbackData already captures its destination URI and uses the view cancellable. CopyCallbackData already captures selected files; investigate its borrowed view lifetime with real regressions before changing it.

### Task 1: Genuine clipboard, destination and undo

**Files:** new operations test and display Meson entry; native FilesView/Miller boundary only for reproduced defects; parity/evidence docs.

**Consumes:** existing guarded MillerTestInput, native clipboard/actions, slot mode action and undo/progress APIs. **Produces:** local `OperationFixture` with window/slot, owned root/source_dir/source_file/parent/child/sibling, view_id, native progress manager and retained job-info array; `void operations_setup(OperationFixture *, gconstpointer view_id)`, `void operations_teardown(OperationFixture *, gconstpointer)`, `NautilusFilesView *operations_view_for_directory(OperationFixture *, GFile *)`, `void operations_settle(OperationFixture *)`, and `void operations_assert_bytes(OperationFixture *, GFile *, const char *)`. These static helpers live in the operations test and are consumed by later tasks; they wrap existing APIs, not file-operation semantics.

- [x] Add `clipboard-list`, `clipboard-grid`, `paste-focused-parent`, `paste-empty-child`, `cut-across-columns`, `copy-undo-redo`, `same-directory-duplicate`, `external-clipboard-source` and `clipboard-after-tab-switch` cases. Set up selection through native model APIs where useful, but Copy/Cut/Paste and undo/redo use real keys; record that evidence boundary. Source payloads include spaces/Unicode names and known bytes. For parent targeting, copy from a separate source directory, open destination parent with child disclosed, retain parent action ownership, then real Paste; assert only parent changed. For empty child, real Paste then Left; assert correct destination and reachable parent. Duplicate uses native same-directory Copy/Paste and native generated naming, not a new duplicate action.
- [x] Establish Grid/List controls first. Assert native clipboard formats/ownership and actual window/list focus before interpreting a failure. External clipboard uses a separate owned minimal GTK process exposing public GDK file-list/URI data through its own real Copy event; no fake clipboard callback. Tab switch case verifies captured source and intended tab destination, including late data arrival.
- [x] Run each case in the mounted-Home private session. Characterize inherited passes. For a defect, preserve RED and native state/clipboard diagnostics, correct its actual cause, and rerun the original case. Assert checksums/bytes, source retention/removal, selected/model rows, native job completion, undo/redo state and untouched child/sibling sentinels.
- [x] Run full build/profile, all Meson executables (expected39 after registering this one), actual preview53, genuine navigation10/delayed4 and maintained Python89. Update exact counts/evidence; commit. Genuine service/environment cases remain explicitly separate; missing environment is not a passing case.

### Task 2: Captured menus/dialogs and empty/ancestor actions

**Files:** operations test, native FilesView/slot/Miller ownership/lifetime boundaries if required, parity/evidence docs.

**Consumes:** Task1 fixture/helpers and native menus/new-folder/rename/destination dialogs. **Produces:** captured-context acceptance and regression fixes.

- [x] Add `new-folder-parent`, `new-folder-empty-child`, `inactive-column-menu`, `toolbar-with-entry-focus`, `rename-captured-selection`, `copy-to-captured-selection`, `move-to-captured-selection`, `dialog-source-pruned` and `operation-tab-switch`. Use genuine shortcuts/menu interaction for ordinary workflows; use native APIs for controlled focus/branch/monitor transitions and label them accordingly. Native GtkFileDialog/rename/new-folder UI remain intact.
- [x] For dialogs, capture selection/backing directory, move action ownership to another column/tab or externally remove the old branch while the dialog is outstanding, then decide/cancel through its native controls. Assert original target or truthful safe cancellation, no UAF/crash, no mutation of the newly active directory, preserved entry focus where applicable and coherent job/model state. For branch mutation, use actual owned GIO rename/delete, not emitted monitor signals.
- [x] First reproduce each defect. Reuse existing captured destination/selection data and native cancellables/ref/weak ownership; correct the narrow lifetime boundary rather than adding a second operation dispatcher. Preserve focus-only/no-extra-history rules.
- [x] Repeat native Grid/List controls and all Task1 cases, then the full project gate and exact parity/evidence update; commit.

### Task 3: Native conflicts, cancellation, errors and Trash

**Files:** operations test; native integration corrections only where reproduced; existing conflict/progress/undo code stays authoritative.

**Consumes:** actual clipboard/native jobs and dialogs; `NautilusProgressInfo`, undo manager and owned XDG Trash. **Produces:** native conflict/error/cancel/recovery acceptance.

- [ ] Add `conflict-replace`, `conflict-keep-both`, `conflict-skip`, `conflict-cancel`, `directory-merge`, `partial-failure`, `permission-denied`, `progress-cancel`, `trash-undo-restore` and `permanent-delete-confirmation`. Compare representative Grid/List controls. Native conflict dialog responses are Cancel/Skip/Replace/Rename; folder merge uses its existing Replace/Merge presentation. Keep-both edits the native suggested-name entry. Assert exact source/destination bytes and sentinel directories after every result.
- [ ] Use owned readable/unreadable files and restore permissions before cleanup. Native conflict/permission dialogs provide deterministic real job pauses. Cancel a started native job through its existing UI/cancellation path; assert progress finishes/cancels and partial results match actual native semantics. Do not promise atomic batches or that all operations are undoable. Unsupported Trash must leave data intact and show its native reason, never silently delete. Establish a supported owned local Trash workflow separately and verify native restore/undo destinations.
- [ ] For each regression, prove RED before correcting integration. Reuse native conflict policy, progress/cancellation, permission checks and undo stack. No custom replace/merge/copy loop. Preserve any upstream baseline differences explicitly.
- [ ] Run all operations cases plus full profile/Meson/preview/navigation/Python regressions; record actual native outcomes and limits, then commit.

### Task 4: Remaining native basic actions and component review

**Files:** operations test and small owned receiver fixture role if needed; user/parity/evidence docs; narrow existing native boundaries for demonstrated defects.

**Consumes:** prior helpers plus native Open/OpenWith, templates, links, Autoar, Properties/permissions and terminal actions. **Produces:** basic-operation matrix evidence and one reviewed component.

- [ ] Add explicit `open-once`, `open-with-captured-selection`, `template-parent-target`, `link-supported`, `compress-parent-target`, `extract-parent-target`, `properties-selection-and-directory`, `permissions-native` and `terminal-directory` cases. Use native GAppInfo/private XDG defaults and an owned receiver to record exact URIs/launch count; hostile filenames remain arguments, not shell code. Test template/archive/link results through existing libraries and bytes/symlink targets. Never change host defaults or launch a host-session terminal.
- [ ] Exercise parent action context while endpoint differs, focus changes after initiation and cancellation. Record native availability reasons honestly. Update every relevant parity row with actual revision/test/evidence; extension/OpenPGP and DnD rows stay open for their own mandatory plans.
- [ ] Run fresh complete regressions, profile and service-isolation/persistence/provider checks. Obtain one fresh read-only whole-operations-plan review, re-grade by user effect, and perform one RED→GREEN correction pass for Critical/Important findings with a fresh full green suite. Keep all minors/rulings. No per-task reviewers or second final review.
- [ ] Commit and record completion through task-done. Continue the full goal into native DnD, extensions and later mandatory stages; the installed manager stays unchanged until separately authorized deployment.

## Self-review and execution ruling

The user supplied purpose, native architecture, explicit operation rules and acceptance gates, and directs continued stages without repeated preference questions. This is a focused test/integration plan against that approved written contract, not a new architecture or operation engine. Routine fixture choices stay reversible and owned. The next DnD plan must decide temporary hover-branch retention explicitly using existing browsing state; it is not silently specified here. Full regression wrappers are reused from the completed preview work, preserving input-guard and LocalSearch dispatch. Branch integration/publishing belongs to the full-goal release handoff, not this component boundary.
