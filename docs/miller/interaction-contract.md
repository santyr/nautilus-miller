# Miller interaction and ownership contract

Authority: [completion goal](finder-completion-goal.md), sections 3–9. This contract expands the historical initial design; it does not authorize deployment.

## State and owners

| State | Owner | Invariant |
|---|---|---|
| Logical lexical branch | Miller path, hosted by one window slot | Endpoint owns location/history, not every action destination. Never canonicalize symlinks or cross URI authority. |
| Active column | One strip per slot | At most one FilesView publishes window file actions. Inactive tabs publish none. |
| Keyboard focus | GTK | Disclosure and preview updates cannot grab focus; entries, IMEs, menus and dialogs keep their keys. |
| Branch marker | Parent column | Identifies the path to the child; must not replace newer user selection. |
| Actionable selection | Active FilesView | One directory only; parent selection cannot be assigned to child model. |
| Context interaction | Opening menu, dialog or drag | Capture initiating view and files/destination; later focus cannot retarget an action. |
| Preview target | Inspector per slot | Snapshot of active selection; never navigation, execution or history. |
| Delayed request | Owning column/tab/preview with cancellation/lifetime identity | Discarded owner and obsolete request cannot publish results. |

## Transition policy

- Single folder selection discloses child, preserving parent row focus. Sibling selection replaces only the unrelated suffix. File and multiple selection collapse unrelated descendants and update inspector without activation.
- Right enters the disclosed child; Left returns to the parent and minimally reveals it. Semantic forward/backward follows RTL layout; history Alt-arrow remains native.
- Each deliberate change of branch endpoint enters ordinary Nautilus history once. Focus, preview, monitor events, programmatic restoration and width changes do not. Continuous keyboard selection may disclose multiple folders; each committed endpoint follows the existing navigation pipeline until explicit coalescing is proven necessary.
- Back/Forward must restore valid selection, active column and scroll state. Tabs retain independent branch state. View switching transfers only selections belonging to the destination view's location.
- Toolbar/keyboard file operations use the active column even when focus is in a non-file-list control. An open context menu/dialog/drop keeps its captured context. Discarding a branch cancels or safely detaches its work.
- Native Enter activation, F2 rename, Ctrl editing/location/selection and history/tab shortcuts remain. Columns gets Unicode/IME-aware type-to-select by default, with stock type-to-search as a preference and explicit recursive search action.

## Layout and persistence policy

Logical ancestry stays complete to the GIO root. Initially keep displayed ancestor policy unchanged; profile models before resource changes. Reveal only the relevant column/pair after user navigation, never snap right after background updates. Pointer scrolling remains intentional.

Disclosure reveals parent and child together if their complete pair fits the viewport; otherwise prioritize the focused parent. A single overwide target reveals its reading-order leading edge. Direct navigation reveals the endpoint, Left/Right and focus changes reveal their target, and an already visible target does not move the viewport. Delayed same-path selection-driven slot echoes do not create a new reveal; wheel/touchpad scrolling and presses cancel pending reveal. GTK supplies physical RTL geometry; semantic forward/backward keys reverse. Focused editors and popovers keep arrow keys. Reveal requests own one frame callback, replaced on new navigation and canceled on strip disposal.

Use 280 px default directory width, 180–1200 px bounds; independent preview width. Persist default and at most 128 per-location width overrides with oldest-use eviction in profile-specific GSettings (XDG settings storage). Reading a remembered location refreshes its recency. Safe identities are local file URIs and SFTP/SMB URIs with a host, no userinfo/query/fragment and at most4096 bytes; other locations use default widths without a saved key. Sanitize unsafe/invalid/duplicate legacy entries on access. Fit measures at most128 evenly spaced already loaded rows; never synchronous directory enumeration. Offer fit, reset, use-as-default and keyboard-operable numeric resize controls with names and current values. Existing live columns retain their current width when another column changes the default; newly created columns use the new default.

## Columns text input

`miller-type-to-select` defaults true and is exposed as **Type to Select in Columns** in General preferences. Only Columns takes ownership of implicit typing; Ctrl+F remains explicit search and disabling the preference restores stock search. Grid/List retain stock behavior. The active listing's GTK IM context owns committed Unicode and preedit. Entries, popovers, dialogs and preview controls retain their own keys. Preedit never selects or activates; Enter/Space during composition cannot fall through to launch/preview. Bare Space outside composition retains external preview.

The bounded prefix is256 Unicode characters and clears after1second of inactivity outside preedit, including cancellation of an older pending match. Backspace removes one grapheme; Escape clears. Comparisons use NFKC and Unicode case folding without changing file identities. The first matching loaded row in current sort order wins. Each idle checks at most256 rows or4ms; no directory enumeration or remote content fetch is added. A plain accessible hint shows prefix/preedit/no-match state. New selection, location, action column, tab, focus departure, settings changes and disposal cancel owned work.

Selection-driven navigation also captures the action generation at the original model change. A later Right/focus navigation invalidates delayed parent notifications before either idle boundary, so the child keeps action ownership.

## Preview policy

Optional stable right-side inspector, initially hidden, with persisted visibility/width. Follows active actionable selection, not branch endpoint. Folders use nonrecursive metadata; multiple selection reports only known aggregate values. Images use existing isolated image infrastructure; text/source reads have byte and display limits. PDFs use thumbnail/first-page representation and Open; audio/video use poster/metadata and external preview, never autoplay. Unsupported formats show a truthful fallback.

All I/O and decode work is asynchronous and cancellable; new selections invalidate earlier results. Only regular files are content candidates. Respect Nautilus thumbnail/privacy preferences; remote content preview defaults off. No shell invocation from filenames, embedded resource fetches, automatic archive extraction or credential-bearing logging/state. Space uses the external provider, with canonical Sushi discoverable in both profiles, retry/restart support, tab ownership and focus return.

## Deployment boundary

This work changes only the development worktree and disposable fixtures. Production RPM installation needs separate user authorization. Release workflow must validate, save and verify rollback, drain, transact, set compatible preferences, launch and verify before recording success. Never force-kill operations or silently invalidate queued offline updates.
