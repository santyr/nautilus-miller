# Nautilus Miller — Finder-like Linux File Manager Completion

## Coding-agent mission

Complete `santyr/nautilus-miller` as a dependable, feature-complete everyday Linux file manager with Finder-inspired column browsing. Keep the existing Nautilus architecture and native Linux behavior. Deliver predictable navigation and selection, complete and correctly targeted file operations, an integrated preview experience, polished keyboard and pointer interaction, resilient local and remote browsing, accessibility, and safe maintainable Fedora packaging. Do not stop at a working column demo, passing path tests, or a successful RPM build.

This is an implementation goal and acceptance contract, not permission to replace the user's installed file manager or modify their real files without a separate explicit deployment instruction.

**Audit date:** 2026-10-16.  
**Reviewed fork revision:** `8618e792713ae6b248c4ca7d740ae78ce9d3040b`.  
**Upstream baseline:** Nautilus 50.4, `87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`.  
**Primary target:** the documented Fedora 44 GNOME/Wayland environment.  
**Architecture:** C, GTK4/libadwaita, NautilusFilesView/NautilusListBase, GIO/GVfs, Meson/Ninja, Fedora RPM.  
**Evidence boundary:** this handoff comes from source, tests, design, and verification-log review. The audit did not independently compile, run the GTK application, or validate the installed desktop. Existing passing results below are repository-reported results, not a fresh independent test run.

Re-read the current checkout and compare HEAD with this baseline before implementation. Preserve subsequent fixes and any uncommitted user work. Treat source observations as starting points to reproduce and test, not justification for unnecessary rewrites.

## 1. Product definition and boundaries

The desired result is a Linux-native file manager with Finder's useful interaction model, not a macOS skin. The highest-value behaviors are persistent hierarchical context, single-selection folder disclosure without stolen focus, clear actionable selection, fast file inspection, fluid column resizing, useful history, and reliable drag-and-drop.

Preserve the established decision to implement columns as one real NautilusFilesView per displayed directory, coordinated by NautilusMillerView and hosted in NautilusWindowSlot. Keep the existing Grid and List modes and their functionality. Reuse Nautilus operations, MIME activation, clipboard, undo manager, permissions, directory models, thumbnail services, bookmarks, extensions, and GIO/GVfs backends.

The initial design explicitly deferred a preview pane. This goal expands that scope. It does not reverse the approved separate development identity, native production replacement identity, upstream foundation, or sensible fallback for nonhierarchical locations.

“Full functionality” means every supported operation is either usable and tested in its supported context or explicitly unavailable with a truthful reason. It does not mean every filesystem supports Trash, every remote server supports monitoring, or every format has an installed preview codec.

Do not add a new file-operation engine, shell-based transfer layer, parallel directory indexer, Electron/WebView shell, mandatory background service, or cloud dependency. Do not imitate iCloud, AirDrop, Apple device management, desktop icon management, or macOS application bundles. Colored tags, Smart Folder builders, Gallery/Cover Flow, media editing, and a full custom shortcut editor are separate future projects, not excuses to delay the required core. Preserve existing Nautilus Starred/search functionality; do not market that as arbitrary Finder tag parity.

## 2. Starting point: findings to reproduce or close

### A. Existing strengths

The repository already contains a native third view, parent-preserving folder disclosure, branch highlighting, single-column multiselection, Left/Right navigation, draggable separators, per-column Nautilus action contexts, location/history integration, tabs, and fallback from Columns for search and special locations. These are capabilities to retain, not recreate. [R1–R5]

The verification document reports seven Miller GTK integration cases, three path cases, three deployment-input cases, and 27 of 28 Meson test executables passing. It explicitly records a portal timeout reproduced against the unchanged upstream baseline. It also records successful installed RPM and FileManager1 ShowItems acceptance. Do not reset this history or relabel the portal result as a pass. [R7]

### B. Confirmed missing feature: inline preview

`README.miller.md` explicitly says there is no file preview pane. Implement it as a required capability, distinct from the existing external previewer integration. [R1, R8]

