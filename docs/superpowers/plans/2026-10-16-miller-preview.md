# Miller Preview and Inspection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a selection-owned inline inspector and complete native external Quick Look acceptance without changing navigation or action targets.

**Architecture:** A separate inspector widget owns metadata, content request lifetimes and presentation. The Miller strip owns its placement and selection subscription; the slot exposes a native toggle. Existing Nautilus thumbnails, MIME activation and Sushi remain providers.

**Tech Stack:** C, GTK4/libadwaita, GIO/GIO-Unix, existing libglycin/libglycin-gtk4 API2, GSettings, private Mutter/GVfs fixtures, Meson.

**Spec:** `docs/superpowers/specs/2026-10-16-miller-preview-design.md`; governing `docs/miller/finder-completion-goal.md` section5/Stage3/C07/C08 and `docs/miller/interaction-contract.md`.

## Global Constraints

- Finish history-plan review/corrections before executing this plan. Continue in the isolated worktree using the authorized native staged workflow. No production installation, host input, user files/keys or messages.
- Inspector initially hidden; settings `miller-preview-visible=false`, `miller-preview-width=320` bounded240–800 logical pixels, `miller-preview-remote=false`. Directory widths remain independent.
- Text:128KiB input/32KiB display, explicit truncation; images:16MiB input/64million source pixels/1024×1024 still output. Five-second content deadline. No recursive counts, autoplay, shell-built commands or automatic execution.
- Only regular-file content candidates. Reject local special files and symlink replacement with nonblocking descriptor opening plus identity checks. Remote-off must not open content, including cached remote content. Honor thumbnail/privacy preferences and re-evaluate when they change.
- Keep at most one content request in flight per inspector, coalescing replacement targets; count canceled work until provider resources are actually released. New selection/hiding/inactive tab/disposal cancels or safely detaches work. Async callbacks keep request data and weak widget ownership, not tabs/windows. Release late results; never publish them or steal focus.
- Reuse existing glycin sandbox and thumbnail providers. Verify installed isolation/resource/cancellation semantics before claiming content acceptance. Never disable sandboxing to make a test pass.

## Review Focus

- Selection changes through restoration/monitor notification rather than a click: preview still follows the single current action owner (Tasks1–2).
- File changes from regular to FIFO/link between metadata and open: no UI stall or unintended content access (Tasks1/3).
- Hiding, inactive-tab loading or teardown during decode: request cannot keep UI owners alive or publish stale content (Tasks1–3).
- Thumbnail/privacy preference changes while a request is outstanding: older content must not appear afterward (Task3).
- Actual external provider goes away while its window owns focus: user can resume browsing and retry (Task4).

### Task 1: Inspector metadata and bounded text

**Files:** create `src/nautilus-miller-preview.[ch]`, `test/automated/display/test-miller-preview.c`; modify source/display Meson registration, `tools/miller/with-delayed-backend.py` and its Python guards; create `test/miller/test-preview-io.py` for actual read/protocol observations. Use a focused UI resource only if needed; follow existing resource generation.

**Interfaces:** `GtkWidget *nautilus_miller_preview_new(void)` returns an internal `AdwBin` subclass. `void nautilus_miller_preview_set_selection(NautilusMillerPreview *self, NautilusFileList *files)` copies the selection. `void nautilus_miller_preview_set_active(NautilusMillerPreview *self, gboolean active)` gates content work. Read-only GObject `loading` property reports owned request state; presentation remains ordinary accessible GTK widgets. No test-only production injection API.

- [x] Add fixture cases `empty`, `folder`, `multiple-known-partial`, `text-bounds`, `invalid-binary`, `special-file`, `newer-selection`, `hide-dispose`, `entry-focus`. Assert visible content/type/known-size wording,128KiB/32KiB bounds and truncation, no directory enumeration/activation, actual weak destruction and unchanged root focus. Add a controlled local replacement race using owned fixtures; a FIFO must never block the UI. Run missing-widget API test first. Expected: compile RED.
- [x] Implement metadata and text requests with generation/cancellable/deadline ownership. Native opening uses `O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC` and `fstat` before bounded regular-file reads; keep it off the GTK main loop. Remote content remains disabled until Task3 policy tests. Escape invalid filename/text safely and display literal text. Expected: focused cases GREEN.
- [x] Run full build, profile validator, private-session Meson and maintained Python guard/deployment suites. Expected: all pass, external skips explicit. Update acceptance with RED/GREEN evidence and commit the standalone widget; no product pane claim yet.

### Task 2: Active selection, inspector controls and persistence

**Files:** Miller view, window slot, `data/org.gnome.nautilus.gschema.xml`, view-menu/shortcuts resources, preview UI/tests, restart fixture/docs.

