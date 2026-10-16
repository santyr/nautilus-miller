# Layout review and remaining acceptance

Fresh independent read-only review of `958cd400c..8fbe46c97c46f0754b88dd86d906e4e5aa3e1649` found no Critical findings, one Important finding and one Minor finding. This is the layout component of Stage2, not completion of Stage2 or C09/C13.

## Important correction

Manual scrolling originally canceled only an existing frame callback. Selection work already queued in either FilesView's notification idle or the strip's disclosure idle could schedule a new reveal afterward. The focused regression reproduced viewport0 changing to1945 after scrolling. The correction records a scroll generation at the original selection-model change. Manual input advances that generation; older selection work still discloses the child but cannot schedule a reveal. The model signal is disconnected when its column is removed.

`test_minimal_reveal` covers both idle boundaries. Red log `/tmp/nautilus-finder-layout-review-red.log`, green `/tmp/nautilus-finder-layout-review-green.log`. The second case starts with its source row visible: invoking FilesView's explicit reveal API on an offscreen row additionally asks GTK to animate focus scrolling, which an emitted strip-controller signal does not cancel. Real gesture/GTK animation interaction remains required genuine-input acceptance.

Post-correction full private Mutter/Wayland + private D-Bus/XDG regression: **30/30 Meson executables passed**, including13 Miller cases and the portal executable; log `/tmp/nautilus-finder-layout-review-suite.log`. Earlier deployment37, fixture4 and session2 tests passed; profile isolation and diff checks passed. No new install or host file operation occurred. Snapshot `/tmp/nautilus-finder-layout.png` was visually inspected for readable native headings/options and column boundaries.

## Deferred minors

- Fit uses a fixed96px icon/affordance allowance, which can underfit a sampled directory name at64px icon zoom. Carry this into mandatory Stage5 zoom/large-text geometry acceptance; use a moderate name below the1200px clamp and assert actual label fit. This is not waived by documented unsampled-name or maximum-width limits.

## Rulings I made

1. Continue native implementation without another preference/design gate: the supplied full contract explicitly directs continuous staged work. Cost if wrong: reversible layout choices need revision; deployment remains separately authorized.
2. Application restart persistence remains required: new settings instances and reconstructed columns establish only in-process rehydration. Cost if wrong: a restart could lose widths or defaults.
3. Real pointer resizing/scrollbar gestures remain required: emitted controller signals bypass compositor delivery and gesture competition. Cost if wrong: real gestures may differ from these tests.
4. Keyboard-only traversal and screen-reader interaction remain required: accessible names alone do not prove reachability or value announcements. Cost if wrong: controls may be inaccessible despite passing properties.
5. Unicode/IME typeahead remains mandatory Stage2 work. Cost if wrong: the arrow guard could be mistaken for a complete input implementation.
6. History/tab selection, active-column and scroll restoration remain mandatory Stage2 work. Cost if wrong: layout correctness alone could conceal lost browsing state.
7. Complete RTL input acceptance remains required: GTK packing and semantic arrows are tested, pointer resizing uses synthetic events. Cost if wrong: physical drag direction/coordinate handling may differ.
8. Large text, fractional scaling, themes, high contrast and reduced motion remain mandatory Stage5 checks. Cost if wrong: geometry/readability failures, including Fit, may remain.
9. Retained fit/default widgets and every controller combination are not individually exercised; teardown structurally disconnects them like the tested controls. Cost if wrong: a retention-specific callback could escape coverage.
10. Branch replacement before a pending reveal is tested for disposal safety, not final replacement geometry. Cost if wrong: replacement geometry could be wrong without crashing.
11. Genuinely delayed backend completion remains required: synthetic loading notifications and fast fixtures do not establish it. Cost if wrong: slow backends may expose stale ordering.
12. Actual remote authentication, monitoring, disconnects and URI semantics remain mandatory Stage5 checks; userinfo-bearing width identities deliberately receive defaults. Cost if wrong: URI policy tests could conceal backend defects.
13. Malformed externally oversized settings and concurrent writers lack dedicated resource/race evidence; no normal-use failure was identified. Cost if wrong: hostile local configuration or concurrent external writers could cause stalls/lost recency.
14. Unsampled long names and names beyond1200px may remain ellipsized: this is the documented bounded-fit policy. Cost if wrong: users expecting exhaustive autosize will need a different policy.
15. Preview, genuine clipboard/DnD, spring-loading, full operations, mutations, performance, extensions, portal acceptance and deployment/rollback remain mandatory broader work. Cost if wrong: a green layout suite could conceal core gaps.
16. Production installation and real user-data operations remain outside this goal's authorization. Cost if wrong: development capabilities remain unavailable in the installed manager until a separately authorized release install.
17. C01–C16 and overall completion remain open. Cost if wrong: component evidence could be overclaimed as a completed everyday replacement.

No second reviewer was requested. The Important finding received one red/green correction pass and a full green suite; the Minor remains explicitly tracked for the later required visual stage.

## Genuine-input harness discovery

A throwaway probe created a RemoteDesktop session on the disposable Mutter's private D-Bus, established virtual keyboard/pointer devices before mapping a GTK window, and delivered actual keyboard input into its entry. The entry contained the expected `x`; log `/tmp/nautilus-finder-input-probe.log`. Earlier attempts mapped the window before Mutter's lazy virtual keyboard existed and never obtained server keyboard focus. Installed Mutter50.6 source confirms lazy device creation in [the RemoteDesktop implementation](https://raw.githubusercontent.com/GNOME/mutter/50.6/src/backends/meta-remote-desktop-session.c).

This is feasibility evidence, not a production input helper or clipboard/drag/IME acceptance. The next input plan must build a guarded test helper that verifies private runtime/socket/bus/compositor ownership, retains the creating D-Bus connection, creates devices before test windows, and cannot address the real desktop. Installed IBus engines include Anthy, Hangul, Chewing and libpinyin, allowing a disposable composition fixture without host package installation. Private-session AT-SPI still fails to activate and needs separate investigation.
