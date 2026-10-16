# Nautilus Miller columns fork

## Purpose and accepted direction

Create a fork of GNOME Nautilus that Sat can build and run on this Fedora 44 host. Miller columns are an alternative to the existing grid and list views. The user approved basing the fork on Nautilus 50.4, integrating with Nautilus's existing windows, tabs, file models, and operations, and giving the development build a separate application identity and installation prefix. Once complete, the user wants the fork to replace stock Nautilus and run as the host's default file manager.

The initial upstream commit is `87bba3cbe6aa2b13a08aebe1330d61c967b1f6d0`, release tag `50.4`. The checkout is `/home/sat/Documents/coding/nautilus-miller`; the development branch is `miller-columns` and the GNOME repository is the `upstream` remote.

## User experience

The view selector offers Grid, List, and Columns. The selected view can be remembered within the fork, without storing the new enum value in system Nautilus's settings. Switching views preserves the current location and any selection that exists at that location.

Columns display directory contents as compact lists with file icons, names, and folder indicators. Each column scrolls vertically; the whole strip scrolls horizontally. Columns have a usable minimum width, and the newest column is brought into view. The first version uses a consistent column width rather than adding a width editor.

Opening a location directly builds its ancestor chain up to the GIO root, highlights the corresponding child in each ancestor column, and displays the requested location's contents at the right. Ancestors load asynchronously. Remote URI roots remain within their own URI hierarchy; there is no traversal across unrelated mounts or URI schemes.

Selecting a single directory opens its contents in the next column and discards any columns to its right. Selecting a file leaves it selected without launching it and removes the descendant branch. Multiple selection remains within one column and does not open a new column. File activation uses Nautilus's existing Open behavior. A directory can also be activated to enter or focus its child column. Modified activation for opening another tab or window continues to use Nautilus's existing behavior.

Up and Down navigate rows. Right enters the selected directory and focuses its contents. Left focuses the previous column and restores its selected child. Enter activates the selection; existing file-operation shortcuts continue to act on the focused column. Focus, selected branch highlights, and the column that owns the actionable selection must remain visually distinguishable. Each list and column has an accessible name and keyboard focus path.

The location bar and navigation history track the opened directory at the end of the current branch. Back and Forward restore that directory and reconstruct the corresponding column chain. Clicking an ancestor breadcrumb or opening a new location reconciles the chain rather than retaining unrelated columns.

## Implementation structure

Nautilus 50.4 has a final `NautilusFilesView` that owns a directory, a `NautilusViewModel`, file actions, and one `NautilusListBase` presentation. Grid and list presentations derive from `NautilusListBase`. A Miller strip introduces several displayed directories, so its implementation must explicitly coordinate displayed columns with the existing single active action context.

Introduce a Miller presentation and a small column controller. The strip owns the ordered directory chain, column widgets, focus transitions, and horizontal scrolling. Each column owns its directory identity, loading state, list model, selection, and directory-monitor lifetime. Keep branch reconciliation separate from row rendering so path changes and delayed completion can be reasoned about independently.

Use Nautilus's directory and file objects for enumeration, metadata, sorting, filtering, monitoring, and file identity. Reuse existing row cells and list interaction code where their assumptions fit. Extend the view adapter only where needed to expose the focused column's model, selection, and backing directory to the existing file actions. Do not silently treat the rightmost directory as the destination for operations invoked in another column.

The focused column supplies the actionable selection and the target for Paste and New Folder. Context menus use the column under the pointer. Drag sources contain the files dragged from their source column; drop destinations use the indicated folder or column background. Rename, Trash, Copy, Cut, Paste, Properties, and Open call the existing Nautilus operations and error reporting. Column state updates from directory monitors after those operations.

Register a distinct Columns view ID without reusing the special Network view ID. Update the view factory, validation, selector, and relevant preferences. Avoid relying on the current two-state toggle to make a third mode undiscoverable. Keep changes to unrelated file operations and existing views narrow.

## Loading, errors, and special locations

Loading a directory never blocks the GTK main loop. Cancel or detach pending work and monitoring when a column is discarded. Tag branch requests so a late completion cannot restore an obsolete column. Release models, handlers, and directory references when closing a tab or changing views.

An empty directory shows an empty state within its own column. Permission failures, removed directories, and disconnected remote locations show a local error state with retry or navigation back to an accessible ancestor. Losing one branch must leave accessible ancestor columns usable. Symlinks follow Nautilus's normal activation and URI rules; branch reconciliation must avoid following canonical parent cycles indefinitely.

