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

The UI executable normally uses GTK's simple IM context, including real Ctrl+Shift+U Unicode preedit and Enter commit. It checks selection without activation/focus transfer, native Ctrl+F/Ctrl+L/F2/Enter, opt-out search, rapid selection→Right ownership, tab/focus reset, the live Preferences binding, and unchanged Grid/List type-to-search. Engine tests separately cover injected preedit/commit lifetimes, incremental matching and expiry; injected signals do not establish real IME conversion.

## External Japanese IME and restart persistence

These separate acceptance commands require installed IBus, Anthy, Python GI/IBus introspection and Bubblewrap, in addition to the normal development dependencies:

```sh
sudo dnf install mutter bubblewrap ibus ibus-anthy python3-gobject
python3 test/miller/test-ime-session.py
python3 tools/miller/with-test-session.py -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" python3 tools/miller/with-test-ime.py -- build-miller/test/automated/display/test-miller-typeahead-ui -p /miller/typeahead-ui/anthy-composition
python3 test/miller/test-state-restart.py
```

The IME helper requires exact private session metadata and all owned XDG paths before starting. It masks the user's home with an empty owned mount, keeps other host files read-only and leaves `HOME` unchanged. A device/inode and read-only-root proof runs before any engine. The IBus daemon and its engines inherit that mount namespace, protecting Anthy's legacy home paths as well as its XDG dictionaries. A private Unix bus/address file selects Anthy; inherited IBus discovery is cleared. The host PID namespace is retained because IBus parent checks and external-client authentication require it. Cleanup exits only this owned IBus instance; the outer session also cleans its owned process group. The real desktop's IBus selection is untouched.

The Anthy case sends actual `nihon`, Space and Enter keys. It observes Japanese preedit and conversion, verifies no preview action during Space and no activation/focus transfer on Enter, then exercises switching and closing a composing tab. It is skipped in the normal simple-IM run; the explicit command above must pass to establish this acceptance.

The restart command uses a private native dconf database and two separate application processes. The writer changes a real column width and the Preferences switch, then exits. The reader verifies the restored numeric control/size request, default width, preference row and a newly opened column using the persisted default. It checks a real dconf file was written and records distinct process IDs. The normal test run skips this case; a same-process settings instance is not restart evidence.

Development input plan: [input and type-to-select](../superpowers/plans/2026-10-16-miller-input.md). See [acceptance](acceptance.md) for exact run evidence and remaining gates.

## Native history input

```sh
python3 tools/miller/with-test-session.py -- meson test -C build-miller test-miller-state --print-errorlogs
```

This executable includes strip snapshot tests and native-window Back/Forward, view/search and tab integration. Its `keys-entry` case delivers actual Alt+Left/Right and Ctrl+L through the guarded private compositor, then verifies endpoint, action directory and retained location-entry focus. Other cases invoke native actions/APIs directly and assert directory identities, selections and GTK adjustments; they are not additional genuine-input coverage. The separate workflows below cover intentional delay and additional navigation entry points. Real remote services and accessibility remain separate acceptance requirements.

## Intentionally delayed GVfs backend

Requires `gvfs-archive`, Python GI/GIO and Linux PIDfds, alongside the private compositor dependencies:

```sh
python3 test/miller/test-delayed-backend.py
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-delayed-backend.py -- meson test -C build-miller test-miller-state --test-args '-p /miller/history/delayed' --print-errorlogs
```

The helper mounts its own read-only ZIP with A/a.txt and B/b.txt in the disposable session. It obtains the actual canonical root from `GMount`; it does not guess GVfs's escaping. It requires the exact private metadata, current owned headless compositor and XDG tree. Only an archive backend with the current UID, exact executable and matching private bus can supply its retained PIDfd. Children send bounded pause/resume requests to a private600-mode Unix socket; no request can select a PID. The parent resumes before unmounting, handles failed children and termination, and reaps its own child. Cleanup still resumes the authorized process if a child corrupts the marker. The default five-second pause deadline resumes the backend and makes the entire run fail, so it cannot manufacture a responsiveness pass. An explicit `--pause-seconds` integer from1–10 can extend the acceptance window; the standalone preview deadline test uses8 to prove its own five-second timeout while the backend remains stopped. All existing history commands retain the default5.

Eleven Python cases cover these guards, actual enumeration held pending and then completed, fragmented/rejected requests, child failure, changed metadata, the pause deadline and termination cleanup. Four GTK cases cover real-key A→B replacement, navigation away, closing a genuinely loading tab, and monitor-creation latency/cancellation. Normal Meson runs explicitly skip these four when the backend fixture is absent. The external command above establishes the delayed-backend evidence. This read-only archive is not SFTP/SMB, authentication, reconnect, live remote monitoring or a performance benchmark.

## Disposable Home and navigation entry points

Build outside the mounted home, then run the built executable:

