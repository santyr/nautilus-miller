#!/usr/bin/python3
"""Generate a profile's Nautilus schemas without changing GNOME/GTK schemas."""
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text()
text = text.replace('org.gnome.nautilus.', sys.argv[3] + '.')
text = text.replace('/org/gnome/nautilus/', sys.argv[4] + '/')
pathlib.Path(sys.argv[2]).write_text(text)
