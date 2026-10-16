# Miller Column Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the layout portion of Stage 2: bounded persistent widths, accessible controls and minimal reveal, retaining the full completion contract.

**Architecture:** Add a small GSettings width-policy unit. Keep GTK geometry, resize controls and reveal lifetime in the existing strip. Preserve the FilesView operation architecture, lexical ancestry and native entries/shortcuts.

**Tech Stack:** C, GLib/GSettings, GTK4/libadwaita, Meson and private Mutter/Wayland integration tests.

**Spec:** `docs/miller/finder-completion-goal.md` sections 3–4, 9 and C09/C13; `docs/miller/interaction-contract.md`.

## Global Constraints

- Isolated `finder-completion` worktree at `958cd400c`; never install on host or operate on real user files.
- Directory widths default 280 px, bounds 180–1200 px; independent from future preview width. At most 128 per-location overrides with least-recent-use eviction.
- Use existing profile-specific GSettings. No state files in browsed directories, no credentials/query/fragment in remembered URIs.
- Full goal remains active: Unicode/IME typeahead, complete history/selection/tab restoration and all later stages are separate mandatory plans, not waived.
- Supplied contract directs continuous native implementation and authorizes reversible choices; do not repeat design/preference approval gates.

## Review Focus

- Credential-bearing remote URI must never appear in settings, including already stored invalid entries (Task 1).
- Reading/touching an MRU entry must not evict a recently used width or grow beyond 128 (Task 1).
- A resize control kept open while its column is removed must not call freed column data (Task 2).
- Delayed branch/slot notifications must not snap a manually scrolled viewport right (Task 3).
- RTL resize direction and semantic Left/Right must agree with physical column placement; entry keys remain untouched (Tasks 2–3).

### Task 1: Bounded width policy

**Files:** new `src/nautilus-miller-widths.[ch]`, `test/automated/displayless/test-miller-widths.c`; modify source/test Meson files and preference schema.

**Interfaces:** `int nautilus_miller_width_get(GSettings*, GFile*)`; `void nautilus_miller_width_set(GSettings*, GFile*, int)`; `void nautilus_miller_width_reset(GSettings*, GFile*)`. Keys `miller-default-column-width` (uint, schema range180–1200/default280), `miller-column-widths` (`a(su)`, newest first/default[]). Get touches a remembered override. Set clamps. Reset removes override, preserving default. Only native file or sftp/smb URIs with no userinfo/query/fragment are persisted. Sanitize malformed/out-of-range/duplicate/unsafe entries on access; retain at most128. Unsupported remote identities use default without persistence.

- [ ] Write policy tests: default/clamp/reset; separate settings instance rehydrates; 129 entries plus touch evicts least-recent-used; URI privacy; corrupt/duplicate settings sanitization. Confirm missing unit RED, then implement and obtain GREEN.
- [ ] Compile/profile-validate and commit independently testable policy.

### Task 2: Native column controls and fit

**Files:** `src/nautilus-miller-view.c`, style and README; GTK tests in `test-miller-view.c`.

**Interfaces:** Each heading contains a labeled `GtkMenuButton` with custom popover: Width spin (180–1200, step10), Fit to Contents, Reset Width, Use as Default. Width state loaded via Task1 policy. Drag updates geometry without persistent writes until drag-end; spin and buttons persist explicitly. Fit samples at most128 evenly spaced already-loaded model rows, Pango measures display names plus bounded icon/affordance space, clamps; no enumeration or I/O. GTK widgets are owned by column hierarchy; popover signals disconnected and menu dismissed before column free. Reset removes override, default button stores current width. RTL drag reverses physical x delta.

- [ ] Add GTK regression asserting remembered initial width (RED against hardcoded280), spin/range/accessibility names, fit/reset/default and drag-end persistence; recreate strip and verify rehydration. Assert no active-action/location changes from controls.
- [ ] Implement controls, full-name heading tooltip, bounded fit and safe teardown. Test remove column with open menu and retained widget references; no late callbacks use freed state.
- [ ] Targeted GTK/policy/profile tests GREEN; commit.

### Task 3: Minimal reveal and RTL navigation

**Files:** `src/nautilus-miller-view.c`, `test-miller-view.c`, interaction docs/acceptance.

**Interfaces:** Replace unconditional scroll-to-end with one owned frame callback storing requested column indices. Compute bounds relative to strip after layout. Reveal active+disclosed child when pair fits; otherwise prioritize full active column. Direct navigation reveals endpoint; Left/focus reveals ancestor. Same-path passive/slot echoes do not schedule new reveal. Manual scrolling/scrollbar press cancels pending reveal. Clamp adjustment to valid range; RTL geometry comes from GTK, Left/Right semantics reverse in RTL. No history entries for focus/width.

- [ ] Add deep narrow-window GTK regression: navigate endpoint, focus earlier ancestor/Left, already visible target unchanged, manual scroll then same-path echo and late loading unchanged (RED against unconditional far-right). Assert relevant bounds and adjustment values, not screenshot alone. Exercise RTL semantic keys and preserve entry-focus behavior.
- [ ] Implement minimal reveal, cancellation and RTL key mapping; test branch replacement before tick and strip disposal during pending tick.
- [ ] Run full Meson and Python suites, profile validator and diff checks. Record limits: these GTK controller tests do not establish genuine input/screen-reader/IME/scaling gates.
- [ ] Commit, dispatch one fresh independent whole-plan reviewer, fix Important/Critical once with red/green evidence and green full suite. Keep all later requirements open.

## Design choices and coverage

GSettings was chosen over a new XDG JSON store because the existing development profile already isolates schema IDs and paths and provides restart persistence. A native popover/spin gives numeric values and keyboard access without a custom slider role. A sampled fit is deliberately bounded and may ellipsize unsampled outliers; full names remain available by tooltip. Only layout is covered here; typeahead and history require independent plans because they change input and bookmark ownership rather than width geometry.
