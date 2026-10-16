# Nautilus Miller

A fork of Nautilus 50.4 adding Finder-style Miller columns as an alternative to Grid and List.

## Build and run on Fedora

```sh
sudo dnf builddep nautilus
sudo dnf install git gcc meson ninja-build pkgconf-pkg-config
./tools/miller/build-dev.sh
./tools/miller/run-dev.sh "$HOME"
```

The development application uses `org.gnome.Nautilus.Miller` and its own Nautilus settings. It does not own the system FileManager1 service. GTK's shared file chooser preferences still follow the desktop.

The default Meson profile retains Nautilus's standard identity for the Fedora replacement package. Packaging and deployment instructions will be added with that implementation.

Upstream is https://gitlab.gnome.org/GNOME/nautilus, release 50.4. The original GPL licensing and authorship apply. See the approved design and plan under `docs/superpowers/`.