```sh
meson compile -C build-miller
python3 test/miller/test-home-session.py
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- python3 tools/miller/with-delayed-backend.py -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" build-miller/test/automated/display/test-miller-state -p /miller/navigation
```

`with-test-home.py` first validates the existing private session. Bubblewrap masks the actual passwd home path with an owned disposable directory, leaves `HOME` unchanged, mounts the host root read-only, and rebinds only the already opened checkout read-only. Private XDG directories and the fixture home remain writable. A device/inode and mount proof runs before the child. The client bus and compositor start inside this namespace so the input helper can still verify their `/proc` identities. The archive helper must run inside that client session, as shown. Five Python cases cover host refusal, actual masking and write boundaries, child status/cleanup, compositor identity and mismatched metadata.

Ten navigation cases cover initial listing focus and Ctrl+L/Escape, absolute-path and escaped-URI entry, Ctrl+2/1/3 mode switching with and without selection, pointer breadcrumb and sidebar bookmark activation with keyboard Back, Alt+Home and return, Ctrl+Enter background-tab and Shift+Enter new-window activation, native Recent/Starred/Trash/Network fallback and return, and entry into an actual mounted GVfs archive followed by Back. Pointer tests check the actual Wayland device position before delivering buttons. They do not emit button signals. Selection setup and special-view actions use native APIs directly; they are not additional real input claims. Starred uses the test's empty dummy tag manager, not a live starred-file index.

These cases assert committed `GFile` locations and the current action owner. Mode/history cases also assert directory-scoped selections. The normal Meson run explicitly skips these ten without the mounted-home environment; the external run supplies their evidence. The archive mount does not establish sidebar volume activation, SFTP/SMB, authentication, writable remote operations or reconnect behavior.

## External IME observation after history integration