### C. Concrete development-profile preview discovery issue

`meson.build` sets `PROFILE` to `Miller`. `nautilus-previewer.c` initially constructs the service name by concatenating `org.gnome.NautilusPreviewer` and PROFILE. Its fallback tries the canonical service only when the initial name ends in `Devel`; the Miller path falls back to the Devel service instead. The normal canonical service is therefore skipped by this discovery sequence.

Reproduce with only the normal previewer available. Fix provider discovery intentionally and cover production, Miller, Devel, missing-provider, and provider-restart scenarios. Do not rename the production previewer contract or require users to run a development Sushi build. [R8, R9]

### D. Width, scrolling, and accessibility gaps

`column_new()` initializes width to 280. `drag_update()` clamps it to 180–1200 and changes a size request. The strip has no persistent column-width policy, autosize interaction, or keyboard-operable resize control in this implementation. Every `reconcile()` schedules `scroll_to_end()`, rather than revealing the column relevant to the user's action. Left/Right handling uses literal key directions without an explicit RTL policy. These are code observations; confirm their visible effects before changing behavior. [R3]

### E. High-risk integration boundary: action directory versus branch endpoint

The slot location can identify the rightmost opened directory while the actionable file view remains its parent. That is deliberate. Audit every action and context menu against this distinction, including dialogs opened before focus changes. This is not evidence that existing operations are broken; it is where a seemingly small UI change can create a wrong-directory operation. [R2–R5]

### F. Automated coverage does not establish end-to-end interaction parity

The window-operation test activates Copy, then invokes the shared transfer API with a chosen destination URI. Its comment explains the Wayland input-serial limitation. The test proves that transfer path and model update, not real clipboard Paste or a complete user drag. Arrow and resize tests emit controller signals directly. The remote path test constructs SFTP URIs without contacting a server. Preserve these useful tests and add the missing layers. [R6, R10]

### G. Resource and lifetime work needs evidence

The path model builds lexical ancestors up to the GIO root; the strip instantiates each resulting column. The 4096-entry path guard is not a practical resource budget. The path-generation helper exists, but the strip does not itself consult it. Existing upstream cancellation and destruction may already provide protection: inspect those lifetimes and prove stale-result safety rather than assuming a race or adding redundant machinery. [R3, R4]

### H. Deployment ordering and recovery need hardening

`install()` invokes the package transaction before `close_application()`; `restore()` closes the app first. Move the installation workflow to a clearly tested preflight/drain/transaction/activate/verify sequence. Preserve the normal Quit-and-wait behavior; never force-kill active file operations. The current three Python deployment tests check missing inputs/state, not transaction sequencing or recovery. [R11, R12]

### I. Small known usability defect

The verification log records that the Grid-mode toggle advertises List while its next mode is Columns. Resolve the icon, tooltip, action target, accessible name, and shortcut documentation together. [R7]

## 3. Nonnegotiable state and ownership rules

Write down the interaction contract before adding features. Maintain these concepts separately even if some remain implemented through existing objects rather than a new controller class:

| Concept | Meaning |
|---|---|
| Branch endpoint | The directory represented by the end of the opened chain and the location/history model. |
| Active column | The column whose selection and backing directory own ordinary file actions. |
| Keyboard focus | The actual focused list, row, entry, menu, dialog, or preview control. |
| Branch marker | The ancestor row showing the route into a displayed child, not an extra action selection. |
| Actionable selection | The selected files in one action-owning directory. |
| Preview target | The active selection snapshot, which does not execute or navigate by itself. |
| Request identity | The owning tab/column and generation or equivalent lifetime protection for delayed work. |

At most one file-action context per active slot/window may own normal toolbar/keyboard operations. Old tabs and discarded columns must not steal it. Context-menu and drag destinations must remain the objects selected when those interactions began, not whichever column becomes active later.

Folder disclosure must not steal row focus. Preview must not steal file-list focus unless the user enters its controls. Programmatic selection restoration and external reveal requests must not launch files. Branch restoration must not overwrite a newer user multiselection.