Ordinary local directories and hierarchical GIO locations support Columns. Search results, Recent, Starred, Trash, and the special Network overview use the existing appropriate view for their flat or special models. The user's Columns preference is retained and restored on returning to an ordinary directory. This first version does not add a file preview pane or redesign the file chooser portal.

## Fork identity, build, and execution

Provide two build profiles. The development profile displays **Nautilus Miller** and uses application ID `org.gnome.Nautilus.Miller`. Its application instance, settings path, desktop entry, and launch command must be distinct. Install this profile into `$HOME/.local/opt/nautilus-miller`, with a user launcher named `nautilus-miller`. Ensure the launcher finds the fork's schemas, resources, and libraries. Scope development portal and file-manager service registration to its development identity.

The replacement profile uses the standard Nautilus application ID, executable, desktop entry, GSettings paths, and D-Bus service contracts. Package it as a Fedora RPM derived from the host's Nautilus source package, with an identifiable Miller release suffix. Install through RPM/DNF so the package database tracks the replacement and extension libraries remain consistent. Do not overwrite package-owned files with an unmanaged Meson install into `/usr`.

After functional verification, install the replacement package and select its desktop entry as the handler for `inode/directory`. Stop the old Nautilus instance before launching the fork. Verify folder opening, `org.freedesktop.FileManager1` requests such as ShowItems, and the desktop launcher. Preserve the canonical file chooser integration shipped by the Nautilus package rather than introducing a separate default-chooser change. Existing grid/list settings remain valid; set Columns as the user's default folder view in the replacement profile while retaining the view selector.

Document how to restore Fedora's Nautilus package and its previous default folder handler. Keep the original handler value and package version as deployment records. Rebuild the fork against later Fedora/upstream updates as a documented maintenance task.

The host already has Git, GCC, Ninja, DNF's builddep plugin, and Nautilus 50.4. Meson and the GTK/libadwaita development packages are absent. The user requested setup commands rather than an unreviewed system package installation. Supply these Fedora commands:

```bash
sudo dnf builddep nautilus
sudo dnf install git gcc meson ninja-build pkgconf-pkg-config
```

The upstream source has already been downloaded. To reproduce that checkout elsewhere:

```bash
git clone --branch 50.4 https://gitlab.gnome.org/GNOME/nautilus.git nautilus-miller
cd nautilus-miller
git switch -c miller-columns
git remote rename origin upstream
```

Provide a build-and-run script and concise fork documentation after implementation. Resolve the exact Meson flags against the implemented fork rather than documenting flags that do not yet exist.

## GitHub repository

Keep the GNOME URL as `upstream` and use the user's new GitHub repository as `origin`. Preserve the upstream license and authorship. The checkout is currently shallow; fetch the upstream history before publishing a normal historical fork. Prepare a clear README explaining the fork, limitations, build instructions, and how to merge future upstream releases.

The repository destination and visibility have not been specified. Prepare the local branch and commits, then obtain those details before creating or publishing the GitHub repository. Do not claim the remote exists until creation succeeds.

## Verification and acceptance

Compile the fork on the host after the user installs the required packages. Run relevant upstream headless checks and focused GTK checks for view construction, switching, selection routing, and asynchronous branch replacement. Use temporary directories for file-operation checks.

Demonstrate nested navigation, selecting a sibling branch, opening a direct deep path, keyboard movement, Back/Forward, and switching Columns to List/Grid and back. Exercise an empty directory, a denied or removed directory, rapidly changing branches while loading, hidden files, and rename/copy/paste/trash actions against disposable files. Check that a late callback cannot resurrect a discarded branch.

Launch the development build with system Nautilus available and confirm its separate application identity. Verify that its desktop entry and settings belong to the fork. After installing the replacement RPM, confirm the running executable belongs to the Miller package, Columns is the default folder view, opening a directory invokes the fork, and FileManager1 requests reach the fork. If display access or package installation prevents a real launch, report that specific limitation and provide the launch command; a successful compile alone is not evidence of a working interactive view.

Acceptance is a runnable Nautilus-derived app on this host with a discoverable Columns view, functioning column navigation and ordinary file operations, installed as the default replacement for stock Nautilus, and a local Git branch ready for the user's GitHub repository. GitHub publication is complete only after the destination is supplied and the push succeeds.
