# Miller native drag browsing design

Authority: the approved finder-completion-goal.md, especially sections3,6,7,8,9 and C04/C05/C06/C10/C16. This adds a reversible interaction policy within the existing native architecture. It grants no deployment authority.

## Intent and constraints

Finder-style hierarchical drag browsing should preserve the original dragged files, let the pointer select the destination independently of the active action column, and restore the prior branch when a drag is cancelled. Keep native Grid/List behavior, GTK drag sources/targets and negotiation, Nautilus transfer/conflict/progress/undo jobs, real directory models, existing hover timing, and existing Miller snapshots. No replacement transfer, hover timer, or history engine.

## Existing boundaries to characterize first

NautilusListBase installs GtkDragSource and native GtkDropTarget on cells, plus native background drop targets. MillerColumn already calls setup_cell_common and setup_cell_hover. Native hover currently selects the folder and activates that selection, so source selection preservation is unproved. GTK4.22.5 GtkListBase already supplies vertical drag autoscroll. Real pointer drags, modifier negotiation, horizontal scrolling and native tab/sidebar drop behavior still need execution evidence.

## Temporary branch policy

Hover-opened directory columns are temporary. They are actual FilesViews for actual directory URIs. Before the first temporary hover, capture the current immutable Miller snapshot and use the existing location-changing notification to preserve the native origin bookmark. Keep the slot's committed endpoint/history until a drop is accepted; provisional hover must not add history entries, retarget captured drag data or steal keyboard focus. Mark this limited committed/provisional distinction in the interaction contract.

Reuse the native hover timeout. Only after a failing genuine-input regression, expose a small handled spring-load signal from the existing hover callback so Miller can disclose without replacing the drag source selection; Grid/List retain the current fallback. Reuse the current GTK drop identity and native accepted-drop boundary, without accepting drops in a motion-only observer or copying files in the coordinator.

Cancel, a rejected drop, or leaving the slot without acceptance restores the pre-drag snapshot while the same drag and original navigation still own it. Accepted native drop retains the displayed destination branch and commits at most one ordinary navigation from the origin. A later native transfer conflict/error/cancellation remains native job behavior; it does not undo the user's accepted destination navigation. A newer explicit navigation, source-tab closure or incompatible monitor recovery supersedes the old snapshot; never roll it back over newer state. All callbacks/references/timers must be owned and cleaned up.

Use existing minimal reveal for disclosure and native vertical autoscroll. Add only missing horizontal edge scrolling after real reproduction; its frame callback belongs to the strip and is cancelled on leave/end/unmap/disposal, uses RTL geometry, stops at adjustment bounds and does not oscillate. Do not measure directory contents or perform synchronous remote queries.

## Acceptance scope

Use the current private compositor, mounted disposable Home, exact file bytes, untouched sibling sentinels, native job completion and observed actual GdkDrag/GdkDrop. No controller-signal emission or direct transfer API counts as a drag. Reuse existing test operation fixtures via a test-only shared helper where there are now two consumers; retain all63 operation tests.

Cover Grid/List controls; within-column folder drops; between visible columns and empty backgrounds; copy/move/link modifiers and unsupported destinations; nested spring-loading and cancel/backtracking; source selection/captured files; cross-tabs/windows; owned external GTK source/sink; native sidebar bookmark/volume paths where supported; vertical/horizontal scrolling; source/destination removal; unmap/tab-close and repeated drags. Real remote disconnect remains tied to the mandatory SFTP/SMB fixtures; a local deletion is not equivalent. Missing backend/session is an open test, never a pass.

Full-gate evidence must retain operations63, both Meson profiles, inspector53, navigation10, delayed4, maintained Python90 plus new cases, and real Sushi. One fresh whole-component review and one regression-first correction pass follow the inline tasks; no per-task or second reviewers. Extensions/OpenPGP, Stage5 and Stage6 stay mandatory.

## Decisions and costs

- Keep native hover timing and native drop/transfer semantics; cost if insufficient: narrow integration hooks may be needed after reproduction.
- Restore temporary branches on cancellation/leave, retain them on accepted input even if the subsequent job fails; cost if undesired: reversible navigation-policy adjustment, no invented rollback of file operations.
- Keep committed slot history separate only while a drag owns provisional columns; cost if guards are wrong: stale native location commits could override provisional state or old snapshots could overwrite new navigation. Add deliberate delayed/navigation/lifetime tests before acceptance.
- Reuse test helpers when adding a second executable; cost if extraction is wrong: existing63 tests regress, so preserve their full gate and change no production API for test convenience.