Do not build a large new state framework merely to name these concepts. Prefer small well-owned additions and explicit invariants at the existing integration boundaries.

## 4. Required interaction behavior

### Folder and file browsing

Single-select a folder: reveal its children in the next column, discard only the unrelated descendant branch, preserve the selected row and parent focus. Select a sibling folder: replace the descendant branch without disturbing earlier columns. Single-select a regular file: select it and update preview, but never launch it. Multiple selection stays within one directory and produces no arbitrary child branch; preview becomes a selection summary or navigable selection set.

Right enters the selected folder's existing or newly loaded child column. Left returns to the preceding column and reveals the selected branch row. Up/Down, Home/End, Page Up/Down, Shift selection, Ctrl selection, Select All, and context-menu keyboard access remain coherent. Empty and denied columns must still have a usable keyboard path out.

Retain native Nautilus defaults: Enter activates, F2 renames, Ctrl-based editing/clipboard shortcuts, Ctrl+L edits the location, and native history/parent/tab shortcuts. Do not globally remap Super or change Enter to rename merely to mimic macOS. Scope handlers so entries, rename controls, menus, dialogs, preview controls, and input methods keep their own key behavior.

Implement Finder-like type-to-select within the active column as a discoverable preference, enabled for Columns by default. Use an explicit search shortcut/action for recursive/content search. Provide an option to retain stock type-to-search behavior. Verify Unicode input and IME composition; do not implement a byte-oriented ASCII-only key buffer. Limit this change to Columns unless a separately documented setting says otherwise.

Preserve middle-click and modifier-based new-tab/new-window activation. A regular-file activation must launch once, not once through the child list and again through the strip.

### History, navigation entry points, and state

Keep the approved lexical ancestry to the GIO root as the logical path policy. Do not silently canonicalize symlinks or cross URI authorities. An optional collapsed ancestor prefix may reduce visual clutter, but every ancestor remains accessible and the default change must be documented.

Back/Forward restores meaningful directory, selected item when still present, active column, and scroll state. Focus-only changes, preview changes, passive monitor updates, and automatic selection restoration must not add history entries. Deliberate branch navigation follows a documented policy; avoid duplicate entries and feedback loops. Record how continuous keyboard folder browsing is handled.

Exercise breadcrumbs, sidebar bookmarks, Home, mounted volumes, Ctrl+L with absolute paths and URIs, command-line locations, open-in-new-tab/window, FileManager1 ShowFolders/ShowItems/ShowItemProperties, and view switching. File selection must survive mode switching when it belongs to the displayed location. Do not attach a parent's selection to its child's listing.

Tab switching must retain each tab's state independently. Where upstream already restores windows/tabs, include Miller state without creating a second contradictory session system. Newly required width and preview preferences must survive application restart. Store app-specific state in the existing settings/metadata mechanism or an appropriate bounded XDG user-state store, never `.DS_Store`-like files inside browsed directories.

### Column layout

Allow drag resizing, a discoverable fit-to-content action, reset-to-default, and a keyboard-accessible resizing path. An autosize operation must not enumerate or measure an enormous directory synchronously. Persist a default width and bounded per-location overrides; define eviction and remote-URI handling. Keep directory width independent of preview width.

Replace unconditional far-right scrolling with minimal reveal of the target column or relevant pair. Navigating back to an ancestor must make it visible; background completion must not snap the viewport away from what the user is inspecting. Preserve intentional horizontal scrolling during updates.

Use native GTK/libadwaita styling, readable row density, ellipsized long names with full-name access, understandable root/volume labels, visible focus, distinct inactive branch highlights, and a correctly labeled three-mode selector. Check small windows, large text, fractional scaling, light/dark, high contrast, and RTL. Do not claim pixel-identical Finder appearance as acceptance.

## 5. Required preview experience

Provide two complementary features.

**Inline preview/inspector:** a toggleable, resizable region adjacent to the active branch. Prefer a stable right-side pane outside the directory model so preview changes do not change path/history or create a fake directory. It follows the active actionable selection rather than blindly following the rightmost directory. For folders, show useful metadata without recursively counting whole trees on every selection. For multiple files, show count, available aggregate metadata, and an optional navigation control; never fabricate totals when not known.

