# Finder Completion Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Stage 0 and Stage 1 of the user-supplied full completion contract without touching the installed file manager; retain all later stages as required open work.

**Architecture:** Preserve the existing per-directory FilesView strip and slot. Add narrow preview discovery, action-context and deployment corrections with private-bus, GTK and mocked-command regressions. Keep the full acceptance matrix authoritative across subsequent subsystem plans.

**Tech Stack:** C11, GTK4/libadwaita, GLib/GIO/GTestDBus, Python unittest, Meson, Fedora 44 Wayland.

**Spec:** `docs/miller/finder-completion-goal.md`; interaction policy `docs/miller/interaction-contract.md`.

## Global Constraints

- Baseline `8618e792713ae6b248c4ca7d740ae78ce9d3040b`; upstream 50.4. Preserve existing user work and installed `50.4-1.miller1.fc44`.
- No production installation or operations on real user files. Only private test buses, test application identity and disposable directories/services.
- Native operation engine, canonical production identities, isolated Miller identity and native special-location fallback remain.
- Do not claim synthetic clipboard/drag/path tests satisfy genuine input or remote requirements.
- Every defect gets a failing regression before the correction. Keep inherited portal timeout separately visible.
- The supplied contract explicitly instructs continuous staged implementation; existing native execution method persists. No repeated preference/design authorization gate; new deployment remains separately gated.

## Review Focus

- Selection restoration arriving after newer user multiselection must not replace it (Task 4).
- An open parent context menu must not resolve actions from a newly focused child (Task 4).
- Late preview proxy callbacks after teardown/restart must not publish obsolete providers or touch destroyed owners (Task 2).
- A failed drain must prevent the RPM transaction; failed launch must not write a success record (Task 5).
- Special-location fallback must preserve Columns preference while the toolbar truthfully describes the next supported mode (Task 3).

---

### Task 1: Baseline, contract and fixtures

**Files:** `docs/miller/finder-completion-goal.md`, `interaction-contract.md`, `finder-parity.md`, `acceptance.md`, `performance.md`; `tools/miller/create-fixture.py`; `test/miller/test-fixtures.py`.

**Interfaces:** Fixture generator exports `create_fixture(root: pathlib.Path, entries: int, depth: int) -> dict`; only writes inside caller-owned empty root. JSON manifest includes entry/depth counts and safe fixture paths, not credentials. CLI requires explicit output directory and refuses an existing nonempty directory. Defaults 32 entries / 3 levels; caller can request 10,000 / 100,000 / 30-level fixtures.

- [ ] Record clean HEAD, installed identity, dependency versions and tools; copy user contract unchanged; write state/operation/acceptance documents. All 16 expanded scenarios initially remain open.
- [ ] Add unittest `small_manifest_and_contents`, `deep_unicode_and_link`, `reject_nonempty_root`; assert exact count, 30-level path capability, Unicode/percent names, lexical link fixture, rejection without modifying existing file. Watch refusal regression fail before implementing generator.
- [ ] Implement explicit owned fixture creation with no privileged operations, shell-derived filenames or recursive deletion of caller files.
- [ ] Build `./tools/miller/build-dev.sh`; run profile check, deployment validation and all discovered Meson tests with clean GTK config. Expected new baseline evidence, portal may remain a documented failure; never relabel as pass. Record logs and available real-input/remote/disposable-system harnesses.
- [ ] Commit baseline/fixtures and record Task 1 gate; expanded capability acceptance remains open.

### Task 2: External preview discovery and restart

**Files:** `src/nautilus-previewer.[ch]`; `test/automated/displayless/test-previewer.c`; `test/automated/displayless/meson.build`.

**Interfaces:** Add internal dependency injection `nautilus_previewer_setup_for_connection(GDBusConnection *connection, const char *profile)` for private-bus regression; existing setup/teardown/show/close API stays. Profiles use own service first, then canonical and Devel without duplicates; canonical production tries canonical then Devel. Pair each service with its corresponding object path. Provider lifecycle owns cancellable, signal/name-watch IDs and callback identity.