At5c875390a, one extra Anthy run failed on `IBUS_ATTR_TYPE_HINT should not happen` in the installed IBus GTK module; unchanged-code GDB and normal reruns passed. See `/tmp/nautilus-finder-history-anthy{,-backtrace,-repeat}.log`. The [IBus1.5.34 GTK source](https://github.com/ibus/ibus/blob/1.5.34/client/gtk2/ibusimcontext.c) contains that warning, and its [input-context source](https://github.com/ibus/ibus/blob/1.5.34/src/ibusinputcontext.c) forwards synchronous post-process preedit without the conversion used in its asynchronous path. That is a candidate provider cause, not a proven complete diagnosis or fix. The intermittent result remains open for expanded IME acceptance. No warnings were suppressed and no installed provider library was changed. The actual dconf restart check passed with writer100664/reader100913 in `/tmp/nautilus-finder-history-restart.log`.

## Standalone inspector metadata and bounded text

Build first, then exercise all22 cases, including the actual paused-backend deadline:

```sh
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- python3 tools/miller/with-delayed-backend.py --pause-seconds 8 -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" build-miller/test/automated/display/test-miller-preview
python3 test/miller/test-preview-io.py
```

The ordinary Meson executable passes20 cases and explicitly skips2 requiring the actual archive fixture. Cases cover empty/folder/multiple metadata, partial and unknown cached totals, yielding aggregation, bounded/invalid/empty/UTF8 text, nonblocking FIFO and symbolic-link rejection, controlled file replacement, hiding/inactive/disposal, retained entry focus and reentrant metadata/text/loading callbacks selecting a newer target. A folder inotify watch asserts no directory open. The2048-item aggregation fixture repeats cached file objects; it is not a real large-directory benchmark.

The two Python I/O observations use separate actual private sessions. Strace observes each native preview open with `O_NONBLOCK|O_NOFOLLOW`, two reads totaling exactly131072 bytes each and closed descriptors. Actual GVfs protocol logging observes metadata `QueryInfo` and no `OpenForRead` while remote content is disabled. The paused-backend case verifies the preview timeout near five seconds while the owned backend is still stopped; resuming then selecting a local file succeeds. None of this substitutes for SFTP/SMB, image decoding, external Sushi or integrated pane acceptance.

## Integrated inspector and persistence

```sh
meson compile -C build-miller
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" build-miller/test/automated/display/test-miller-preview-ui
python3 test/miller/test-preview-restart.py
```

Nine ordinary cases cover parent and empty-child targets, history, Grid/special-view return, inactive/closed tabs and weak destruction, control focus/close, independent sizing, actual pointer enlargement/shrinking/minimum and repeated genuine Ctrl+Shift+P with a focused Ctrl+L entry. The tenth case skips unless the separate-process helper supplies its phase. That helper shares only private dconf/XDG and mounted disposable Home between two actual processes; it checks visibility,410-pixel pane sizing, remote-policy persistence, distinct PIDs and a real dconf database.

Pane resizing uses GtkPaned and native mouse interaction. State persistence observes the divider position on release; it does not implement a second gesture. Open/Properties buttons resolve the existing Nautilus actions in the current action context. The directory scroller retains its minimum size when the inspector is hidden. [GTK4.22.5’s implementation](https://github.com/GNOME/gtk/blob/4.22.5/gtk/gtkpaned.c) describes its native shrink/resize policy. Existing New Folder wrong-target assertions also run with the inspector visible. These cases do not replace the required screen-reader/scaling/RTL or file-operation/clipboard/drag workflows.

## Renderer and remote-content acceptance

After building, the full standalone inspector run includes real providers and a paused owned backend:

```sh
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- python3 tools/miller/with-delayed-backend.py --pause-seconds 8 --preview-content -- env GTK_A11Y=test GSETTINGS_SCHEMA_DIR="$PWD/build-miller/data" build-miller/test/automated/display/test-miller-preview
python3 test/miller/test-preview-io.py
```

Without the backend, cases that need it explicitly skip. The optional payload preserves the default archive contents for navigation/history tests and adds only owned200KiB text and a64×32 PNG. GdkPixbuf generates the PNG using the installed provider; FFmpeg generates finite owned VP8/WebM and PCM/WAV media for native thumbnail tests. These are test tools, not a new production decoder dependency.

I/O observations include actual native descriptor flags/closure and128KiB read totals, real glycin sandbox executions, exactly two glycin load calls during1,000 selection replacements, remote-off QueryInfo without OpenForRead, exactly131072 bytes in32 shared GIO read calls for an enabled200KiB remote text input, and an independent GDB count of262144bytes/64GIO calls for the two native reads. Syscall records go to strace’s owned runtime output file and are read after completion, separately from concurrent service diagnostics. The GDB observers do not mutate application state; call counts do not establish physical child-process concurrency, RSS or input latency. Source-line observation excludes nested backend reads and avoids optimized-library return-value assumptions.

The cache guard proves mounted Home identity and read-only host/source mounts before creating Home/.cache. Other XDG directories retain the owned temporary tree. Added guard tests reject a forged marker, a different mask inode and a symlink cache. This layout is needed by the existing GNOME Desktop provider sandbox, which binds its cache at the same absolute path; a nested /tmp cache created extra directories in its output mount and triggered a native cleanup warning. It does not permit the operator’s Home or cache.

### GTK focus retention characterization

The small [probe](fixtures/gtk-tab-close-probe.c) contains only GTK/libadwaita code. Compile outside the read-only test namespace and run both variants inside it:

```sh
cc docs/miller/fixtures/gtk-tab-close-probe.c -o build-miller/gtk-tab-close-probe $(pkg-config --cflags --libs gtk4 libadwaita-1)
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- env GTK_A11Y=test build-miller/gtk-tab-close-probe
python3 tools/miller/with-test-session.py -- python3 tools/miller/with-test-home.py -- env GTK_A11Y=test build-miller/gtk-tab-close-probe wait
```

On GTK4.22.5/libadwaita1.9.4 the empty-replacement, immediate switch/close retains the old child and exits23; separated actions release it and exit0. A focusable replacement does not reproduce. This is an explicit inherited observation, not a passing preview test or a waived resource acceptance requirement. The application tests now cover both a pre-existing background tab and immediate new-tab/close during decoding. Nautilus clears remaining focus only when closing an inactive slot that still owns it, using GTK’s existing focus API. Both assert weak destruction; genuine Ctrl+L remains focused when closing another tab. The pure library probe is retained without that application guard.

## Actual external Sushi acceptance

Build both profiles without installing the canonical build, then run the maintained real-provider suite:

```sh
meson setup build-preview-canonical -Dprofile= -Dtests=all
meson compile -C build-miller
meson compile -C build-preview-canonical
python3 test/miller/test-quick-look.py -v
```

The suite uses installed `/usr/libexec/org.gnome.NautilusPreviewer` on owned private buses/compositors and mounted disposable Home. It checks genuine Space/Down/Escape/repeated Space, native tab transitions, focus return, absence, service restart, latest pending selection, pending entry/tab invalidation and another client's real exported window handle. Tab creation/selection/closure use native APIs; they are not claimed as genuine tab key input. Real D-Bus `ShowFile` messages must match the expected URI/order and have method-return acknowledgments from the actual service; the basic case also requires its real `SelectionEvent`. GLib parses message bodies, including escaped foreign-window handles.

`--without-previewer` writes an activation override only inside owned test XDG data before the private bus starts. It never modifies the installed service. Restart tests launch the real installed provider with its existing `SUSHI_PERSIST` setting, verify its D-Bus PID/executable, and crash/reap only the retained `GSubprocess` child. No host PID is signaled. Pending-invalidation cases observe the whole deadline and then prove a fresh eligible request still works. Cross-client cases use a second real GTK window and actual provider calls, without fabricating visibility or selection signals.

Ordinary Meson runs explicitly skip the external-service cases. The separate suite rejects skips; installed Sushi and both built profiles are required. These fixtures isolate files, settings, bus and compositor; they do not yet provide a private network namespace. Real SFTP/SMB and network isolation remain separate required work.
