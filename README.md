# Nautilus Miller

Nautilus Miller is **Sat’s personal project**, an independently maintained fork of GNOME Nautilus with Finder-style Miller columns alongside Grid and List. It is not an official GNOME release. Development currently targets Fedora 44 and GNOME/Wayland, based on Nautilus 50.4.

- [Build, run, install and roll back](README.miller.md)
- [Current completion scope](docs/miller/finder-completion-goal.md)
- [Feature and acceptance status](docs/miller/acceptance.md)
- [Extension compatibility](docs/miller/extension-compatibility.md)
- [Contributing to this personal project](CONTRIBUTING.md)

## Support, bug reports and feature requests

Use the [Nautilus Miller GitHub issue tracker](https://github.com/santyr/nautilus-miller/issues) for questions, bugs and feature requests about this fork. Include the fork revision or RPM version, Fedora/GNOME versions, reproduction steps and relevant logs with private information removed. Development and support are provided on a personal, best-effort basis.

Please keep reports about this fork in this repository. GNOME’s issue tracker, Discourse and design channels are not support channels for this project.

## Support this project

If you would like to support this personal project, you can send sats over Lightning.

**Lightning address:** `sat@bolverker.com`

![Lightning payment QR code for sat@bolverker.com](docs/miller/assets/lightning-support.png)

## Runtime dependencies

Build dependencies and installation commands are documented in the [Miller guide](README.miller.md). Inherited runtime components include:

- [Bubblewrap](https://github.com/containers/bubblewrap), used for process isolation.
- [LocalSearch](https://gitlab.gnome.org/GNOME/localsearch), used for search, metadata extraction, starred files and batch renaming.
- [xdg-user-dirs-gtk](https://gitlab.gnome.org/GNOME/xdg-user-dirs-gtk), used for default bookmarks and localization.

## Extensions

The fork retains the native Nautilus extension interfaces. See our [compatibility status and test requirements](docs/miller/extension-compatibility.md). Upstream technical references are available for [libnautilus-extension](https://gnome.pages.gitlab.gnome.org/nautilus/) and [nautilus-python](https://gnome.pages.gitlab.gnome.org/nautilus-python/).

## Upstream attribution and license

This project is derived from [GNOME Nautilus / Files](https://gitlab.gnome.org/GNOME/nautilus), initially release 50.4. The original authors’ copyright notices and GPL licensing remain in place; see [LICENSE](LICENSE). Upstream source and API links are provided for attribution and technical reference. This repository’s builds, changes and support belong to the Nautilus Miller personal project.
