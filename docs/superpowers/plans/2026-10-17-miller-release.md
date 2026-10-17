# Nautilus Miller release handoff plan

> **For agentic workers:** Use superpowers:executing-plans inline. This continues the already authorized completion goal and execution method; routine release preparation does not require another design approval. Do not publish, merge or install on the host without the applicable authorization.

**Goal:** Produce a traceable Fedora44 package set, maintained CI and a precise installation/rollback handoff for the alternate column view.

**Architecture:** Keep the existing Nautilus build, Fedora split packaging, tracked deployment phases and disposable test harness. This plan adds release metadata and connects existing checks; it adds no file-manager service.

**Tech stack:** Meson/Ninja, RPM/DNF5, Python, rootless Podman, GitHub Actions.

**Spec:** `docs/miller/finder-completion-goal.md`, especially sections4–6. The DnD plan's no-push/no-install constraint remains in effect for that component. Historical exhaustive matrices are superseded by the current goal.

## Global constraints

- Preserve canonical production contracts and isolated `org.gnome.Nautilus.Miller` development identity.
- Do not change real user files, keys, installed packages, default handler, offline transactions or active file operations.
- Keep package names `nautilus`, `nautilus-extensions`, `nautilus-devel`, version50.4 and exact matching release/architecture. Increment release to `1.miller2.fc44`.
- Reuse `tools/miller/build-rpm.sh`, `deploy.py`, existing session/home guards and existing suites.
- Workflow files/local container checks are not observed GitHub CI. Deferred compatibility is not a pass.
- Retain rollback data, personal README and exact support QR.

## Review focus

1. Mixed split versions or development identity shipped in production: inspect built RPM identities and canonical assets.
2. A package update loses saved stock rollback/settings: existing deployment regression plus disposable stock→fork→stock package transactions.
3. CI bypasses Home/input guards: reuse the current wrapper behavior; execute as a nonroot user in its canonical Home with source beneath it.
4. A source archive does not identify the tested checkout: clean committed tree, recorded commit, archive/RPM checksums and source-package inspection.
5. A handoff claims deployment/compatibility without evidence: report installed version, observed CI state and all material limitations separately.

## Task 1: Release identity and reproducible package handoff

Files: `packaging/fedora/nautilus-miller.spec`, `README.miller.md`, `docs/miller/deployment.md`, `docs/miller/release-readiness.md`.
Consumes: green DnD correction checkpoint and reviewed existing components.
Produces: clean source commit; main/extensions/devel and source RPMs under `build-rpm`; `source-commit.txt` and SHA256 manifest.

- [x] Inspect current spec and installed version; record old miller1 release/GNOME URL as the missing release metadata.
- [x] Increment Miller release, use public fork URL, replace obsolete desktop-icons description with the actual alternate-view scope; retain Fedora patches/dependencies and native packaging.
- [x] Build through `./tools/miller/build-rpm.sh`; require exit0 and all matching packages.
- [x] Inspect RPM NEVRAs, dependencies, files, canonical services/schema/desktop/extension ABI and source archive commit/checksums. Preserve private development profile check from the fresh regression checkpoint.
- [x] Record exact artifacts and commit; do not install them on the host.

## Task 2: GitHub CI over native tests

Files: `.github/workflows/build.yml`, `tools/miller/ci-test-wrapper.py` if needed for the existing dispatch, relevant test documentation.
Consumes: native Fedora dependencies and existing Meson tests/session/home helpers.
Produces: build/test workflow for production and Miller profiles; uploaded Meson logs.

- [x] Record absence of workflow/observed runs before adding the adapter.
- [x] Use a disposable Fedora44 environment, nonroot canonical Home and source beneath Home. Install existing spec BuildRequires plus harness dependencies; build both profiles.
- [x] Reuse existing wrapper dispatch (input-guard direct, LocalSearch temporary cwd, all other filesystem/GUI tests under mounted disposable Home). Do not invent another runner or disable failing maintained cases.
- [x] Execute existing suites locally in the appropriate environment and retain logs. External-only suites remain separately recorded from ordinary Meson skips.
- [x] Review workflow permissions/triggers/log upload and report remote results only after an actual authorized run. If publication authorization is required, present the concrete branch/workflow/package state for approval as the final publication step.

## Task 3: Disposable package integration and focused remaining evidence

Files: `docs/miller/release-readiness.md`; owned ignored release artifacts/scripts only where necessary.
Consumes: exact RPM set and signed stock rollback artifacts; rootless Fedora44 image.
Produces: transaction log, before/fork/after identities and asset contracts; honest runtime limits.

- [x] In a disposable rootless container, install the exact signed stock main/extensions, then local fork set, then restore stock using DNF/RPM. Check package identities/integrity after every transaction. Do not reuse the host RPM database or writable user state.
- [x] Verify canonical desktop, schema, D-Bus interfaces and extension typelib/library assets from the installed fork. Native full-session service activation remains covered by private-host profile tests; a package-only container is not GNOME login/power-loss acceptance.
- [x] Preserve deployment failure-ordering/signature/offline/drain tests; identify unexercised full-desktop/reboot boundaries without claiming them tested.
- [x] Retain direct keyboard/accessibility-label/RTL evidence. Add `/miller/navigation/large-deep-responsive` to existing `test/automated/display/test-miller-state.c`, reusing its native Home guard, location-entry typing, model and key helpers: a real12-level branch with5000 regular files and one empty child, exact loaded model count, genuine Home/Right/Left input, actionable parent/empty child, and weak owner release after unrelated navigation/close. Record end-to-end entry/load time and main-context heartbeat gap as observations, not a new upstream benchmark or strict invented performance target. Run the focused case in both profiles with existing session/home wrappers; fix only a demonstrated integration defect. No new executable or fixture framework.
- [x] Report GPG/python module absence and outgoing foreign/real-remote limitations explicitly. Do not implement replacement providers.

## Task 4: Concrete readiness report

Files: `docs/miller/release-readiness.md`, `docs/miller/acceptance.md`, user-facing README/deployment commands.
Consumes: package identities, green maintained regressions, component review/corrections, CI evidence and transaction logs.
Produces: reviewable host deployment handoff with no known Important column regression.

- [x] Reconcile every current-goal requirement to actual evidence or an explicit permitted limitation. Archive scratch evidence before cleanup.
- [x] Update current readiness table and exact miller2 install command; use the existing restore wrapper and preserved stock snapshot.
- [x] Verify clean source/provenance/checksum consistency and support QR; report package/source identity, tested native paths, material limitations and exact commands.
- [x] Keep host installation separate. Do not mark the goal achieved until all required handoff artifacts and focused evidence exist.

## Self-review

Tasks1/3 cover package identity, contracts and rollback; Task2 covers CI reuse and safe test state; Task3 closes only missing current-goal UI/responsiveness evidence; Task4 audits/reporting. Existing components supply the alternate view and reviewed native integration; this plan does not replace or re-certify upstream. No new public API or cross-task signature is introduced. Remaining publication approval, if needed, follows a concrete reviewable result.
