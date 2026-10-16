# Nautilus Miller

Sat’s personal project: an independently maintained fork of GNOME Nautilus 50.4 with Finder-style Miller columns alongside Grid and List. This is not an official GNOME release. Questions, bugs and feature requests belong in the [Nautilus Miller GitHub issue tracker](https://github.com/santyr/nautilus-miller/issues); support is personal and best effort. See the [project README](README.md) for scope and attribution.

Select **Columns View** from View Options, or press **Ctrl+3**. Selecting a folder reveals its contents in the next column while keeping focus in the parent. Right enters the selected folder; Left returns to the previous column. Ancestor selections stay highlighted, each column scrolls vertically, and separators can be dragged to resize columns. Normal Nautilus file activation and operation shortcuts apply.

The toolbar cycles **List → Grid → Columns → List** in ordinary folders. Its icon, tooltip, and accessible name describe the next view. In special locations that do not support Columns, the toggle cycles List and Grid; selecting Columns explicitly still saves it for ordinary browsing. **Ctrl+1** selects List, **Ctrl+2** Grid, and **Ctrl+3** Columns.

Search, Recent, Starred, Trash, and the Network overview use their existing views. Columns returns when you resume ordinary folder browsing. This version has no file preview pane.

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
XDG_CONFIG_HOME="$(mktemp -d)" meson test -C build-miller --print-errorlogs
```

Use a temporary GTK configuration for tests because user themes can emit fatal GTK test warnings. The column tests cover path reconciliation, folder/file/multiple selection, ancestor highlights, resizing, focus, permission errors, history, view switching, live file-transfer updates, tabs, and search fallback. File transfers are exercised through Nautilus's existing operation API with the active column's backing URI; automated Wayland clipboard ownership requires a real input serial and is not used as an automated gate.

The inherited `test-portal-file-chooser` has a render/response timeout on this host, also reproduced with the unchanged upstream baseline. See `docs/miller/verification.md` for the actual results and deployment record.

## Source and maintenance

The upstream remote is https://gitlab.gnome.org/GNOME/nautilus, initially tag `50.4` (`87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`). The fork's public repository is https://github.com/santyr/nautilus-miller. Original GPL licensing and authorship remain in place.

Keep upstream changes on a separate branch for review:

```sh
git fetch upstream
git switch -c update/next-nautilus-release
git merge <reviewed-upstream-release-tag>
```

For a new upstream release, update the RPM version/source archive prefix and Fedora spec/patches together, increment the Miller release, rebuild, and repeat development and host integration checks before replacing the installed package. Fedora updates may supersede a locally installed fork; rebuild the fork against those updates to retain Columns.