**Quick Look-style preview:** Space invokes the existing external preview integration; Space/Escape closes or returns naturally according to the provider's supported contract. Verify navigation between files, tab ownership, returning focus, missing-provider behavior, and service restarts. Reuse the existing previewer where possible rather than embedding its code or assuming its window can become an inline widget.

For inline preview, require useful native previews for common images and bounded text/source, a thumbnail/first-page representation and external open path for PDFs, and poster/metadata plus a working external preview path for supported audio/video. Use a truthful type/icon/metadata fallback for unsupported formats. Richer document/media rendering may use maintained providers, but must not turn this project into a new document suite.

Prefer already available safe image/thumbnail infrastructure; the baseline already declares glycin dependencies. Every new decoder/dependency needs a documented purpose, supported package version, isolation approach, and resource limits. Selecting a file must never execute it, run shell commands built from its name, fetch embedded external content, recursively extract an archive, or autoplay audio.

All preview work is asynchronous, cancellable or detached safely, and bounded. A late preview from file A must not replace the currently selected file B. Avoid blocking on remote metadata, downloading an unlimited remote file, or reading FIFOs/devices as ordinary text. Show explicit loading/error/unsupported states. Carry through upstream privacy and thumbnail preferences; support a no-remote-content-preview policy. No credential-bearing URIs in logs or persistent view-state keys.

## 6. Full operation parity, not a replacement engine

Create a capability matrix comparing stock Grid/List behavior with Columns. Cover Open/Open With, new folder and templates, rename and multi-rename, copy/cut/paste, duplicate, move/copy-to, links where supported, Trash/restore/permanent deletion, undo/redo, Properties/permissions, compress/extract, Open in Terminal, bookmarks, and installed extension/context-menu actions.

For each capability record the source view, actionable selection, destination resolution, availability rule, actual Nautilus API/action used, error/cancel behavior, and test. Implement missing presentation/integration, not new file-transfer semantics. Preserve upstream behavior for metadata, links, conflicts, progress, cancellation, permissions, and undo.

Mandatory wrong-target tests: Paste into a focused parent with a child still open; New Folder in an empty non-rightmost column; right-click in an inactive column; context-menu action after another view changes; toolbar action while keyboard focus is outside the listing; switching tabs during an operation; deleting/renaming the open branch directory. Verify filesystem results as well as UI state, including untouched sibling directories.

Conflict handling must remain native. Cover replace, merge where supported, rename/keep-both, skip, cancel, and partial failure. Do not promise all moves are atomic, every operation is undoable, or Trash is available on every backend. Never silently fall back from Trash to permanent deletion. Use disposable fixtures and verify contents/checksums, not just filenames.

## 7. Drag-and-drop and spring-loaded browsing

Reuse the Nautilus/GTK drag source and destination machinery; existing NautilusListBase already contains folder-hover logic. Determine what it does in a multi-directory strip before adding a competing implementation.

Require drags within a column, between visible columns, into folder rows, onto an empty column background, between tabs/windows, to bookmarks/volumes where supported, and to/from other applications. Honor native move/copy/link negotiation and capability feedback. The destination under the pointer wins, independently of the active action column.

Folder hover opens a navigable child without corrupting the source selection or ending the drag. Test nested spring-loading, moving back to ancestors, canceled drags, source removal, and destination disconnect. Define which temporary hover-opened branches are retained after a completed drop and which are restored on cancellation. Provide vertical and horizontal edge scrolling without oscillation or accidental navigation. A pointer drag and a selection gesture must not fight each other.

## 8. Filesystem, remote, and error behavior

Use GFile identities/URIs and Nautilus's existing directory/file objects. Do not require every object to have a local filesystem path. Respect URI authorities, encoded names, symlinks, case-sensitive naming, hidden files, inaccessible ancestors, mount roots, removable storage, and differing backend capabilities.

