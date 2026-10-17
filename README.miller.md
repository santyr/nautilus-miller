# Nautilus Miller

Sat’s personal project: an independently maintained fork of GNOME Nautilus 50.4 with Finder-style Miller columns alongside Grid and List. This is not an official GNOME release. Questions, bugs and feature requests belong in the [Nautilus Miller GitHub issue tracker](https://github.com/santyr/nautilus-miller/issues); support is personal and best effort. See the [project README](README.md) for scope and attribution.

Select **Columns View** from View Options, or press **Ctrl+3**. Selecting a folder reveals its contents in the next column while keeping focus in the parent. Right enters the selected folder; Left returns to the previous column. Ancestor selections stay highlighted, each column scrolls vertically, and separators can be dragged to resize columns. Normal Nautilus file activation and operation shortcuts apply.

Each column heading has **Column Options**: a keyboard-accessible width control, **Fit to Contents**, **Reset Width**, and **Use as Default**. Dragged widths are saved on release. Directory widths range from 180 to 1200 pixels and default to 280; the 128 most recently used overrides are remembered in the app's settings. Fit samples up to 128 loaded rows, so an unusually long unsampled filename may still be ellipsized; full names remain available by tooltip. Credential-bearing or unsupported remote URI identities are not remembered.

Navigation reveals the relevant column with minimal horizontal scrolling. Selecting a folder shows its parent and child together when they fit; otherwise it keeps the focused parent visible. Background loading and delayed location updates preserve manual scrolling. In right-to-left layouts, Left enters the selected child and Right returns to its parent. Arrow keys inside editors and popovers retain their native behavior.

Back/Forward remembers the active column, remaining selected files and both scroll positions. Reopening a closed tab and opening history in a new tab use independent copies of that browsing state. Returning from Grid/List or canceling search restores the prior Columns context unless you made a new selection there. Location-entry focus is preserved. These snapshots last for the running application; they do not introduce saved sessions across application restarts. If a history folder is renamed, its native bookmark is used and the outdated snapshot is discarded.

Typing selects the first matching loaded name in the active column, without launching it. Matching follows the current sort order and supports Unicode case folding and input-method text. Backspace removes a grapheme; Escape clears the prefix; it expires after one second of inactivity. Bare Space retains external preview. In **Preferences → General**, turn off **Type to Select in Columns** to restore stock type-to-search. **Ctrl+F** opens search in either mode; Grid and List retain their normal search behavior. Test coverage and remaining requirements are recorded in the [acceptance ledger](docs/miller/acceptance.md).

The toolbar cycles **List → Grid → Columns → List** in ordinary folders. Its icon, tooltip, and accessible name describe the next view. In special locations that do not support Columns, the toggle cycles List and Grid; selecting Columns explicitly still saves it for ordinary browsing. **Ctrl+1** selects List, **Ctrl+2** Grid, and **Ctrl+3** Columns.

Search, Recent, Starred, Trash, and the Network overview use their existing views. Columns returns when you resume ordinary folder browsing.

In the development build, **View Options → Show Preview** or **Ctrl+Shift+P** toggles the optional inspector. It follows the active column’s selection, including a parent whose child is still open. It shows file/folder metadata, honest multiple-selection totals, bounded UTF-8 text, still images, and PDF/media thumbnails when an installed GNOME provider supports them. Unsupported content has an explicit metadata fallback. Images use the existing glycin sandbox; documents and media use Nautilus’s existing thumbnail queue. There is no autoplay.

Remote contents are disabled by default. Enabling **Preview remote contents** also requires **Preferences → General → Performance → Show Thumbnails → All Files**. Reads remain bounded; unavailable content retains Open/Space actions. See [preview providers, privacy and limits](docs/miller/preview.md). Space uses installed Sushi; both build profiles have real-service restart, navigation, focus and ownership coverage. See the [preview review](docs/miller/preview-review.md) for evidence and remaining release requirements.

Drag the native divider or use **Preview width** to resize the inspector independently of directory columns (240–800 logical pixels; default320). Visibility and width survive restart. **Open** and **Properties** use Nautilus’s existing actions. **Close Preview** returns focus to the active listing if the inspector owned it; location-entry focus is retained when toggling from there. These development changes have not been installed over the current RPM.

## File operations in Columns

The focused column owns ordinary file actions. Paste, New Folder, New Document and Open in Console use its directory, including a parent whose child remains displayed. A context menu uses the clicked column; native dialogs retain their initiating selection when another column or tab becomes active.

| Action | Native input |
|---|---|
| Open / Open With | Enter / context menu |
| Copy, Cut, Paste | Ctrl+C, Ctrl+X, Ctrl+V |
| New Folder / New Document | Ctrl+Shift+N / background context menu |
| New Folder with Selection | Selection context menu |
| Select Items matching a pattern | Ctrl+S |
| Rename one or several files | F2 |
| Link from clipboard / link in place | Ctrl+M / Ctrl+Shift+M; link menu items are optional in Preferences |
| Trash / permanent delete | Delete / Shift+Delete with native confirmation |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z for supported operations |
| Properties / directory Properties | Alt+Enter / background context menu |
| Compress / Extract | Native context menu and destination chooser |
| Open directory in Console | Ctrl+Period when the Console provider is available |

Conflict, error, permission, cancellation and Trash availability follow Nautilus's native behavior. On the tested Wayland session, Ctrl+Shift drag negotiates Move; the protocol has no Link action. Use the native Create Link commands above for links. The current host lacks Console (`kgx`); its private provider contract has development coverage. Extension/OpenPGP and drag-and-drop acceptance remain open. See [operation evidence and limitations](docs/miller/operations-verification.md) for actual tested workflows; these development changes have not replaced the installed RPM.

## Build and run on Fedora 44

```sh
sudo dnf builddep nautilus
sudo dnf install git gcc meson ninja-build pkgconf-pkg-config
./tools/miller/build-dev.sh
./tools/miller/run-dev.sh "$HOME"
```

The development build uses `org.gnome.Nautilus.Miller` and separate Nautilus settings. It does not own the canonical FileManager1 service. GTK's shared file chooser preferences still follow the desktop.

## Install as the default Nautilus

Commit source changes before building the release archive:

```sh
./tools/miller/build-rpm.sh
./tools/miller/install-default.sh build-rpm/RPMS/x86_64/nautilus-50.4-1.miller1.fc44.x86_64.rpm
```

The RPM follows Fedora's Nautilus package layout, including its extension library, desktop entry, D-Bus contracts, and terminal patch. The installer validates the complete package set and host architecture, preserves the original settings and verified stock RPMs under `$XDG_STATE_HOME/nautilus-miller` (normally `~/.local/state/nautilus-miller`), requests sudo authentication, and closes Nautilus through its normal Quit action. It waits for background work; a refused drain prevents the package transaction. It then runs `sudo dnf --repo=fedora --repo=updates install <explicit matching RPM paths>` (including devel only if installed), selects Columns, launches, and verifies the installed packages, executable inode, canonical service owners, handler, and preference before recording success.

DNF's terminal transaction confirmation is enabled by default. Use `--yes` only for a reviewed, explicitly authorized transaction. A prepared or unknown offline update state stops deployment; `--allow-offline-reset` is a separate explicit opt-in to invalidating that queued transaction. Never pass it merely to bypass an unexplained error. Run the installer as the desktop user, without putting sudo in front of the script.

The original `before-install.json` is never rewritten by repeated installs or Miller updates. Rollback RPM identities, package digests, Fedora signatures and recorded SHA-256 checksums are verified; valid cached RPMs do not require another download. A current optional devel package gets a matching rollback version without changing the original snapshot. `deployment.json` records the current phase and any failure; a prior success is kept separately as `last-success.json`. A failed launch or verification does not create a new `installed.json`. See [deployment recovery](docs/miller/deployment.md).

Run the installed app with `nautilus` or `nautilus-miller`. Restore the saved Fedora packages and previous folder-view/default-handler settings with:

```sh
./tools/miller/restore-stock.sh
```

The installer requires the Fedora `fedora` and `updates` repositories to download the original RPMs before replacement. Rollback uses those saved RPMs. Both installation and rollback use the current user's desktop session and need sudo access for DNF.

## Checks

```sh
python3 tools/miller/check-profile.py build-miller
python3 test/miller/test-deployment.py
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" build-miller/test/automated/display/test-miller-view
python3 test/miller/test-preview-io.py
```

These are focused checks. GUI tests use a private bus/compositor, disposable XDG directories and a mounted disposable Home while leaving `HOME` unchanged. Do not run legacy filesystem tests against the operator’s Home. See [input/testing procedures](docs/miller/input-testing.md) and the [acceptance ledger](docs/miller/acceptance.md) for full regression evidence and remaining gates. Operation tests now include genuine clipboard input, an independent GTK source, native dialogs/conflicts, and real Trash/restore in a normal application child. Actual drag-and-drop, extension/OpenPGP and release acceptance remain required; see [operation evidence](docs/miller/operations-verification.md).

Historical runs recorded a `test-portal-file-chooser` render/response timeout, also reproduced with the unchanged upstream baseline. Fresh isolated regression suites pass that smoke test; the original failure remains in [verification](docs/miller/verification.md), and release portal acceptance is still tracked separately.

## Source and maintenance

The upstream remote is https://gitlab.gnome.org/GNOME/nautilus, initially tag `50.4` (`87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`). The fork's public repository is https://github.com/santyr/nautilus-miller. Original GPL licensing and authorship remain in place.

Keep upstream changes on a separate branch for review:

```sh
git fetch upstream
git switch -c update/next-nautilus-release
git merge <reviewed-upstream-release-tag>
```

For a new upstream release, update the RPM version/source archive prefix and Fedora spec/patches together, increment the Miller release, rebuild, and repeat development and host integration checks before replacing the installed package. Fedora updates may supersede a locally installed fork; rebuild the fork against those updates to retain Columns.
