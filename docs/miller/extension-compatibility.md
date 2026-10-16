# Extension compatibility

The fork preserves Nautilus extension API 4.1, the `libnautilus-extension-4` ABI, the production extension directory and the native module loader. Every Miller directory uses a real FilesView: selection menu providers receive that view's selected files, and background menu providers receive its backing directory. Properties model and information providers remain upstream implementations. Extensions adding List metadata columns do not automatically add those columns to the Miller presentation.

This is an architectural compatibility statement, not a claim that every extension has passed acceptance. Extensions built for older Nautilus/GTK APIs may need upstream migration. Python extensions also need a compatible `nautilus-python` provider. The development profile uses an isolated installation prefix, so system extension loading must be configured explicitly in its disposable test environment; release acceptance must also exercise the canonical RPM identity.

## OpenPGP

Fedora's `seahorse-nautilus` provides GnuPG file encryption/decryption and signing through Nautilus. It is available for Fedora 44 but is not currently installed on this host. Its menu integration is expected to remain compatible; the actual action workflow has not yet been tested. See [Fedora package information](https://packages.fedoraproject.org/pkgs/seahorse-nautilus/seahorse-nautilus/) and the [Nautilus MenuProvider contract](https://gnome.pages.gitlab.gnome.org/nautilus/iface.MenuProvider.html).

Required C04/C16 acceptance uses disposable files and a temporary GnuPG home/key, without access to the user's keyring. Test encrypt, decrypt, sign and verify; assert recovered contents and signature result. Invoke from a parent while a child remains displayed, from an inactive column and after focus changes; only the captured selection may reach the extension. Compare Grid/List and Columns. Do not mark compatibility verified merely because a module loaded or a menu appeared.

Other installed properties providers and a simple Python menu provider must also be exercised. Preserve native availability rules, extension errors and cancellation. No extension installation or real-key operation was performed as part of this compatibility audit.