Run actual SFTP and SMB fixtures for supported environments, including authentication cancellation, slow enumeration, disconnect/reconnect, and unsupported monitoring. A URI-construction unit test is not remote integration coverage. When monitoring is unavailable, use an honest refresh/retry path rather than silently presenting stale content as live.

Test external create/delete/rename/move events, including renaming or removing an ancestor while a child is displayed. Update or invalidate the branch deliberately; keep still-accessible ancestors usable and prevent operations against stale locations. Error, empty, loading, offline, and permission-denied states must be distinguishable within the affected column and offer useful retry/navigation actions.

Search, Recent, Starred, Trash, and the Network overview remain appropriate native flat/special views. Selecting Columns there must not fabricate an ancestry or destroy the saved ordinary-folder preference. Returning to hierarchical browsing restores Columns. Test search cancellation and restoration of the prior browsing selection/state. Treat mounting a real network location differently from browsing the special Network overview.

The portal file chooser is not required to become a Miller interface. It is required not to regress. Keep the known baseline timeout visible until independently resolved; add a real portal smoke test in an environment capable of exercising it.

## 9. Responsiveness, lifetime, accessibility, and privacy

Do not synchronously enumerate directories, query remote metadata, decode previews, or calculate recursive sizes on the GTK main loop. Respect existing Nautilus cancellation/monitor lifetimes. Extend them only where new ownership requires it. Every new idle, timeout, frame callback, signal, monitor, cancellable, and weak reference must have an identified owner and cleanup path.

Build reproducible local fixtures for empty/small directories, 10,000 entries, 100,000 entries, a 30-level chain, long names, mixed Unicode, symlink cycles, unreadable locations, and rapid branch replacement. Use warm/cold runs and compare against the same upstream baseline on the same host. Report time to first useful rows, input latency, CPU, peak/settled RSS, file descriptors, and live monitor/column counts.

Target immediate visible selection/loading feedback under ordinary local conditions (approximately 100 ms at the 95th percentile on the reference host), not completion of remote I/O within an arbitrary time. Keep the UI responsive during slow operations. A branch-switch/close loop must return resource counts near a documented bounded-cache baseline rather than growing linearly. Label these as project targets, not already measured performance.

Prioritize visible/active columns and constrain unnecessary ancestor work. GTK row virtualization is not the same as limiting how many directory models are loaded. Profile before introducing column virtualization or large architectural changes.

Test keyboard-only use and an actual accessibility tree/screen reader: column identity, location, selected and focused states, loading/errors, directory affordances, preview controls, and resize controls. Do not rely only on color. Confirm RTL direction, Unicode display, high-contrast readability, enlarged fonts, and reduced-motion behavior where applicable.

Use disposable files and local services. Do not upload the user's directory names, contents, previews, credentials, or browsing history for testing or diagnostics.

## 10. Development, CI, packaging, and rollout

Keep `org.gnome.Nautilus.Miller` isolated during development. Verify its settings, launcher, resources, schema paths, and nonownership of canonical FileManager1. Test with stock or installed replacement Nautilus running simultaneously. Preserve intentional shared GTK desktop preferences rather than claiming perfect isolation where the design does not provide it.

Retain the canonical production app, executable, desktop entry, FileManager1, extension library, and portal contracts in the replacement RPM. Keep matching main/extension/devel packages consistent. Increment the release for changed artifacts and record source revision, build inputs, package metadata, checksums, and test results.

Deployment phases must be explicit: validate package set and host; save/verify rollback data; obtain authorization; drain the running application safely; perform the tracked package transaction; update compatible user settings; launch; verify running executable/package ownership and D-Bus/default-handler routing; record success. On any failure, report the exact phase and a safe recovery path. Do not erase original rollback information, force-kill transfers, blindly consume an offline update transaction, or mark installation successful after a failed launch.

Use verified cached rollback RPMs when available. Test interrupted download, wrong/mismatched package, transaction failure, application refusal to exit, corrupt/missing state, repeated installation, update from an existing Miller release, restore, and failure after package replacement but before activation. Mock external commands for deterministic ordering tests, then exercise real transactions only in a disposable system or explicitly authorized host.

