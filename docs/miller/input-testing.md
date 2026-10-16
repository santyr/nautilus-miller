# Disposable genuine-input tests

Run unprivileged from a configured development checkout:

```sh
python3 tools/miller/with-test-session.py -- meson test -C build-miller --no-rebuild --print-errorlogs test-miller-input-guard test-miller-input
python3 test/miller/test-session.py
```

The harness starts an owned headless Mutter with1280×900 virtual monitor, private D-Bus and disposable XDG/TMP directories. It waits for the Wayland socket and for the RemoteDesktop service to belong to that exact compositor PID. It writes a private600-mode `runtime/miller-session.ini` and exports `MILLER_TEST_SESSION` to its child. `--inside` refuses a normal host runtime. Cleanup terminates only the owned process group/compositor and detaches only portal mounts inside that private runtime.

The test-only C input helper validates canonical owned/private paths, marker mode/UID, the Wayland socket, exact runtime/display/bus metadata, and a positive recorded PID. Before creating a session it connects explicitly to that private bus and verifies the actual RemoteDesktop owner PID and `/proc` executable/command line of the owned headless Mutter. Each input call verifies the owner again and targets the captured unique D-Bus owner, preventing a service-name restart from retargeting delivery. It retains the creating connection/session and closes it at cleanup. Missing metadata cannot fall back to host input devices or portals.

Mutter creates virtual input devices lazily. The helper establishes keyboard/pointer capabilities before GTK windows map, so they obtain server keyboard focus. Delivered keys and pointer buttons travel through Mutter and Wayland into GTK; the entry test does not emit GTK controller signals. It verifies `x`, Ctrl+A/Shift input and pointer focus, plus a live mismatched-marker-PID rejection. Nine separate metadata guard cases cover valid metadata, missing marker, marker/runtime modes, display/bus mismatch, host runtime, invalid PID and symlink. Ownership checks are enforced by implementation; these unprivileged fixtures do not fabricate a foreign-UID file.

Without the private harness, genuine-input cases explicitly skip because the environment is absent. Metadata guard tests remain displayless. A skip is not genuine-input acceptance. File clipboard/DnD, external IME conversion, accessibility and complete C05/C06/C13 still require their corresponding workflows and filesystem assertions.

The engine and application integration now have genuine key tests:

```sh
meson compile -C build-miller
python3 tools/miller/with-test-session.py -- meson test -C build-miller --print-errorlogs test-miller-typeahead-engine test-miller-typeahead-ui
```

The UI executable uses GTK's simple IM context, including real Ctrl+Shift+U Unicode preedit and Enter commit. It checks selection without activation/focus transfer, native Ctrl+F/Ctrl+L/F2/Enter, opt-out search, rapid selection→Right ownership, tab/focus reset, the live Preferences binding, and unchanged Grid/List type-to-search. This is distinct from external IBus/Anthy conversion acceptance, which is not yet complete. Engine tests separately cover injected preedit/commit lifetimes, incremental matching and expiry; injected signals do not establish real IME conversion.

Development input plan: [input and type-to-select](../superpowers/plans/2026-10-16-miller-input.md). See [acceptance](acceptance.md) for exact run evidence and remaining gates.
