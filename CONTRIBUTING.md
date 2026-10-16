# Contributing to Nautilus Miller

Nautilus Miller is Sat’s personal project, independently maintained from GNOME Nautilus. Contributions and support are handled on a personal, best-effort basis.

Use this repository’s [GitHub issues](https://github.com/santyr/nautilus-miller/issues) for bugs, questions and feature proposals, and submit pull requests to [santyr/nautilus-miller](https://github.com/santyr/nautilus-miller/pulls). For substantial changes, describe the proposed behavior in an issue first. GNOME’s support and contribution channels do not maintain this fork.

Follow the [Miller build and development guide](README.miller.md) to build and run the separate development profile. Consult the [acceptance status](docs/miller/acceptance.md) before making claims about supported behavior. Preserve upstream copyright notices and licensing.

## Commit messages

Use a short component prefix and explain the change, its reason and relevant verification in the commit body. Link issues from this repository when applicable.

Your commit message should look something like this:

```
tag: Short explanation of the commit

Longer explanation explaining exactly what's changed and why, whether
any external or private interfaces changed, what issue were fixed (with
issue number if applicable) and so forth. Be concise but not too brief.

Closes #1234
```

## File Chooser

The commands below are inherited upstream references for the `Devel` profile, not the default Miller development profile. Do not assume they exercise a Miller build without adapting the service identity and checking that profile’s portal support.

Developing for the Nautilus file chooser may be complicated because of the
portal implementation.  In order to call the development version of nautilus
(rather than the system installed version) use the below commands, while making
sure that you have the development version already running.  Note that the
below commands are somewhat redundant and include default keys in order to make
updating them easier. For example, `OpenFile` defaults to `directory = false`,
but you can easily open a directory file chooser by changing `false` to `true`.

##### Open a file or directory

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.OpenFile '/org/gnome/Nautilus.Devel' 'org.gnome.Nautilus.Devel' '' 'Open a File' "{'filters': <[('All Files', [(uint32 0, '*')]), ('Images', [(uint32 0, '*.ico'), (uint32 1, 'image/png')]), ('Text', [(uint32 0, '*.txt')])]>, 'current_filter': <('All Files', [(uint32 0, '*')])>, 'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>, 'directory': <false>, 'multiple': <false>, 'modal': <true>, 'accept_label': <'Open'>, 'current_folder': <b'$HOME'>}"
```

##### Save a file

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.SaveFile '/org/gnome/Nautilus.Devel' 'org.gnome.Nautilus.Devel' '' 'Save a File' "{'filters': <[('All Files', [(uint32 0, '*')]),('Images', [(uint32 0, '*.ico'), (uint32 1, 'image/png')]), ('Text', [(uint32 0, '*.txt')])]>, 'current_filter': <('All Files', [(uint32 0, '*')])>, 'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>,  'modal': <true>, 'accept_label': <'Save'>, 'current_file': <b'$HOME/test_file'>}"
```

##### Save multiple files

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.SaveFiles '/org/gnome/Nautilus.Devel' 'org.gnome.Nautilus.Devel' '' 'Save Files' "{'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>,  'modal': <true>, 'accept_label': <'Save'>, 'current_folder': <b'$HOME'>}"
```