Wire the relevant build/tests into the repository's actual CI platform. The tree reviewed contains inherited GitLab configuration but no GitHub Actions workflow; establish an effective GitHub CI path or document an intentionally configured external CI provider. Do not equate a workflow file with successful CI. Run unprivileged GUI tests in an appropriate session and clean settings, never rely on root permission-denied tests. Keep baseline failures separately tracked and visible, not silently skipped.

Do not freeze Fedora security updates indefinitely to retain the fork. Document reviewed upstream rebases, packaging refreshes, extension compatibility, and repeating acceptance after updates.

## 11. Work packages and gates

Implement in reviewable stages. Each stage must leave a usable application. For a defect, first add a focused failing regression, confirm the failure, make the smallest correction, rerun it and relevant upstream tests, then commit with evidence. New functionality needs acceptance tests before being declared complete.

### Stage 0 — Baseline and executable contract

Read the existing design, implementation plan, README, verification log, and relevant upstream code. Record HEAD, local changes, dependencies, installed build identity, and existing results. Produce the capability matrix and state-transition contract. Run the existing test commands below, preserving baseline failures. Add reproducible fixtures and identify the real input harness. Do not touch the production installation.

### Stage 1 — Action targeting and immediate integration defects

Reproduce/fix the preview service-name fallback and misleading view toggle. Verify one active action owner, context-menu ownership, entry-safe key handling, and stale column cleanup. Establish wrong-directory and cross-tab tests before further UI changes. Audit installation ordering and add command-sequence tests.

### Stage 2 — Navigation, layout, and persistent state

Implement minimal reveal scrolling, width policy/autosize/keyboard resize, native shortcuts, scoped type-to-select, selection restoration, and independent tab/history state. Preserve the lexical root policy. Test rapid selection and asynchronous branch changes with intentionally delayed completions, not just fast local paths.

### Stage 3 — Preview and inspection

Implement the inline inspector and safe bounded previews. Integrate/fix external Quick Look behavior in both build profiles. Test unsupported, missing-provider, stale-result, huge-file, remote, malicious-name, and tab-close cases. The preview must not change the action directory or execute a file.

### Stage 4 — File operations and real drag/clipboard workflows

Close every required parity-matrix gap. Exercise actual copy/cut/paste and drag-and-drop with genuine session input. Add spring-loading, cancellation, scrolling, conflicts, undo/redo, empty-column operations, and external application integration tests. Do not count direct transfer API tests as completing this stage.

### Stage 5 — Remote/error resilience, accessibility, and performance

Run real local SFTP/SMB fixtures, monitors, disconnects, permission errors, and ancestor mutations. Complete keyboard/screen-reader/scaling/RTL checks and reproducible stress benchmarks. Fix leaks and event-loop stalls. Capture screenshots and accessibility evidence using disposable filenames.

### Stage 6 — Release candidate and safe rollout handoff

Run fresh full regressions and a real portal smoke test where possible. Build traceable RPMs and exercise install/update/rollback in a disposable system. Produce release notes, artifact checksums, known limitations, and explicit deployment/rollback commands. Leave the user's installed manager untouched unless deployment has been explicitly authorized. Do not silently turn this last stage into an unmanaged `/usr` install.

## 12. Source map for the implementer

