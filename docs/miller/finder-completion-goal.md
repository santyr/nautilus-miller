# Nautilus Miller — Alternate Column View Completion

## Current authority and scope

**Revised 2026-10-17 at the user's explicit direction.** Nautilus is the mature file manager. Nautilus-Miller adds an alternate way to view and interact with file hierarchies. Reuse Nautilus's existing functionality in the context of Miller columns. Reimplementation and reinvention must be avoided.

This revision supersedes the broader acceptance contract of 2026-10-16 and conflicting requirements in earlier plans, briefs, checklists, and ledgers. The prior contract is preserved as `docs/miller/finder-completion-goal-2026-10-16.md` for history. Its unexecuted exhaustive test matrices, exact test-count targets, and broad new-feature requirements are no longer independently mandatory. This is an explicit user-directed scope change, not evidence that those old requirements passed.

The active goal remains incomplete until the focused deliverable below is working and reviewable. Preserve completed fixes, useful tests, the public repository identity `santyr/nautilus-miller`, and the personal-project README/support QR. Do not discard user work or rewrite historical evidence.

## 1. Deliverable

Deliver a dependable Finder-inspired column view within the existing C/GTK/libadwaita Nautilus application, buildable and runnable on this Fedora/GNOME host, with a traceable replacement RPM and safe installation/rollback instructions.

Keep Grid/List modes, native Linux behavior, the separate development identity `org.gnome.Nautilus.Miller`, and the canonical production Nautilus contracts. Finder-inspired means persistent hierarchical context, predictable selection and focus, minimal column reveal, usable resizing and keyboard navigation, and native drag-and-drop across the hierarchy. It does not require cloning macOS or building a separate file manager.

## 2. Reuse boundary — primary implementation rule

Use the existing Nautilus/GIO/GVfs directory and file objects, FilesView/ListBase, actions, menus, MIME activation, clipboard, file-operation engine, conflicts/progress/cancellation, undo, permissions, archive handling, bookmarks, mounts, special views, extension loader/API, thumbnails, and external previewer.

The Miller coordinator owns only what the multi-column presentation requires: branch disclosure and markers; the active action column; selection/focus coordination; per-tab/history integration; column width/reveal; and temporary spring-loaded branch state.

Before adding behavior:

1. Locate the existing Nautilus/GTK implementation and use it.
2. Identify the precise column integration boundary that needs adaptation.
3. Reproduce a missing or incorrect observable behavior before modifying native code.
4. Make the smallest integration change and verify the affected path.

Do not add parallel transfer, clipboard, history, hover, remote, thumbnail, preview-service, indexing, or extension engines. New helpers need an actual integration purpose. Existing completed additions should be audited for duplication; simplify or remove redundant mechanisms where that improves the result. Do not rewrite working native behavior merely to satisfy an old checklist.

## 3. Column-specific behavior to finish

- Selecting a folder discloses its child and replaces only unrelated descendants. Selecting a file does not launch it. One column owns actionable selection; branch markers are separate. Folder disclosure and preview updates preserve intended keyboard focus.
- Native shortcuts, context menus, toolbar actions and delayed dialogs use their initiating selection and directory. A focused parent with a child disclosed remains a valid action destination. Empty columns remain navigable and actionable.
- Back/Forward, tabs, breadcrumbs, sidebar navigation, external reveal and view switching retain coherent Miller state through existing Nautilus navigation. Background completions must not overwrite a newer branch or steal action ownership.
- Columns have usable resize/reset/fit controls and minimal reveal, including relevant parent/child visibility, intentional scrolling and RTL. Keep existing implemented preferences without expanding into a new layout framework.
- Native drags preserve their payload and use the pointer destination. Reuse native hover and vertical scrolling; coordinate only temporary column disclosure/restoration and outer-strip scrolling that GTK does not supply. Check nested hover/cancel, accepted drops, and relevant tab/window/external-app and owner-destruction boundaries. Do not require a separate implementation for each backend or destination class when the same native path handles it.
- Remote locations, mounts, errors, special views and installed extensions use the same native providers/actions as Grid/List. Check the Miller-specific GFile, action context and lifetime boundaries. Fix a demonstrated column regression; do not rebuild or comprehensively re-certify GVfs, mount/authentication, Trash, OpenPGP or extension internals.
- Retain the implemented inspector/preview only insofar as it uses existing safe providers and respects active selection, privacy, cancellation and focus. Keep native Sushi/Space integration. Further preview formats, custom rendering and additional preview features are optional and do not block this release.

