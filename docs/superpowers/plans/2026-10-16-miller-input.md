# Miller Input and Type-to-Select Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the Stage2 input contract: guarded genuine session input, Unicode/IME-aware type-to-select enabled by default in Columns, discoverable stock-search preference and verified native shortcut boundaries.

**Architecture:** Keep native FilesView/ListBase operations. A small per-column typeahead object owns GTK IM context, bounded prefix/preedit state and cancellable incremental matching over loaded rows. One strip capture controller filters text before file-view shortcuts; existing arrow handling and native actions remain downstream. Test input uses only the disposable compositor already owned by the test harness.

**Tech Stack:** C, GLib/GIO, GTK4/Pango, GSettings, private Mutter RemoteDesktop D-Bus, optional disposable IBus/Anthy, Meson and Python unittest.

**Spec:** `docs/miller/finder-completion-goal.md` sections3–4/9 and C01/C09/C13; `docs/miller/interaction-contract.md`. Existing layout review `docs/miller/layout-review.md` preserves broader open requirements.

## Global Constraints

- BASE70cef4ba2, isolated finder-completion worktree. No production installation, host input injection, real files or real credentials.
- User-supplied contract directs continuous native implementation. Choose reversible details and record rulings without another design/preference authorization gate.
- Unicode input must use GTK input methods; no keyval-to-ASCII buffer. Respect entries, popovers, dialogs, preview controls, modifiers and native activation.
- One action directory remains separate from branch endpoint. Typing selects without launch; selecting a directory discloses normally with focus retained.
- New callbacks/timers/scans have owners and cleanup. No synchronous directory enumeration, no persistent filename/typeahead history.
- Full history/session restoration, preview, operations, remote/accessibility/performance and release stages remain mandatory follow-on work. Neither a genuine-input smoke nor this plan closes all C01–C16.

## Review Focus

- A copied/missing/stale marker or real display/bus cannot authorize input delivery (Task1).
- Space/Enter during IME composition must commit/convert, not preview or launch (Tasks2–3).
- New tab/focus/selection/model generation invalidates older pending matching or composition (Tasks2–3).
- A100k-row prefix miss must yield between bounded chunks; closed columns cannot publish late selection (Task2).
- Ctrl+F, Ctrl+L, F2, Ctrl+C/V and typed editing retain native behavior, including preference-off stock search (Task3).

### Task1: Guarded private compositor input

**Files:** modify `tools/miller/with-test-session.py`, `test/miller/test-session.py`, test Meson files; create `test/automated/miller-test-input.[ch]`, `test/automated/display/test-miller-input.c`.

**Interfaces:** harness writes600-mode `runtime/miller-session.ini` containing exact runtime, absolute Wayland socket, private D-Bus address and owned compositor PID, and exports `MILLER_TEST_SESSION`. Wait for the socket and RemoteDesktop service owned by that PID before launching the test command. `MillerTestInput *miller_test_input_new(GError**)` validates marker ownership/mode/paths/environment and bus service PID before CreateSession. Keep the creating connection/session until `miller_test_input_free`. Initialize lazy keyboard/pointer devices before GTK windows map. Expose `miller_test_input_key(input,keysym,pressed,error)`, `miller_test_input_button(input,button,pressed,error)`, and relative motion, each rechecking owner PID before delivery. No fallback to host devices, display or portals. Pure guard validator takes marker/runtime/display/address arguments for negative unit tests; constructor additionally verifies current bus/process identity.

- [ ] RED Python harness marker/readiness assertions and C guard/smoke tests before implementation. Guard rejects missing marker, symlink, wrong owner/mode, wrong display/runtime/bus, invalid PID and canonical host runtime. Negative guard tests never create a remote session.
- [ ] Implement marker and guarded test helper; use existing harness cleanup, never host process termination. Genuine GTK entry test receives actual compositor `x`, click/keyboard modifier events and preserves command exit status. Run under private session; absent private marker is an explicit skipped environment, not a pass.
- [ ] Run Python harness tests and targeted Meson guard/input tests. Commit with evidence; full suite at task gate.

### Task2: Bounded Unicode/IME typeahead engine

**Files:** create `src/nautilus-miller-typeahead.[ch]`, displayless Unicode-policy tests and display tests; modify source/test Meson files.

