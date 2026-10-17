# Extension compatibility

The fork preserves Nautilus extension API 4.1, the `libnautilus-extension-4` ABI, the production extension directory and the native module loader. Every Miller directory uses a real FilesView: selection menu providers receive that view's selected files, and background menu providers receive its backing directory. Properties model and information providers remain upstream implementations. Extensions adding List metadata columns do not automatically add those columns to the Miller presentation.

This is an architectural compatibility statement, not a claim that every extension has passed acceptance. Extensions built for older Nautilus/GTK APIs may need upstream migration. Python extensions also need a compatible `nautilus-python` provider. The development profile uses an isolated installation prefix, so system extension loading must be configured explicitly in its disposable test environment; release acceptance must also exercise the canonical RPM identity.

## OpenPGP

Fedora's `seahorse-nautilus` provides GnuPG file encryption/decryption and signing through Nautilus. It is available for Fedora 44 but is not currently installed on this host. Its menu integration is expected to remain compatible; the actual action workflow has not yet been tested. See [Fedora package information](https://packages.fedoraproject.org/pkgs/seahorse-nautilus/seahorse-nautilus/) and the [Nautilus MenuProvider contract](https://gnome.pages.gitlab.gnome.org/nautilus/iface.MenuProvider.html).

Under the [revised integration goal](finder-completion-goal.md), compatibility work focuses on the native MenuProvider receiving the correct column selection/background directory and retaining its initiating context. When a compatible OpenPGP module is available, exercise that boundary with disposable files and a temporary GnuPG home, without access to the user's keyring. Full encrypt/decrypt/sign/verify certification is follow-up work unless a Miller-specific defect requires it. A module loading or menu appearing does not establish a verified cryptographic workflow.

The2026-10-17 host preflight confirms that neither `seahorse-nautilus` nor `nautilus-python` is installed. Their runtime compatibility remains unverified; no extension installation or real-key operation was performed. Preserve native availability, errors and cancellation through the existing loader and FilesView actions. Do not add a Miller-specific extension loader or OpenPGP implementation.
