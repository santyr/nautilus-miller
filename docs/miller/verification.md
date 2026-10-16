# Host verification

Host: Fedora 44, x86_64. Upstream baseline: Nautilus 50.4, commit `87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`.

- Original upstream configured and compiled on the host.
- Original upstream headless suite: 18/18 passed with host display/session access.
- Isolated Miller development build configured and compiled; generated schemas and desktop identity validated.
- New path-state executable: 3 cases passed, including remote URI hierarchy and encoded filenames without network I/O.
- Existing file-view executable plus new compact-column/action-context checks: 11 cases passed.
- New Miller integration executable: 7 cases passed, including a full Nautilus window, transfer destination and directory-monitor update, tabs, search fallback/return, history, mode switching, focus, resizing, permission error, rapid branch replacement, cached ancestor highlighting, and revealing an offscreen automatic selection without stealing focus.
- Deployment argument/state validation: 3 cases passed.
- Full Meson run after implementation: 27/28 executables passed. `test-portal-file-chooser` timed out waiting for a rendered chooser response; the same test failed against the unchanged baseline binary. This is an outstanding host test limitation, not evidence that this portal test passed.

Graphical tests use temporary GTK configuration because this host's existing user theme emits a parser warning that GTK tests treat as fatal. Wayland programmatic clipboard ownership does not reliably persist without a real input serial; the automated transfer test uses the existing Nautilus operation API and verifies the active column's backing directory and resulting model update. Clipboard implementation is inherited from Nautilus.

## Final review

An independent read-only review of `87bba3c..b4d22b1` found no critical defect and one important defect: managed automatic selections suppressed scrolling along with focus. A 200-row endpoint-selection regression failed before the fix and passed after exposing the existing focus-preserving scroll helper. The test preserves entry focus while revealing the selected file, using the endpoint path used by FileManager1 ShowItems.

After the fix, all 27 other Meson test executables passed; the separately reproduced upstream portal timeout remains excluded from that run. Development-profile validation and all three deployment validation cases also passed.

Deferred minor: in Grid mode the toolbar toggle still advertises List View while its actual next mode is Columns. The explicit Columns menu entry and Ctrl+3 work independently.

## Packaging and deployment

The canonical Fedora replacement RPM and matching extension, development, debug, and source RPMs built successfully. Package metadata and file lists retain `/usr/bin/nautilus`, the canonical desktop entry, both Nautilus and FileManager1 D-Bus services, and the canonical schema, and add `/usr/bin/nautilus-miller`.

The final RPM was built from `a8e67bbe6b02b9e55cc9ca8c4c92e1b588d20e9e`. SHA-256:

```text
8dfc83f26eac769cc8bb4807d7fea94bc7be45a4082e20c00e6ae0892c2a8c90  nautilus-50.4-1.miller1.fc44.x86_64.rpm
c9fcddd9fddca739e7aee02fbc8a6c3a70c6f109eacac05ae0cefeb43887746a  nautilus-extensions-50.4-1.miller1.fc44.x86_64.rpm
```

An unprivileged `dnf install --assumeno` resolved an upgrade of exactly Nautilus and nautilus-extensions from `50.4-1.fc44` to `50.4-1.miller1.fc44`. It also reported that installation would invalidate an existing offline transaction initiated by `dnf5daemon-server`. The user explicitly chose to install the fork now and clear that queued transaction. No package installation has been performed. Sudo requires the user's local password.

Both stock RPMs were downloaded to `~/.local/state/nautilus-miller/stock-rpms` and their metadata checked. At the user's request, an executable, argument-free host entry point was created at `/home/sat/install-nautilus-miller.sh`; it delegates to the versioned installer with the explicit final RPM path and refuses execution as root, so preferences are written for the desktop user. Its shell syntax was checked. Installation and live default-handler acceptance remain pending the user's local sudo authentication.

## Execution decisions

- Used a dedicated newly cloned checkout and branch for isolation. There was no existing user work in that checkout; a second worktree offered no extra isolation. Cost if wrong: development would need moving to a separate checkout.
- Exercised file transfers with the existing operation API and active column backing URI because automated Wayland clipboard ownership requires a real input serial. Cost: interactive clipboard behavior relies on inherited upstream code rather than a passing automated clipboard end-to-end test.
- Kept the unrelated portal timeout as a documented baseline limitation and ran all other executables after the review fix. Cost: this host's portal response behavior remains unverified.
