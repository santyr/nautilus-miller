# Preview providers, privacy and limits

These are development-build capabilities. The installed replacement RPM has not been updated by the completion work. Acceptance results and remaining gates are recorded separately in the [acceptance ledger](acceptance.md).

## Existing providers

| Content | Provider and behavior |
|---|---|
| File/folder/multiple selection | Existing Nautilus file objects and asynchronous GIO metadata; no recursive folder sizes. Unknown totals are labeled partial. |
| UTF-8 text | GIO input streams in a worker; literal read-only GTK text. Binary/control data and invalid UTF-8 receive an unavailable message. |
| Images | Existing libglycin API2 and its native bubblewrap sandbox, followed by GDK/GdkPixbuf pixel conversion/scaling in a worker. Exactly one still frame is requested. |
| PDF | Existing Nautilus thumbnail queue and installed GNOME Desktop thumbnailer, tested here with Papers. |
| Video/audio | The same queue and installed GStreamer thumbnailers. Video posters are supported when the codec/container is available. Audio without artwork retains metadata and Open/Space. No player or autoplay is embedded. |
| External inspection | Existing Nautilus/Sushi D-Bus contract; Space remains the external preview action. |

Reference host providers: Fedora 44, glycin libs/GTK/loaders 2.1.5-1.fc44, GNOME Desktop 4 44.5-1.fc44, papers-thumbnailer 49.8-1.fc44 and gst-thumbnailers 1.0.0-1.fc44. Other systems may have different installed format support. A provider failure is presented as unavailable; Open and Properties remain native actions.

## External Quick Look

Space uses the existing Nautilus/Sushi service. Sushi owns its external window, arrow navigation, repeated Space and Escape. Nautilus routes navigation to the owning active tab and closes its preview when that tab changes or closes. If closing Sushi leaves a newly selected tab without focus, the existing slot focus restoration handles it; an already focused location entry is preserved.

While the service starts or restarts, the wrapper retains the latest requested selection for up to five seconds. A later selection replaces the pending target. Tab closure or switching cancels it; entering the location entry before it is delivered prevents a late preview from stealing focus. Failure displays **Preview unavailable. Check that Sushi is installed and running.** A fresh Space retries through native service discovery.

The canonical service is shared with other clients. Sushi 45 and newer expose `ParentHandle`; Nautilus compares that property with its existing exported window handle before processing automatic selection updates, preview navigation or Close. Another client's preview is left alone. Older providers without this property retain their legacy behavior and cannot provide that ownership isolation. The reference host uses Sushi 50.0-1.fc44. No Sushi code is embedded and no additional production service is introduced.

## Bounded work

- Text: at most 128 KiB input and 32 KiB displayed, with explicit truncation.
- Images, PDF and media: at most 16 MiB encoded input. Oversized files receive metadata/fallback. Image source headers above 64 million pixels are rejected before requesting a frame; actual output is at most 1024×1024 while preserving aspect ratio. A provider may ignore its advisory scale request, so output is bounded again after decoding.
- Native image MIME types also honor the existing thumbnail size preference, using freshly queried size and pre/post-read descriptor checks. A custom row icon does not disable explicit content inspection.
- Each inspector retains one outstanding chain and coalesces replacement selections. The five-second deadline stops UI loading and invalidates publication; it does not claim every backend/provider has exited within five seconds. Ongoing native thumbnail work retains its bounded input until the existing queue completes. Late results cannot update a newer selection.
- Selecting another item, hiding the pane, deactivating a tab or disposal invalidates/cancels its work. Requests use weak widget references and do not own tabs/windows.

These are input/output/request limits, not a total process-memory ceiling or completed performance benchmark. The separate large-directory/resource acceptance remains required.

## Privacy and execution

Remote content is off by default, including cached remote content. Opt-in also requires native thumbnails set to **All Files** and approval from Nautilus’s existing filesystem preview policy. GIO handles remote transport and cancellation; the inspector does not construct remote shell commands. The archive fixture establishes bounded transport behavior, not SFTP/SMB authentication, disconnect or monitoring acceptance.

Native reads reject final symlinks and special files using nonblocking/nofollow opening and descriptor identity checks, off the GTK thread. Inspection never activates a file, runs a desktop launcher, interprets markup in filenames or invokes commands built from filenames.

Images are passed as bounded bytes to the installed glycin bubblewrap sandbox. Actual executions were observed with namespace isolation, seccomp and cleared environment, without a source-directory bind. A real SVG fixture observed no open of its external local image and no request to its owned loopback server. This is specific evidence, not a claim of exhaustive decoder security analysis.

GNOME Desktop providers need a pathname. The inspector stages bounded private 0400 runtime snapshots rather than exposing source directories or allowing providers to fetch remote originals. The existing thumbnail queue has a noncaching request option; ordinary listing callers retain their existing success/failure cache behavior, including coalesced cached requests. Normal completion deletes the snapshot, verified by filesystem events. Abnormal process termination can leave a private snapshot until runtime/session cleanup. Provider sandbox/cache internals remain native GNOME behavior.

## Test environment observation

The mounted-home fixture uses its proved disposable Home/.cache, leaving HOME unchanged. Putting XDG_CACHE_HOME below the provider sandbox’s shared /tmp output mount caused extra cache-parent directories and a native cleanup warning. Home identity, mount permissions, cache ownership and symlink rejection are tested before allowing this alternative cache path; the operator’s cache is not used.

A separate GTK 4.22.5/libadwaita 1.9.4 observation remains open: switching to an empty tab and closing the formerly focused tab before deferred focus processing can retain a page bin. A minimal program without Nautilus reproduces it; separating the actions releases it. GDB observed two retained focus references and one deferred release, consistent with [GTK’s focus implementation](https://github.com/GNOME/gtk/blob/4.22.5/gtk/gtkwindow.c). The fork now clears focus through GTK’s existing API before closing an inactive slot that still owns it. This prevents the second deferred reference in the reproduced application transition; it does not modify or claim to fix GTK. Both a decoding close with a pre-existing background tab and an immediate new-tab/close sequence assert weak destruction. A separate genuine Ctrl+L check retains entry focus while closing another tab. The pure GTK observation remains visible for later resource testing.

See [test procedures](input-testing.md) and [acceptance evidence](acceptance.md). No production installation is implied by these results.
