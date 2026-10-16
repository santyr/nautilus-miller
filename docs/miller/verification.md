# Host verification

Host: Fedora 44, x86_64. Upstream baseline: Nautilus 50.4, commit `87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`.

- Original upstream configured and compiled on the host.
- Original upstream headless suite: 18/18 passed with host display/session access.
- Isolated Miller development build configured and compiled; generated schemas and desktop identity validated.
- New path-state executable: 3 cases passed, including remote URI hierarchy and encoded filenames without network I/O.
- Existing file-view executable plus new compact-column/action-context checks: 11 cases passed.
- New Miller integration executable: 6 cases passed, including a full Nautilus window, transfer destination and directory-monitor update, tabs, search fallback/return, history, mode switching, focus, resizing, permission error, rapid branch replacement, and cached ancestor highlighting.
- Deployment argument/state validation: 3 cases passed.
- Full Meson run after implementation: 27/28 executables passed. `test-portal-file-chooser` timed out waiting for a rendered chooser response; the same test failed against the unchanged baseline binary. This is an outstanding host test limitation, not evidence that this portal test passed.

Graphical tests use temporary GTK configuration because this host's existing user theme emits a parser warning that GTK tests treat as fatal. Wayland programmatic clipboard ownership does not reliably persist without a real input serial; the automated transfer test uses the existing Nautilus operation API and verifies the active column's backing directory and resulting model update. Clipboard implementation is inherited from Nautilus.

RPM build, independent review, installation, and default-handler verification are recorded below when performed.
