# Miller History and Navigation State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore coherent branch, actionable selection, active column and scroll state through native Back/Forward, tabs, view changes and search, with genuinely delayed backend acceptance.

**Architecture:** Keep Nautilus bookmarks/history/navigation state authoritative. A compact immutable GVariant snapshot contains only URI identities, selections and scroll positions; it owns no widgets/models and is never written to browsed directories. Miller captures/restores that value, while the slot attaches it to existing bookmarks and uses existing history transitions. One owned restoration tick waits for loaded models/layout and is invalidated by newer user interaction or navigation.

**Tech Stack:** C, GTK4/GLib/GIO, native Nautilus bookmarks, private Mutter input, GVfs archive backend and Python fixture orchestration.

**Spec:** `docs/miller/finder-completion-goal.md` sections3–4,8–9/Stage2/C01,C09,C11; current `docs/miller/interaction-contract.md`. Input component review is complete at9321e1c6f; broader C01–C16 remain open.

## Global Constraints

- Continue native execution in the existing isolated finder-completion worktree. The full user contract explicitly directs continuous staged work without repeated design/preference gates. No production install, host input, real user files, credentials or messages.
- Preserve lexical GFile ancestry/authorities, native operations, Grid/List, special views and the existing single action owner. Snapshot identities are in-memory only; do not log credential-bearing URIs.
- Each distinct deliberate endpoint change enters ordinary history once, including committed continuous keyboard folder browsing. Focus/selection within the endpoint, preview, monitors, widths and automatic restore add no entries. Do not add a second history/session system.
- History restores selection only to its recorded directory. Absent files are ignored. Newer selection, focus/navigation or intentional scrolling wins over pending restoration. Entries/popovers retain focus.
- Reuse existing closed-tab/duplicate-tab navigation state. Clone navigation bookmarks when copying mutable per-tab history; immutable snapshot values may be shared. No new cross-process tab session promise beyond upstream capability.
- Width/input preferences already have actual process-restart evidence. Preview persistence and full operations, SFTP/SMB, accessibility, real filesystem performance, CI and disposable package transactions remain mandatory later work.

## Review Focus

- Parent action context differs from endpoint: Back must not apply a parent's selected URI to its child's model (Tasks1–2).
- Restore is pending while the user selects/scrolls/navigates: old state must not overwrite the new interaction (Task1).
- Two tabs share a copied bookmark: subsequent history captures must remain independent (Task2).
- Grid/List or search replaces Columns: valid new endpoint selection wins; canceled search without a new result restores its prior branch/context (Task2).
- A backend really completes A after B was selected or after close: A must not reappear or take action/focus (Task3).

### Task 1: Immutable strip snapshots and cancellable restoration

**Files:** `src/nautilus-miller-view.[ch]`, ListBase internal headers; new `test/automated/display/test-miller-state.c`; display test Meson registration. Promote the existing `nautilus_list_base_get_scrolled_window()` declaration from subclass-private to internal public header so coordination can obtain GTK adjustments without fragile widget-tree searches; its implementation is unchanged.

**Interfaces:** `GVariant *nautilus_miller_view_capture_state(NautilusMillerView *self, gboolean leaving_branch)` returns a full reference of type `(ssda(sasd))`: endpoint URI, active-directory URI, horizontal adjustment, array of (directory URI, selected-file URI array, vertical adjustment). When leaving an old displayed branch after its parent selected a different file/folder, capture the old route's branch marker for that ancestor, so Back restores a coherent old branch. Ordinary endpoint selections are copied unchanged. `void nautilus_miller_view_restore_state(self, GVariant *state, gboolean restore_focus)` reconstructs the endpoint without emitting navigation/history and restores only selections belonging to the corresponding column. Caller retains its value; strip holds an owned reference while restoration is pending.

**Ownership:** strip owns one restoration tick/value and per-column completion tracking. Wait for loading to finish before applying native automatic selections, then restore adjustment values after layout; GTK clamps out-of-range values. Hold temporary references across callbacks. Cancel on new direct navigation, nonautomatic model selection, active-column change, manual press/scroll and disposal. Internal restore changes are guarded. No synchronous enumeration/metadata queries.

- [x] Write missing-API RED tests: endpoint/active-parent round trips, multiselection and vertical/horizontal positions, entry focus preserved, selected file removed while away, new selection/scroll/navigation before pending restore, teardown with pending restore.
- [x] Implement capture/restore and cancellation using existing FilesView/ListBase selection and GTK adjustments. No activation, extra path endpoint or history request may be emitted by restoration.
- [x] Run targeted tests under `with-test-session.py`, then full Meson suite/profile check. Commit with before/after evidence.

### Task 2: Native bookmark, tab, view and search integration

**Files:** `src/nautilus-window-slot.c`, optionally its existing navigation-state header if ownership requires it; Miller strip signal boundary; `test-miller-state.c`, genuine UI tests; interaction/acceptance/README docs.

**Interfaces:** bookmark GObject data key `nautilus-miller-history-state` owns an immutable snapshot reference, destroyed with the bookmark. Slot captures before ordinary navigation, view replacement/search transfer and `get_navigation_state()`. A strip `location-changing` signal before branch reconciliation lets the slot save the old endpoint before a selection-driven request mutates it. Back/Forward captures the destination bookmark value before ordinary navigation and applies it after the destination view is bound. Existing closed-tab navigation state copies the value with its bookmark. Copy navigation bookmarks, selected URI vectors and snapshot references rather than sharing mutable bookmark objects between tabs.