| Area | Existing code to inspect and modify narrowly |
|---|---|
| Branch/column coordination | `src/nautilus-miller-view.[ch]` |
| Row presentation, sort/zoom | `src/nautilus-miller-column.[ch]` |
| URI ancestry and request identity | `src/nautilus-miller-path.[ch]` |
| Selection and file-action context | `src/nautilus-files-view.[ch]` |
| Location/history/tabs/search/view fallback | `src/nautilus-window-slot.[ch]` |
| Gestures, clipboard, transfers | `src/nautilus-list-base.c`, `src/nautilus-files-view-dnd.c`, `src/nautilus-dnd.c`, `src/nautilus-clipboard.c` |
| External preview | `src/nautilus-previewer.[ch]` |
| Settings and UI | `data/org.gnome.nautilus.gschema.xml`, `src/nautilus-global-preferences.[ch]`, `src/nautilus-view-controls.c`, `src/resources/ui/nautilus-view-controls.blp`, `src/resources/ui/shortcuts-dialog.blp`, `src/resources/style.css` |
| Existing regression entry points | `test/automated/display/test-miller-view.c`, `test/automated/displayless/test-miller-path.c`, `test/automated/display/test-files-view.c` |
| Build/profile/deployment | `meson.build`, `data/meson.build`, `tools/miller/*`, `packaging/fedora/nautilus-miller.spec`, `test/miller/test-deployment.py` |

A separate small `nautilus-miller-preview.[ch]` and matching UI resource are reasonable new units if the implementation warrants them. A new per-tab state unit is reasonable only if it simplifies ownership. These are proposed file boundaries, not existing interfaces. Avoid putting preview decoding, persistent-state serialization, and all interaction logic into the strip's existing C file.

## 13. Acceptance scenarios that must have evidence

| ID | Scenario | Required result |
|---|---|---|
| C01 | Select folder A, then sibling B before A finishes loading | B remains the branch; A cannot reappear or steal selection/focus. |
| C02 | Parent selected, child visible; paste and create folder in parent | Only parent is modified; the child and unrelated siblings are untouched. |
| C03 | Focus an empty child; paste/create, then move Left | Correct child destination; new items appear; ancestor remains reachable. |
| C04 | Context menu in inactive column while endpoint differs | Menu actions target the clicked context, including after intervening focus changes. |
| C05 | Real Copy, Cut/Paste, and undo/redo across columns | Correct contents and source retention/removal; native error/cancel behavior. |
| C06 | Drag through two spring-loaded folders, then cancel | No unintended filesystem changes; drag state and documented branch state restored. |
| C07 | Select file A then B while A's preview is delayed | B owns preview; no launch, stale replacement, focus theft, or unbounded read. |
| C08 | Space with only normal previewer installed; repeat in Miller profile | Usable preview in both profiles, or truthful missing-provider message; safe focus return. |
| C09 | Deep chain, scroll left, change focus/resize, Back/Forward, switch tabs | Relevant column remains visible; per-tab state and selection stay coherent. |
| C10 | External rename/delete of displayed ancestor; remote disconnect | Branch updates or invalidates safely; usable ancestors and retry remain. |
| C11 | Search/Recent/Starred/Trash/Network, then return to an ordinary directory | Native special view works; Columns preference and browsing state are preserved. |
| C12 | FileManager1 reveals the final row in a long directory | Correct parent, selected item visible, accurate OpenLocations, no unintended activation. |
| C13 | Keyboard-only/screen-reader/RTL/high-contrast/large-text operation | Named controls and clear state; resize/preview/navigation usable without pointer or color alone. |
| C14 | 10k/100k directories, deep paths, repeated branch/tab lifecycle | Responsive input and bounded steady-state resources, with comparative measurements. |
| C15 | Install/update/restore and injected failures in a disposable host | Safe phase ordering, preserved rollback, compatible settings, correct package/service identity. |
| C16 | Existing Grid/List, extensions, portal chooser | No new regression; baseline-only failures documented separately with fresh evidence. |

Tests that need a graphical session or external service must be distinguishable from unit tests. A missing environment is a blocked test, not a passing one. Use compositor-supported or accessibility-based real input where possible; supplement with documented manual acceptance when required. Screenshots alone do not prove file-operation correctness.

## 14. Existing commands and evidence format

The README currently documents:

```sh
./tools/miller/build-dev.sh
./tools/miller/run-dev.sh "$HOME"
python3 tools/miller/check-profile.py build-miller
python3 test/miller/test-deployment.py
XDG_CONFIG_HOME="$(mktemp -d)" meson test -C build-miller --print-errorlogs
```