**Interfaces:** final GObject `NautilusMillerTypeahead`; constructor `nautilus_miller_typeahead_new(NautilusFilesView *view, GtkLabel *hint)` (weak view/hint ownership); `gboolean ...handle_event(self,GdkEvent*)`; `void ...reset(self)`; `void ...set_focused(self,gboolean)`; `gboolean ...has_preedit(self)`. Export text policy helpers for canonical case-fold prefix matching and grapheme removal for displayless tests. Owner column clears object before unparenting view. Object owns IM multicontext, timeout and one incremental scan; reset/dispose cancels work and clears client widget safely.

**Policy:** prefix limit256 Unicode characters, inactivity reset1second outside preedit, canonical normalization+Unicode case fold, first matching loaded row in current sort order. Backspace removes one grapheme; Escape clears active prefix/preedit without navigation; bare Space outside preedit retains Quick Look. Committed strings may contain Unicode/IME spaces. No activation on commit. Preedit text is displayed but does not select. Match at most256 rows or4ms per idle; capture model/generation, cancel/restart on changes and ignore stale owners. Hint is plain text, no markup, named for accessibility, hidden when idle. Candidate scans never enumerate or fetch remote content.

- [ ] RED policy cases: composed/decomposed accents, case-fold expansions, CJK/Hangul, grapheme/emoji removal, invalid UTF8,256-character bound. RED GTK commit→selection, preedit→no selection, cancel/close/model replacement→no stale selection; empty/large model miss yields to main loop.
- [ ] Implement policy and engine, respecting borrowed file display names and real model item types. Use native ListBase cursor/selection to preserve existing operation/disclosure machinery. Hold temporary references across callbacks that can reenter navigation; no source/object reference cycle.
- [ ] Run policy/GTK tests and full existing suite; commit.

### Task3: Scoped integration and genuine IME acceptance

**Files:** strip, slot and preferences source/schema/UI; new genuine-input tests; interaction contract, README and acceptance ledger.

**Interfaces:** schema `miller-type-to-select` boolean defaulttrue, Preferences switch “Type to Select in Columns”, subtitle “Type a name to select it in the current column. Use Ctrl+F to search.” Strip capture legacy controller routes key press/release only from active listing (exclude Editable/Popover descendants). Per-column hint/object lifetime is explicit; focus leave/tab deactivation/settings disable resets composition and scans. Parent strip capture precedes FilesView shortcuts so IME Space/Enter can consume before preview/activation. Slot disables implicit type-to-search only for enabled Columns while explicit search remains usable; Grid/List unchanged.

- [ ] RED genuine keyboard test: typing selects loaded ASCII/Unicode files without opening recursive search or launching. Native arrows and Enter/F2/modifiers retain behavior. Preference-off typing opens stock search, Ctrl+F always opens explicit search; Ctrl+L/editor/rename keys do not change branch.
- [ ] Implement scoped settings/integration. Add rapid selection→Right before coalesced notification regression: old parent must not reclaim child action/focus. Correct only reproduced lifetime/ownership faults.
- [ ] In a disposable IBus/Anthy session with `GTK_IM_MODULE=ibus`, deliver actual compositor keys for Japanese preedit/conversion/commit. Assert preedit doesn't select, Space doesn't preview, Enter commits without launch, committed Unicode selects correct file, focus stays in list; switching/closing tab discards late composition. Missing engine/session is explicitly blocked/skipped, never mislabeled verified.
- [ ] Run real input, full Meson/Python/profile checks and process-restart width/preference persistence check. Record exact revision/session/logs and limitations. Commit; one fresh whole-plan independent review, one red/green correction pass for Important/Critical findings. Keep later requirements active.

## Design reasoning

GTK IM context is preferred to hidden text entries because the visible list retains focus and owns file actions. The capture point must be an ancestor of FilesView to see IME conversion before its native Space shortcut. Incremental matching uses loaded metadata and cancels on model changes;100k-name synchronous scans would violate the responsiveness goal. Input helper is test-only and must establish virtual devices before mapping windows: the completed throwaway probe showed Mutter's lazily created keyboard otherwise leaves those windows without server keyboard focus.