**Policy:** restoring active context does not steal an entry/popover's focus. Explicit incoming selection from reveal/navigation wins over a saved value. When returning from Grid/List at the same endpoint, a user's new selection belongs to that endpoint and becomes active there; unchanged automatic selection permits restoring prior Columns context. Save the original Columns state before search transfer; cancel without a new result restores it. Preserve native special-location fallback and apply only directory-valid result selection. Do not persist snapshots or create history entries for restoration/focus/width changes.

**Observed timing/identity constraints:** reuse the existing internal model accessor (already used by the strip) for a synchronous flat-view selection observer; disconnect it when unbinding. FilesView's idle notification is too late for immediate selection→mode changes. Explicit selection invalidates the bookmark's saved value, without a parallel per-slot selection flag. Retain valid destination snapshots when native Grid/List history creates a new current bookmark. Discard values whose endpoint differs from a bookmark that followed a rename; do not rewrite lexical URI identities.

- [x] Add RED native slot/history tests: selected item/active-parent context and both scroll axes through Back/Forward; no history growth on focus/width/restore; independent duplicate/closed/reopened tabs; deleted selection; mode switching with correct endpoint-only transfer; search cancel returns prior branch/context. Add genuine Alt+Left/Right and entry-focus boundaries.
- [x] Integrate snapshot ownership at existing navigation boundaries. Fix only reproduced reentrancy/selection faults. Retain existing native Grid/List/history behavior and special-view preference.
- [x] Run targeted and full private-session Meson, Python fixture/session/deployment checks and profile validator; update docs with shipped policy/limits and commit.

### Task 3: Actual delayed backend and navigation-entry acceptance

**Files:** new `tools/miller/with-delayed-backend.py`, `test/miller/test-delayed-backend.py`, `test-miller-state.c`; `src/nautilus-monitor.c` for the reproduced synchronous remote-monitor stall; input/acceptance documentation.

**Fixture:** create an owned ZIP containing A/a.txt and B/b.txt, mount it through private GVfs archive service and obtain the canonical mounted root. This backend is read-only and uses no credentials. Identify only the archive process matching owned UID, executable and exact private D-Bus environment; resume it in finally before cleanup. Pausing this process deliberately holds real GIO enumeration pending. The successful throwaway probe is `/tmp/nautilus-finder-history-backend-probe4.log`; raw requested archive URI differs from GVfs's canonical mounted URI, so use the mount root, not a guessed URI. Outer harness owns D-Bus/XDG/compositor and cleanup. Never pause a host backend.

**Reproduced shared-path constraint:** actual private GTK paused in synchronous GVfs CreateDirectoryMonitor from `nautilus_monitor_directory`, `/tmp/nautilus-finder-history-delayed-hang-backtrace.log`. Add a direct paused-backend monitor latency/cancellation RED before the fix. Keep local creation unchanged; make non-native setup asynchronous with a cancelable owned task and a lifetime-safe monitor handle. A canceled handle must reject late completion and release the returned monitor; no widget/directory retention. Connect accepted monitor events in the application main context and reconcile once when monitoring starts to cover changes during setup. Preserve unsupported-monitor behavior and native mount-removal signals. The helper's five-second emergency resume makes the entire run fail; it cannot turn a stalled UI into a passing test. GIO documents cancellation for [monitor creation](https://docs.gtk.org/gio/method.File.monitor_directory.html), [thread-default event delivery](https://docs.gtk.org/gio/class.FileMonitor.html), and [GTask completion/lifetime](https://docs.gtk.org/gio/method.Task.run_in_thread.html).

- [x] RED guard/cleanup cases before a maintained helper: missing/forged session, mismatched bus/PID/executable, child failure and resume on failure. Prove enumeration remains pending while paused and completes after resume.
- [x] Exercise actual A selection/loading, B selection before A completes, then resume. B keeps branch/action/focus; old A rows cannot return. Repeat navigation/close while pending. Use genuine private keys for selection/history and filesystem/model identity assertions, not emitted loading signals.
- [ ] Exercise existing navigation entry points with disposable locations: breadcrumbs/sidebar bookmark/Home, Ctrl+L absolute path and URI, new tab/window, special search/Recent/Starred/Trash/Network and return. Record full C01/C09/C11 evidence boundaries; real SFTP/SMB/auth/monitor/disconnect and release FileManager1 evidence remain later required gates.
- [ ] Full Meson/Python/profile checks, commit. One fresh read-only whole-plan review; one regression-first correction pass for Critical/Important findings, with full green suite. Preserve all rulings/deferred minors; continue larger goal.

## Design reasoning and self-review

Snapshots supplement the bookmarks already used for native Back/Forward and closed tabs. They carry directory identities because a Miller action view can be an ancestor of its endpoint. Immutable values cannot retain discarded widget/model lifetimes; cloning mutable bookmark owners avoids cross-tab overwrite. Restoration uses loaded metadata and one cancelable frame callback, so a user can supersede it while a slow directory loads. Numeric adjustments are clamped to current geometry rather than promising obsolete pixel positions after fonts/window size change. The archive fixture provides genuinely delayed GIO completion now; it does not substitute for mandatory real SFTP/SMB or performance testing.

Self-review: state ownership and Task1 APIs match Task2 consumers; Task3 uses the existing guarded real input harness. Every review-focus item has explicit tests. Stage2 history/selection/navigation is covered by this plan; preview/operation/remote/a11y/performance/release scope remains in the full contract rather than being relabeled complete.