- [ ] Register a private fake `org.gnome.NautilusPreviewer2` with Visible=true; tests run without GTK windows or touching the user's previewer. For profiles empty/Miller/Devel assert canonical discovery when only normal provider exists; Miller regression must fail on baseline. Include own-profile preference when present.
- [ ] Fix ordered provider discovery and exact object paths; retry missing provider on later request or appearance. Track provider disappearance/restart and discard obsolete callbacks. Teardown resets state and cancels pending discovery safely.
- [ ] Add missing-provider, provider-restart, teardown-during-discovery regressions. Expected all private-bus cases pass; existing Miller/view/profile checks stay green. UI missing-provider feedback and full Quick Look ownership are still Stage 3 work.
- [ ] Commit with red/green evidence.

### Task 3: Accurate view selector

**Files:** `src/nautilus-files-view.c`, `src/nautilus-window-slot.c`, `src/resources/ui/nautilus-view-controls.blp`, `src/resources/ui/shortcuts-dialog.blp`; `test/automated/display/test-miller-view.c`.

**Interfaces:** Keep Grid=2, List=1, Columns=4. Normal folder cycle List→Grid→Columns→List. Toolbar icon/tooltip/accessibility name and explicit three-mode menu must agree with actual destination. Special locations skip unsupported Columns in cycle while explicit Columns selection retains normal preference.

- [ ] Add GTK regression checking tooltip/icon and activated resulting mode through the real slot action, including normal Grid→Columns and special fallback. Watch normal Grid tooltip case fail.
- [ ] Correct labels/icons and slot contextual binding as needed; retain menu sort-section index contract. Accessible label follows destination; Ctrl+1/2/3 docs identify actual modes.
- [ ] Run Miller integration and existing files-view tests, profile/desktop validation. Commit.

### Task 4: Action ownership and delayed branch regressions

**Files:** `test/automated/display/test-miller-view.c`, `src/nautilus-files-view.c`, `src/nautilus-miller-view.c`, `src/nautilus-window-slot.c` only if reproduction requires fixes.

**Interfaces:** Reuse existing private action group/backing URI getters and context-menu creation. Open menu must resolve its initiating view actions after focus switches; ordinary root actions still resolve only active context. Add object-scoped action group to popover only if failing evidence proves missing binding.

- [ ] Add focused GTK tests for inactive parent context menu, intervening focus switch, toolbar while entry focused, inactive tab, empty non-rightmost destination, newer multiselection versus delayed branch restore and discarded pending callbacks. Assert action enabled state, backing URI, selection and untouched siblings with disposable files.
- [ ] For any failure implement only the demonstrated ownership correction; prove red/green. Baseline passing behaviors get coverage without rewrites. Record remaining actual-input acceptance separately.
- [ ] Run complete relevant GTK tests and headless path tests; commit and record gate.

### Task 5: Deployment phase ordering and failure evidence

**Files:** `tools/miller/deploy.py`, wrappers and `test/miller/test-deployment.py`, `README.miller.md`.

**Interfaces:** Explicit preflight→verified rollback→drain→transaction→settings→launch→verification→success record. External commands are injectable/mocked in unit tests. Existing original snapshot is immutable; cached stock RPMs are validated by metadata/checksum before use. Runtime failures identify phase and recovery, never imply success. An offline transaction requires explicit opt-in to invalidate it.

- [ ] Mock tools and temporary package/state files. Add `drain_precedes_transaction`, `drain_refusal_prevents_transaction`, `wrong_split_package`, `download_failure`, `transaction_failure`, `launch_failure_no_success_record`, `repeat_install_preserves_original_state`, `cached_rollback_used`, `corrupt_state`, and restore ordering cases. First order assertion fails on baseline.
- [ ] Implement narrow phase helpers, validation, atomic journal/success state, safe cached rollback reuse and running executable/service checks. Do not execute real system transactions in this worktree stage.
- [ ] Run Python suite, shell syntax/profile check and relevant Meson regressions. Commit and record gate. Real disposable install/update/rollback remains Stage 6 mandatory.

## Follow-on required plans (not optional or complete)

Stage 2: navigation/minimal reveal, bounded persistent width store and accessible controls, Unicode/IME type-to-select, history/selection/scroll state. Stage 3: inspector, safe bounded content pipeline and full Quick Look ownership. Stage 4: real clipboard/DnD/spring loading/conflicts and complete operation matrix. Stage 5: actual SFTP/SMB, mutations, accessibility/scaling/RTL and comparative stress metrics. Stage 6: GitHub CI observed run, fresh full/portal regression, reviewed traceable RPMs, real disposable transactions and safe release handoff. Each gets a focused implementation plan and evidence against the full goal. The overall goal stays active until all explicit requirements are proven.