**Consumes:** Task1 widget API. Strip owns the paned and sends copied current selection after active-view and selection transitions. Disconnect old view/file subscriptions before replacing targets. Slot action `slot.miller-preview` is stateful boolean, enabled in Columns, bound to the visibility setting and Ctrl+Shift+P.

- [x] Add `active-parent`, `empty-child`, `history-selection`, `mode-special-return`, `tab-ownership`, `controls-focus`, `width-independent` and genuine `toggle-keys` cases. Assert target identities, unchanged branch/history/action owner and selection, no file launch, named controls, width320/bounds240–800 without changing column width280. Add separate-process dconf writer/reader checks for all three settings and actual pane sizing. Expected: missing setting/action/pane RED.
- [x] Host the pane beside the scroller; integrate visibility, named close/numeric width controls and paned resizing. Persist deliberate width changes rather than allocations caused by a window resize. Prevent hidden/background tabs from reading content. Closing a focused inspector returns focus safely to its listing; other controls keep focus. Expected: targeted UI and restart GREEN.
- [x] Run full Meson/Python/profile, genuine mounted-home navigation and delayed-backend regressions. Expected: pass with the prior special-view/focus boundaries retained. Update README to describe the now-visible capability and commit.

### Task 3: Images, thumbnails, media fallback and remote privacy

**Files:** preview implementation/tests, existing thumbnail API consumers as needed, `tools/miller/with-delayed-backend.py` fixture payload only if needed, dependency/acceptance docs.

**Interfaces:** retain Task1 API. Content result is owned entirely by its request; decoding and metadata completion never mutate a selection or path. No new decoder dependency or external process manager.

- [ ] Add `png-jpeg-still`, `source-output-limits`, `corrupt-image`, `pdf-thumbnail`, `media-poster`, `unsupported-codec`, `privacy-change`, `remote-no-read`, `remote-bounded-read`, `delayed-a-b`, `decode-tab-close`, `rapid-replacement-budget`, and `malicious-name` cases before adding each behavior. Use real disposable file bytes, installed providers and paused owned GVfs rather than fake completion signals. Check fallback/loading/error wording, actual read limits and timely weak destruction. Expected: unsupported renderer/privacy/lifetime behavior RED; characterize existing provider support separately.
- [ ] Use bounded bytes with glycin asynchronous load/scaled still-frame calls; no source-directory exposure, embedded resource fetch or unsafe sandbox selector. Check source dimensions before requesting a frame. Use existing thumbnail policy/generation for PDF/poster representations; retain explicit Open and Space paths. Metadata/type fallback must state unavailable preview truthfully. Record installed provider versions, isolation and actual limits. Expected: supported cases GREEN, unsupported formats honest fallback, no external reads/launch/autoplay.
- [ ] Exercise remote-off and enabled bounded reads against the actual archive fixture, and repeat against mandatory local SFTP/SMB when those fixtures exist in Stage5. Archive acceptance is not remote authentication/monitor acceptance. Run full regressions and update C07 evidence precisely; commit.

### Task 4: Real external Quick Look and component review

**Files:** `src/nautilus-previewer.[ch]`, existing previewer tests, genuine preview UI tests, private session fixtures, docs. Build a separate canonical-profile test tree without installing it.

- [ ] Add actual Space/selection navigation/repeated Space/Escape/focus-return/tab-switch/tab-close cases using installed canonical Sushi. Repeat in Miller and canonical profiles on private buses with mounted disposable Home. Add unavailable-provider and service-restart cases; expected truthful visible feedback, no stale selection/action owner or lost browsing focus. First reproduce any failure; discovery mocks alone cannot pass this gate.
- [ ] Correct demonstrated wrapper/UI ownership and missing-provider feedback at native boundaries. Preserve provider contract and existing discovery-generation tests. Expected: actual provider cases and discovery suite GREEN.
- [ ] Run full fresh regressions, profile isolation, process restart and installed-provider acceptance. Record exact source/providers/results; all Stage3 gates must have evidence. Obtain one fresh read-only whole-plan review, then one regression-first correction pass for Critical/Important findings with a full green suite. Preserve deferred minors/rulings and continue the full goal.

## Self-review

The four tasks cover the spec's widget/read lifetime, user integration/persistence, renderer/privacy and actual external-provider requirements. Later consumers use exactly the Task1 signatures; no parallel path/history or transfer engine is introduced. Each review-focus timing class has named tests in its owning task. Canonical builds remain private and uninstalled. Actual SFTP/SMB, accessibility and performance verification remain required full-goal work; this plan does not waive them. Resource caps are proposed product limits and must be tested before claims.