Revalidate these against current HEAD and the actual session. Keep a temporary test configuration path under your control and clean it up after collecting evidence. Discover specific Meson test names from the configured build rather than inventing names. Run build-profile and relevant upstream tests after changes to shared Nautilus code. Container/headless checks do not replace a real Wayland session.

For each capability report: requirement ID, source revision, test or manual procedure, environment, pass/fail/blocked result, artifact/log path, and outstanding limitation. For every fixed regression include before/after evidence. For performance include fixture sizes, hardware, build type, cold/warm conditions, and measured values. For packages include NEVRA, source revision, checksums, and actual transaction results.

## 15. Required deliverables and definition of done

Deliver working reviewed code in small commits; an up-to-date capability/acceptance matrix; the interaction/state contract; focused and end-to-end tests; real-session and remote/backend evidence; preview/security/resource-limit documentation; comparison benchmarks; accurate keyboard/UI/user documentation; CI results; traceable RPMs; and a tested installation/update/rollback runbook. Add a completion report that separates shipped behavior from unsupported capability, environment-blocked checks, inherited baseline failures, and optional future work.

Suggested documents: `docs/miller/finder-parity.md`, `docs/miller/interaction-contract.md`, `docs/miller/acceptance.md`, `docs/miller/performance.md`, and updated `docs/miller/verification.md`. Keep the existing design/plan as historical records and link the new goal rather than rewriting history. Add focused implementation plans under `docs/superpowers/plans/` as required by the repository workflow.

Completion requires the core interaction and operation acceptance scenarios to work in the development build and the release candidate; no known wrong-directory or data-loss regression; useful inline and external preview; real clipboard/drag verification; native special-location fallback; resource/lifetime safety; accessible controls; and a safe release/rollback path. A core blocker must remain clearly open, not be downgraded into an optional feature to claim success.

Work through the stages without stopping after scaffolding or asking the user to restate preferences already established here. Make narrow reversible choices when unspecified and record them. Stop for authorization before host installation or risky operations on real user data. Never say “fully functional” solely because the app launches or a small synthetic test suite passes.

## Reference register

Repository references below are anchored to the reviewed revision stated at the top. The coding agent should read current versions and compare changes before relying on them.

- **R1:** `README.miller.md` — user behavior, supported profiles, explicit absence of preview pane, commands and maintenance.
- **R2:** `docs/superpowers/specs/2026-10-16-miller-columns-design.md` — accepted scope and action/endpoint architecture.
- **R3:** `src/nautilus-miller-view.c` — selection_idle, key_pressed, column_new, drag_update, reconcile, scroll_to_end, lifecycle.
- **R4:** `src/nautilus-miller-path.c` — lexical ancestors, cycle/size guard, generation API.
- **R5:** `src/nautilus-files-view.c`, `src/nautilus-window-slot.c`, integration commit `695231319d65cd28f3eada6327fed502554cd2c9` — managed action ownership, history/search/fallback.
- **R6:** `test/automated/display/test-miller-view.c` — seven cases and synthetic-input/transfer coverage boundaries.
- **R7:** `docs/miller/verification.md` — reported tests, baseline portal timeout, toolbar mismatch and installed acceptance.
- **R8:** `src/nautilus-previewer.c` — service discovery, PROFILE behavior and external preview integration.
- **R9:** `meson.build` — upstream version, dependencies, profile and application/schema identities.
- **R10:** `test/automated/displayless/test-miller-path.c` — lexical URI tests, not real remote I/O.
- **R11:** `tools/miller/deploy.py` — package/state handling and installation/restore sequence.
- **R12:** `test/miller/test-deployment.py` — three missing-input/state tests.

External behavioral/API references consulted: Apple Support, “Change how folders are displayed in the Finder on Mac” and “View and edit files with Quick Look on Mac”; GNOME Help, “Preview files and folders”; GIO documentation for `g_file_enumerate_children_async()` and `g_file_monitor_directory()`. These inform interaction and platform constraints, not a claim that this fork already implements them. The detailed targets and acceptance criteria in this document are project recommendations, not requirements quoted from those sources.