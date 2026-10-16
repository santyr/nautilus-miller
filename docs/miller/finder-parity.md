# Capability parity matrix

Source baseline: `8618e792713ae6b248c4ca7d740ae78ce9d3040b`. Scope: [full goal](finder-completion-goal.md). Every row is inherited in Grid/List unless otherwise stated. Inheritance alone is not a Columns acceptance result. Native permission/capability checks, cancellation, conflict UI and error reporting remain authoritative.

| Capability | Existing action/API entry to audit | Context / availability / destination | Required acceptance | Current Columns evidence |
|---|---|---|---|---|
| Open / Open With | `open-with-default-application / open-with-other-application` | Selected files; folder disclosure is intercepted only for unmodified single-directory activation | C01,C16 | Source reuse; full end-to-end acceptance open. |
| New folder / templates | `new-folder / generated template actions` | Active writable backing directory, including empty ancestor columns | C02,C03 | Parent and empty-child native New Folder dialogs, delayed focus change and untouched sentinel verified in C integration; genuine input parity open. |
| Rename / multi-rename | `rename` | Captured selected files, native rename UI/provider | C04,C10 | Source reuse; full end-to-end acceptance open. |
| Copy / Cut / Paste | `copy / cut / paste` | Source selection snapshot; paste active backing directory, clipboard capabilities | C02,C03,C05 | Source reuse; full end-to-end acceptance open. |
| Duplicate | No direct action; same-directory copy/paste through existing operations | Selected files in their original directory | C05 | Source reuse; full end-to-end acceptance open. |
| Move / Copy to | `move-to / copy-to` | Selected files and native destination dialog snapshot | C04,C05 | Source reuse; full end-to-end acceptance open. |
| Links | `create-link / create-link-in-place` | Native backend and preference availability | C05 | Source reuse; full end-to-end acceptance open. |
| Trash / permanent delete | `move-to-trash / delete-permanently-menu-item` | Native trash capability and explicit permanent-delete confirmation; never silent fallback | C05,C10 | Source reuse; full end-to-end acceptance open. |
| Restore | `restore-from-trash` | Native Trash special view and original destinations | C11 | Source reuse; full end-to-end acceptance open. |
| Undo / Redo | `window undo / redo` | Existing operation undo manager; only supported operations | C05 | Source reuse; full end-to-end acceptance open. |
| Properties / permissions | `properties / current-directory-properties` | Selected files or captured backing directory | C04,C16 | Source reuse; full end-to-end acceptance open. |
| Compress / extract | `compress / extract-here / extract-to` | Native archive tools and destination snapshot; no new extraction engine | C04,C05 | Source reuse; full end-to-end acceptance open. |
| Open in Terminal | `console / current-directory-console` | Captured selection or active backing directory; installed terminal provider | C04 | Source reuse; full end-to-end acceptance open. |
| Bookmarks / volumes | `slot/window and sidebar native actions` | Branch navigation/mount capability, not an arbitrary file transfer destination | C09,C11 | Source reuse; full end-to-end acceptance open. |
| Extension actions | `MenuProvider / PropertiesModelProvider / InfoProvider` | Native loader and initiating view selection; list metadata columns stay in List | C04,C16 | Source reuse; full end-to-end acceptance open. |
| Drag/drop | `NautilusListBase and Nautilus DnD` | Pointer destination, negotiated action and captured source; no retargeting on focus | C06 | Source reuse; full end-to-end acceptance open. |

The action names above were checked against the current `view_entries` action table. Duplicate has no direct action in this baseline; expose or document a native same-directory copy workflow in Stage 4. Dynamic template and extension actions are created by their existing providers. Verification must assert filesystem contents/checksums and untouched siblings, not only UI labels. Existing direct transfer tests remain useful but do not close real Paste or pointer-drag requirements.

## Confirmed additions required

Inline inspector; canonical preview discovery in Miller; width persistence/fit/reset/keyboard controls; minimal reveal; scoped type-to-select; stronger history state; complete operation, DnD, remote, accessibility, performance, CI and deployment acceptance. Special-location fallback and existing Grid/List remain required. See [acceptance ledger](acceptance.md).
