#!/usr/bin/python3
"""Check the built development identity and its settings isolation."""
import pathlib
import sys
import gi
from gi.repository import Gio, GLib

build = pathlib.Path(sys.argv[1]).resolve()
source = Gio.SettingsSchemaSource.new_from_directory(str(build / 'data'), None, False)
assert source.lookup('org.gnome.nautilus.miller.preferences', False) is not None, 'development schema missing'
assert source.lookup('org.gnome.nautilus.preferences', False) is None, 'stock settings schema leaked into development build'
desktop = GLib.KeyFile()
desktop.load_from_file(str(build / 'data/org.gnome.Nautilus.Miller.desktop'), GLib.KeyFileFlags.NONE)
assert desktop.get_string('Desktop Entry', 'Name') == 'Nautilus Miller'
assert '.local/opt/nautilus-miller/bin/nautilus' in desktop.get_string('Desktop Entry', 'Exec')
assert not (build / 'data/org.freedesktop.FileManager1.service').exists(), 'development build claims canonical service'
print('Development profile: schemas and desktop identity are isolated')
