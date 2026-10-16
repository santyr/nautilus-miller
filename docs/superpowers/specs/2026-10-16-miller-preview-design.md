# Miller inspector and Quick Look design

Status: design preparation for Stage3 under the supplied full completion contract. History-plan review and its correction pass are complete; see `docs/miller/history-review.md`. This document does not claim preview implementation or acceptance.

## Intent and scope

Provide quick inspection while retaining directory context and native Linux actions. The user requested Finder-like behavior and additional preview functionality. The governing [completion contract](../../miller/finder-completion-goal.md) requires both an inline inspector and external Quick Look. The inspector is optional to show, initially hidden; implementing it is required. It follows the active actionable selection, including an ancestor column, independently of the branch endpoint. Selecting a file never launches it.

## Placement and ownership

Use a stable right-side pane alongside the existing directory scroller. A horizontal `GtkPaned` inside the Miller widget owns the scroller and a separate `NautilusMillerPreview` widget. Column widths and horizontal adjustments remain scoped to the scroller. A native view-menu toggle controls visibility; the pane also has a named close control and keyboard-operable width control. Profile-specific GSettings persist visibility and width, separately from directory widths: `miller-preview-visible=false`, `miller-preview-width=320` (240–800 logical pixels), and `miller-preview-remote=false`. Ctrl+Shift+P toggles the inspector in Columns; document it alongside the menu action. Grid/List and special views retain their existing layout.

Alternatives considered: hosting the inspector in the slot would make it available in every view but expand this stage's integration surface; representing preview as another column would mix inspection with path/history. Keep the pane in Miller and keep decoding in its own unit.

The strip sends an owned selection snapshot when its active view or actionable selection changes. The inspector owns one current request generation, cancellable and deadline. Keep at most one content request in flight per inspector and coalesce replacement targets; cancellation must not create an unbounded queue of detached decodes. Verify actual provider work is released, not merely hidden from callbacks. Callbacks own request data and a weak reference to the widget; they must finish and release their results even after cancellation or destruction. New selection, hiding, inactive tab and disposal invalidate prior work. Completion cannot mutate selection, action context, location, history or keyboard focus.

## Content and privacy

Empty selection shows an instruction. Folders show available nonrecursive metadata. Multiple selection shows count and only known aggregate metadata, explicitly identifying incomplete totals. Unsupported types, links, devices and FIFOs show type/icon/metadata plus native explicit Open/Properties actions where available. No recursive directory counts or archive extraction are started by inspection.

Text/source uses bounded asynchronous reads, literal text, no markup interpretation, and a read-only view. Initial limits: 128KiB read, 32KiB displayed, with explicit truncation. Local file opening must reject special files and symlink replacement without a blocking FIFO open; verify the opened descriptor's regular-file identity. Remote content is disabled by default, also constrained by inherited thumbnail/privacy settings. Enabling it still applies byte/deadline limits.

Common images use the existing libglycin/libglycin-gtk4 API2 dependency (pkg-config version2.1.0; Fedora development RPM2.1.5), bounded encoded bytes and a scaled still frame. Initial limits: 16MiB input, 64million source pixels and 1024×1024 output; a five-second deadline cancels the request. Decode through the maintained sandbox, never the unsafe disabled mode. Passing bounded bytes instead of a source path avoids exposing the source directory to SVG external-resource loading. Verify actual installed decoder isolation, cancellation, memory policy and no external resource access before acceptance. The [upstream sandbox description](https://github.com/GNOME/glycin#sandboxing-and-inner-workings) describes loader memory limits and network isolation; these are dependency mechanisms, not measurements of this fork.

PDFs use the existing thumbnail infrastructure for a first-page representation and explicit external Open. Audio/video use available poster/metadata and external preview, without autoplay. Follow existing thumbnail availability and preference rules. A missing codec/thumbnail yields a truthful fallback rather than claiming rendering succeeded. Do not add a document engine or a media player.

## External preview

Retain Space and the canonical Sushi contract. Complete genuine normal-provider behavior in both Miller and production profiles, repeated Space/Escape, selection navigation, tab ownership, focus return, missing provider and restart. Existing discovery tests remain useful but are not a substitute for a functioning provider. Missing-provider feedback must be visible and actionable without launching or navigating the selection.

## Acceptance and sequencing

1. Separate preview widget: metadata/empty/multiple states, request lifetime and bounded text before strip integration.
2. Integrate active-selection updates, visibility/independent resizing and persistence. Verify parent actions, entries, tab switches and close with actual private input and restart.
3. Add image and thumbnail/media representations with limits, unsupported/error/loading states, preference changes and cancellation.
4. Complete actual external Sushi workflows in both identities and fresh full regressions; obtain one independent component review.

Required tests include A completing after B, hidden/closed widgets, tab ownership, huge text/images, invalid UTF8/binary data, FIFO/device/link races, unsupported/corrupt files, malicious names, remote-off/no-read behavior, bounded enabled remote reads, unavailable codecs/providers and provider restart. Assert file contents and state where relevant; screenshots alone are insufficient. Retain the full Stage3/C07/C08 and later remote/a11y/performance/release gates until their evidence exists.
