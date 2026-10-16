# Completion acceptance ledger

Full authority: [goal](finder-completion-goal.md). Historical results stay in [verification](verification.md); no historical pass closes an expanded requirement without new evidence. Status **open** means incomplete, not failed or unsupported. **Blocked** requires a concrete missing environment and is never a pass.

Development branch: `finder-completion`; baseline `8618e792713ae6b248c4ca7d740ae78ce9d3040b`. Worktree `.worktrees/finder-completion`. Installed package at start: `nautilus-50.4-1.miller1.fc44`; no production changes authorized by this goal.

| ID | Required scenario | Owning stage | Status | Revision / procedure / evidence |
|---|---|---|---|---|
| C01 | Late branch replacement | Stage 1/2 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C02 | Paste/create in focused parent | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C03 | Empty-column destination and Left | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C04 | Captured context menu/dialog destination | Stage 1/4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C05 | Real clipboard, conflicts, undo/redo | Stage 4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C06 | Nested spring-loaded drag/cancel | Stage 4 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C07 | Safe stale-proof inline preview | Stage 3 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C08 | External preview both profiles/restart | Stage 1/3 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C09 | Layout, history, per-tab state | Stage 2 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C10 | Ancestor mutation, remote disconnect | Stage 5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C11 | Special views and return state | Stage 2/5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C12 | External reveal last row | Stage 1/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C13 | Accessibility, keyboard, RTL/scaling | Stage 2/5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C14 | Large/deep/lifetime benchmarks | Stage 5 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C15 | Disposable install/update/restore failures | Stage 1/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |
| C16 | Grid/List, extensions, portal | Stage 0/6 | Open | Expanded acceptance pending; baseline evidence not sufficient. |

## Evidence requirements

Each result records source revision, named test or reproducible manual steps, session/backend, pass/fail/blocked status, log/artifact path and remaining limitation. Before/after logs accompany defect fixes. Real clipboard/drag needs compositor-supported input; signal emission and direct transfer calls do not satisfy it. Real SFTP/SMB must contact disposable servers and cover authentication cancellation, delays, disconnect/reconnect and unavailable monitoring. Packaging tests must include real disposable transactions, not only mocks. CI requires an observed run, not only a YAML file.

## Baseline environment discovery

Fedora 44 / GNOME Wayland; GTK 4.22.5, libadwaita 1.9.4, Meson 1.11.2, Sushi 50.0. Native Mutter and Podman are present. `/dev/kvm` exists. `/dev/uinput` is root-only; no ydotool/dotool, sshd or smbd executable found on PATH. These are harness discovery facts, not yet blockers: nested compositor/libei, user containers and downloadable disposable VM/service tools remain options. Never use the real desktop's files for fixtures.
