# Nautilus Miller Columns Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a Nautilus 50.4 fork with Finder-style Miller columns, publish it to the user's GitHub repository, and install it as this Fedora host's default file manager.

**Architecture:** A window-slot controller composes one existing `NautilusFilesView` per directory, using a new compact `NautilusListBase` subclass for each column. It keeps the branch endpoint, focused action context, and ancestor highlights separate. A pure path-state object handles chain reconciliation; each existing file view retains its own asynchronous directory/model/operation lifecycle.

**Tech Stack:** C11, GTK 4, libadwaita, GLib/GIO, Nautilus directory and file models, Blueprint, Meson/Ninja, Fedora RPM tooling, Git/GitHub.

**Spec:** `docs/superpowers/specs/2026-10-16-miller-columns-design.md` (approved, with the user's OS X interaction clarification incorporated).

## Global Constraints

- Base on Nautilus 50.4, upstream commit `87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`; preserve upstream licensing and authorship.
- Development application ID: `org.gnome.Nautilus.Miller`; development prefix: `$HOME/.local/opt/nautilus-miller`; launcher: `nautilus-miller`.
- Replacement profile retains the standard Nautilus executable, application ID, desktop entry, settings paths, and D-Bus service contracts. Install it through Fedora RPM/DNF.
- Grid and List remain available; Columns becomes this user's default after replacement installation.
- Single directory selection reveals children without stealing focus. Regular file selection does not launch the file. Multiple selection stays within one column.
- Back/Forward and breadcrumbs follow the branch endpoint; file actions follow the focused or context-clicked column.
- Ordinary hierarchical GIO locations support Columns; Search, Recent, Starred, Trash, and the special Network overview use existing views.
- Directory loading stays asynchronous; discarded columns cannot be restored by delayed callbacks.
- Use draggable separators and retain surviving column widths during a session. No additional preview pane is included in this version.
- Dependencies are installed as of 2026-10-16. Confirm the full Meson configure before claiming build readiness.

## Review Focus

- A context click in an inactive ancestor must operate on that ancestor, even when the branch endpoint is several columns to its right (Tasks 3–5).
- A delayed load, selection notification, or error from a discarded branch must not change the current branch or retain a closed tab (Tasks 2 and 4).
- Hidden or unusually named path components, symlinked ancestors, and remote URI roots must preserve file identity and stop at the correct GIO root (Tasks 2 and 4).
- Search, view switching, tab switching, and closing the last column must not leave the window's `view` action group pointing at an obsolete file view (Tasks 3 and 5).
- Development execution must not claim stock FileManager1 services or write stock view preferences; replacement installation must claim the canonical services (Tasks 1 and 6).

---

## File Map

- `src/nautilus-miller-path.[ch]`: URI chain reconciliation and generation state, without GTK widgets.
- `src/nautilus-miller-column.[ch]`: compact single-directory `NautilusListBase` presentation.
- `src/nautilus-miller-view.[ch]`: horizontal strip, column ownership, focus, branch selection, and resizing.
- `src/nautilus-files-view.[ch]`: explicit active-context control and compact presentation registration; retain existing per-directory actions.
- `src/nautilus-window-slot.[ch]`: strip lifecycle, navigation/history, active file-view bindings, and special-location fallback.
- `src/nautilus-view-info.h`, `src/nautilus-types.h`, `src/meson.build`, view controls/menu/shortcuts: register and expose Columns.
- Meson configuration, `data/meson.build`, schemas/desktop/services, preferences initialization: development identity and replacement profile.
- `test/automated/displayless/test-miller-path.c`, `test/automated/display/test-miller-view.c`, existing files-view tests: behavioral verification.
- `tools/miller/`: development build/launch and deployment scripts.
- `packaging/fedora/`: Fedora source spec adaptation and reproducible RPM instructions.
- `README.miller.md`: user-facing build, usage, maintenance, and rollback instructions.

### Task 1: Reproducible build and isolated development identity

**Files:** Modify `meson.build`, `meson_options.txt`, `data/meson.build`, `data/org.gnome.Nautilus.desktop.in.in`, `src/nautilus-global-preferences.c`, `src/nautilus-freedesktop-dbus.c`; add `tools/miller/build-dev.sh`, `tools/miller/run-dev.sh`, and `README.miller.md`.

**Interfaces:** The existing `profile` option accepts `Miller` in addition to its existing profiles; `-Dprofile=Miller` selects the development identity. The default profile remains suitable for Fedora replacement packaging. Scripts accept a location argument and quote paths correctly.

- [ ] Configure and compile untouched upstream as a baseline: `meson setup build-baseline -Dtests=all` and `meson compile -C build-baseline`. Record actual failures before modifying code; resolve missing dependencies with the user-supplied installation workflow.
- [ ] Add profile-specific application naming, icon reuse, executable paths in desktop entries, and generated Nautilus schema IDs/paths. Development Nautilus schemas use `org.gnome.nautilus.miller.*` and `/org/gnome/nautilus/miller/`; canonical schema names remain in the replacement build. Leave GNOME/GTK external schema references canonical.
- [ ] Scope development service installation and runtime bus ownership to the development identity. Development must not install `org.freedesktop.FileManager1.service` or claim that canonical name; retain canonical behavior in the replacement profile.
- [ ] Add build and launch scripts that configure `build-miller` with `-Dprofile=Miller -Dtests=all`, use the development prefix, and find the built/installed resources, schemas, and libraries. Keep package installation outside these scripts.
- [ ] Verify both configurations complete and compile. Use `glib-compile-schemas --strict` on the generated schema directories. Inspect generated desktop/services and compare development/canonical application IDs and settings paths. Run the configured existing headless suite once.
- [ ] Commit the isolated build support and document its actual commands and baseline outcome.

### Task 2: Column path reconciliation and stale-branch protection

**Files:** Add `src/nautilus-miller-path.[ch]`, `test/automated/displayless/test-miller-path.c`; modify `src/meson.build` and `test/automated/displayless/meson.build`.

**Interfaces:** `NautilusMillerPath` is a GObject. `nautilus_miller_path_new(void)` creates it; `guint nautilus_miller_path_set_location(NautilusMillerPath *self, GFile *location)` returns the retained common-prefix length; `guint nautilus_miller_path_get_n_columns(NautilusMillerPath *self)`, `GFile *nautilus_miller_path_get_column(NautilusMillerPath *self, guint position)` (borrowed), and `guint64 nautilus_miller_path_get_generation(NautilusMillerPath *self)` expose state. `gboolean nautilus_miller_path_is_current(NautilusMillerPath *self, guint64 generation)` rejects stale work.

- [ ] Write tests for `/a/b` producing `/`, `/a`, `/a/b`; replacing it with `/a/c` retaining two columns; and an identical location preserving generation. Use URI identity, not raw string-prefix matching.
- [ ] Add remote-root tests using `sftp://example.test/a/b`, Unicode/space/percent-encoded names, lexical symlink paths, and a root with no parent. Constructing a chain must not perform network I/O or canonicalize through symlinks.
- [ ] Run the new Meson test and confirm it fails because the new API is absent, then implement the path object using `GFile` parent/equality operations. Bound parent traversal and reject repeated identities defensively.
- [ ] Assert that replacing a branch invalidates an earlier generation, while revisiting the same endpoint does not invalidate unchanged state. Run `meson test -C build-miller test-miller-path --print-errorlogs` and require all assertions to pass.
- [ ] Commit the path object and tests.

### Task 3: Compact column renderer and explicit file-action context

**Files:** Add `src/nautilus-miller-column.[ch]`; modify `src/nautilus-files-view.[ch]`, `src/nautilus-list-base.[ch]` only where shared interaction requires a policy hook, `src/nautilus-view-info.h`, `src/nautilus-types.h`, `src/meson.build`, and `test/automated/display/test-files-view.c`.

**Interfaces:** Reserve `NAUTILUS_VIEW_MILLER_ID` as `4` (Network remains `3`). `NautilusMillerColumn *nautilus_miller_column_new(void)` constructs the compact `NautilusListBase` subclass. Add `void nautilus_files_view_set_managed_context(NautilusFilesView *self, gboolean managed)` and `void nautilus_files_view_set_action_context(NautilusFilesView *self, gboolean active)` so only the controller-selected file view owns the window's `view` actions. Existing single-view callers retain automatic slot activation.

- [ ] Extend files-view construction tests to instantiate view ID 4, verify it exposes one directory/model, and verify switching action context between two views changes the root action group exactly once.
- [ ] Run the focused test to observe the missing mode/API, then implement a compact `GtkListView` presentation using `NautilusNameCell`, existing cell helpers, sort/filter model, and per-directory loading. Include a trailing folder indicator, ellipsized names, and existing list zoom levels. Implement the required `NautilusListBase` methods.
- [ ] Ensure plain file selection never activates a file in this presentation, including when the global Nautilus click policy is single-click. Directory reveal will be selection-driven by Task 4; normal double-click/Enter activation and modified open actions remain available.
- [ ] Refactor slot activation into explicit action-context activation for managed columns. Inactive-column destruction must not remove another column's root action group. Expose local `view` actions on each file view so pointer-local operations have the correct target after focus/context activation.
- [ ] Test managed/unmanaged activation, two-column context switching, switching tabs, disposing an inactive column, empty-directory rendering, and existing grid/list zoom/construction behavior. Run the targeted display tests under an available display or a private Xvfb/D-Bus test session.
- [ ] Commit the compact renderer and context API.

### Task 4: Finder-style strip, loading lifecycle, and keyboard behavior

**Files:** Add `src/nautilus-miller-view.[ch]`, `test/automated/display/test-miller-view.c`; modify `src/meson.build`, `test/automated/display/meson.build`, and `src/resources/style.css` (already included by `src/resources/nautilus.gresource.xml.in`). Register the display test with the same LocalSearch sandbox configuration as `test-files-view`.

**Interfaces:** `NautilusMillerView` derives from `AdwBin`. `NautilusMillerView *nautilus_miller_view_new(NautilusWindowSlot *slot)`; `void nautilus_miller_view_set_location(NautilusMillerView *self, GFile *location, NautilusFileList *selection)`; `NautilusFilesView *nautilus_miller_view_get_active_view(NautilusMillerView *self)` (borrowed); `GFile *nautilus_miller_view_get_location(NautilusMillerView *self)` (borrowed endpoint); `void nautilus_miller_view_set_active(NautilusMillerView *self, gboolean active)`; `void nautilus_miller_view_stop_loading(NautilusMillerView *self)`; `void nautilus_miller_view_reload(NautilusMillerView *self)`. Signals: `active-view-changed(NautilusFilesView *)` and `location-requested(GFile *)`.

- [ ] Write a display test using temporary `A/child`, `B/child`, an empty folder, and a regular file. Assert selecting A appends A's column while retaining focus/selection in its parent; selecting B replaces only the descendant branch; selecting the file launches nothing and removes descendants.
- [ ] Add tests for multi-selection remaining in one column, direct deep locations selecting ancestors after loading, and programmatic ancestor highlights not recursively opening branches. Run to confirm missing controller behavior.
- [ ] Implement the strip using `NautilusMillerPath`, one managed `NautilusFilesView` per column, horizontal scrolling, and drag separators. Keep retained columns/models/selection/widths; discard only the changed suffix. Use initial width 280 logical pixels and minimum width 180. Bring a revealed column into view after allocation, with no keyboard-focus transfer on selection.
- [ ] Track focused column independently from endpoint. Capture focus and context presses before row actions, then set the managed action context. Right enters/focuses the selected child; Left focuses the parent with its branch highlight intact. Directory activation focuses or requests the child; modified activation delegates to existing Nautilus behavior.
- [ ] Attach generation/column identity checks to delayed controller work. Cancel loading, disconnect handlers, and release references when removing columns. Reuse each file view's directory monitors and error UI. An inaccessible column must leave ancestor widgets operational; provide retry or return to an ancestor.
- [ ] Test rapid A→B replacement with delayed notifications, removal of the displayed directory, selection of hidden path components, a denied directory, teardown with work pending, and keyboard focus boundaries. Test resizing preserves retained widths and scrolling reaches the new column. Require the new test executable to pass with GLib criticals treated as failures.
- [ ] Commit the strip and lifecycle handling.

### Task 5: Window navigation, visible view selection, and file-operation integration

**Files:** Modify `src/nautilus-window-slot.[ch]`, `src/nautilus-files-view.[ch]` where activation routing requires it, `src/resources/ui/nautilus-view-controls.blp`, `src/resources/ui/shortcuts-dialog.blp`, `data/org.gnome.nautilus.gschema.xml`, and display tests. Existing view accelerators are registered in `src/nautilus-window-slot.c`; the toolbar sort menu's assumed position must be preserved when adding the view selector.

**Interfaces:** Add `NautilusMillerView *miller_view` ownership to the slot. `content_view` denotes the current actionable file view; the slot location denotes the branch endpoint. Factor `static void bind_content_view(NautilusWindowSlot *self, NautilusFilesView *view)` to disconnect/rebind selection, loading, search, and menu properties without changing ownership of the strip. Use Task 4's controller API; keep the public window-slot content-view getter returning the actionable file view.

- [ ] Write integration tests for Columns selection, navigation to a deep location, Back/Forward, breadcrumbs, modified Open in New Tab, tab switching, and Columns→Grid→List→Columns at the same endpoint. Assert the appropriate file selection is preserved when it exists in the destination view.
- [ ] Add Grid/List/Columns choices to the view menu, validate ID 4 throughout view handling, and expose Columns as Ctrl+3 alongside Ctrl+1/Ctrl+2. Store ID 4 only in the fork schema. Keep the original two modes' shortcuts and behavior valid.
- [ ] Host the strip in the slot's content container. Route pending navigation and selection through `nautilus_miller_view_set_location`; route focus/context changes through `bind_content_view`. Feed branch requests through the slot's normal navigation/history pipeline while preserving parent focus on selection-driven requests. Guard against location-notification feedback loops.
- [ ] Route Reload/Stop to the controller. Keep endpoint-based location/history/status separate from active-column selection/actions. For Search, Recent, Starred, Trash, special Network, and file chooser modes, instantiate the existing suitable view while retaining the user's Columns preference for return to ordinary browsing.
- [ ] Verify operations against disposable files in an ancestor while a deeper column remains visible: Copy/Paste, Cut/Paste, New Folder, Rename, Trash/Undo, Properties, and drag/drop to a neighboring column. Assert the changed file's directory matches the focused/context-clicked column, not the endpoint. Confirm directory monitors update affected columns.
- [ ] Run focused navigation and context tests plus existing files-view tests. Perform interactive keyboard/pointer checks of branch highlights, separators, loading/error states, and special-location return. Record available display tooling and any coverage limitation honestly.
- [ ] Commit integrated Columns navigation and view controls.

### Task 6: Fedora replacement package, GitHub publication, and host default

**Files:** Add `packaging/fedora/nautilus-miller.spec`, `tools/miller/build-rpm.sh`, `tools/miller/install-default.sh`, `tools/miller/restore-stock.sh`; finish `README.miller.md` and add a versioned verification report under `docs/miller/`.

**Interfaces:** `build-rpm.sh` consumes this checkout and writes RPMs below an ignored build/output directory. `install-default.sh` consumes an explicit built RPM path and records previous package/default-handler/view settings below `$HOME/.local/state/nautilus-miller/`. `restore-stock.sh` restores the recorded directory handler and view preference plus Fedora's package using DNF. No scripts silently broaden privileges; document the exact `sudo dnf` command they invoke or require.

- [ ] Fetch Fedora's matching Nautilus source RPM/spec using `dnf download --source --destdir=... nautilus`; preserve its package split, downstream patches, file lists, extension ABI, and canonical service/portal integration. Adapt its source input to the fork and add a Miller release suffix greater than the installed 50.4 release. Build with the replacement profile through `rpmbuild` and query the resulting package metadata/file lists.
- [ ] Test script argument handling and missing-artifact behavior. Ensure rollback restores only recorded view/default-handler keys and does not reset unrelated Nautilus preferences. Review contents with `rpm -qpl` and metadata with `rpm -qp --info`; preview the explicit RPM installation transaction using `sudo dnf install --assumeno` before installation.
- [ ] Compile and run the targeted new tests, relevant upstream headless checks, desktop/schema validation, and the integrated display checks. Fix failures before proceeding to deployment. Obtain an independent review of the final branch using the execution method selected by the user; resolve reportable issues.
- [ ] Collect the user's GitHub owner/name and visibility if still unspecified. Verify authenticated GitHub access without exposing credentials. Replace the checkout's tag-only upstream fetch refspec with `+refs/heads/*:refs/remotes/upstream/*`, fetch full upstream history, add the new repository as `origin`, and publish the reviewed commits. Retain `upstream` and document future merge/rebuild commands. If GitHub access is unavailable, leave the local branch ready and state the specific publishing requirement.
- [ ] Record the currently installed Nautilus package version, directory MIME handler, and relevant view setting. Install the built replacement and matching split packages through DNF. Close the old Nautilus application after ensuring no file-operation progress is active, then start the replacement. Set `inode/directory` to the canonical Nautilus desktop entry and set this user's folder view to Columns.
- [ ] Verify the installed/running executable belongs to the Miller RPM, opening a directory invokes the fork, the desktop launcher works, and FileManager1 ShowItems reveals a disposable file in the correct column. Confirm grid/list still switch, the fork is the default, and rollback instructions name an available stock-package source.
- [ ] Commit final documentation and report the GitHub URL, run command, installed RPM version, checks actually passed, and rollback command. Do not describe build-only results as an interactive success.