## 4. Focused verification and release gate

Verification proves that existing functionality is correctly connected to columns. It is not a fresh certification of the entire upstream file manager.

- Audit production changes against the reuse boundary. Review significant column ownership/lifetime changes and fix demonstrated important regressions.
- Build both development and production profiles. Run the maintained regression suites at a meaningful component/release checkpoint; use focused tests during iteration. Keep historical results and known upstream/environment failures accurate. Do not repeatedly rerun unrelated exhaustive gates after each small change.
- Demonstrate navigation/selection/focus, parent/empty-column action targeting, native clipboard and drag integration, relevant delayed/lifetime boundaries, history/tabs, and preview ownership using disposable files and genuine input where necessary. Verify actual file results and untouched destinations for operation-targeting checks.
- Use existing evidence when it directly covers unchanged integration. Add cases for uncovered column-specific risks, not to achieve predetermined numeric totals. Source reuse establishes architectural continuity; a claim of exercised runtime compatibility still needs actual evidence. Unsupported or untested capabilities must be described honestly.
- Check keyboard accessibility, control names, readable styling and RTL where Miller introduces UI. Check deep/large-directory responsiveness and owner cleanup sufficiently to identify regressions attributable to multiple columns. A new comprehensive screen-reader certification, every display configuration, or 10k/100k upstream benchmark campaign is not an independent release gate absent a demonstrated Miller issue.
- Real remote services, full OpenPGP workflows, exhaustive conflict/backend/codec matrices and unrelated upstream stress tests are follow-up work unless needed to reproduce a specific Miller integration defect. Installed extension menu/action targeting, including GPG when available, remains a focused compatibility check; do not claim untested plugins were verified.
- Build consistent traceable Fedora packages with an incremented release and checksums. Verify development/production identity, canonical file-manager/extension/default-handler contracts, and safe installation/rollback ordering. Exercise the relevant package change and recovery path in a disposable environment where feasible. Preserve rollback data and document precise commands and any unexercised release limitation.
- Maintain appropriate GitHub CI for this fork. Report observed CI separately from a workflow file or local tests; unrelated CI/environment infrastructure work must not turn into a new file-manager feature project.

Existing test cases remain useful and should not be deleted merely to make a suite green. Superseded unimplemented cases are deferred explicitly, rather than silently counted as passing. Record the focused remaining work and its evidence without retaining old mandatory numeric-count gates.

## 5. Deployment and data safety

Development and tests use disposable files, private test settings and owned services. Do not modify real user data, keys, production package state or queued offline transactions during development. Do not force-kill active file operations.

The user ultimately wants this fork as the default file manager. Prepare the concrete package set, install script, rollback path and readiness report. Installing the newly completed release on the host requires a separate explicit deployment instruction; the earlier installed fork is not authorization to replace it during this development goal. Do not report deployment complete on the basis of an RPM build or the old installed version.

## 6. Completion report

Report the implemented column behavior, native paths reused, focused verification results, package/source identity, remaining known limitations, and exact installation/rollback commands. Distinguish tested compatibility from preserved API compatibility. Do not claim that all upstream functionality has been independently tested.

The goal is complete when this alternate view and its integration are ready for the requested host deployment handoff, with no known important column-specific wrong-target, data-loss, focus/navigation or lifetime regression. A comprehensive new file-manager implementation or upstream re-certification is expressly outside this goal.
