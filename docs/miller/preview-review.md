# Whole-preview review and correction record

Status: independent review and one correction pass complete. The final full source-frozen gate passed. This does not close the full completion goal.

Reviewed immutable range: `5a8060352..8d2554bd969cd639d5e77550d4e2fe3cdd44fd8c`. One fresh read-only reviewer inspected all production changes, relevant native integration and tests, plan/spec, six executor rulings and supplied logs. No second review is planned. The reviewer did not run new suites or claim runtime reproductions. Source-traced findings require regression-first reproduction before fixes.

## Findings and grades

No Critical finding. Three Important findings stand after checking the surrounding native source:

1. **Monitor refresh:** `nautilus-miller-view.c:438` refreshes only on changed selection pointers. FilesView intentionally emits selection notifications when selected files change; retaining the same objects must not suppress inspector refresh. Preserve the separate no-navigation/no-action-reclaim rule. Required regression: actual owned-file content change and rename, with unchanged focus/history/action owner.
2. **Mounted remote privacy:** `nautilus-miller-preview.c:542` uses native-path representation as locality. Native paths can reside on remote filesystems. Reuse native filesystem-info readiness and `nautilus_file_is_remote`; its uncached fallback is local, so substituting that predicate alone is inadequate. Required regressions: actual filesystem-info readiness, cached native-path remote policy with no content open, and preserved nonnative remote/deadline/lifetime cases. The cached policy fixture is not actual NFS/CIFS acceptance.
3. **Empty pending selection:** FilesView returns on empty selection without invalidating the wrapper's retained request; service recovery can open the old URI. Cancel the owning intent on deselection/removal and check target validity before dispatch. Required regression: real Sushi recovery after genuine deselection and owned-file removal, followed by a fresh eligible request.

All fixes must retain existing Nautilus/GTK/GIO/glycin/thumbnail/Sushi infrastructure. No replacement decoder, transfer engine, filesystem classifier or external process manager is authorized by this correction pass.

## Deferred minors

- Thumbnail-stage cancellation lacks direct acceptance of publication invalidation, weak owner destruction and eventual snapshot deletion. Successful PDF snapshot deletion and glycin-frame cancellation are covered; static thumbnail cleanup is present. Keep this additional case in the required later lifetime acceptance, without claiming it has run.
- A historical acceptance paragraph says Python76/75; the recorded Task3 final result is76/76. The generating scratch verifier also contains the typo. This is documentation polish; preserve the historical evidence and use exact current test output for claims.

## Executor rulings reviewed

1. Continue authorized native staged execution with one whole-plan review. This preserves the agreed scope and avoids repeated preference gates. Cost if wrong: reversible UI decisions need adjustment; no production installation or real-data authorization follows.
2. Extend the existing archive fixture with bounded1–10second pauses, using8 to observe the inspector's own5second deadline. Preserve failing watchdog semantics. Cost if wrong: slower explicit acceptance invocations; no timeout failure becomes a pass.
3. Mask Home for full-suite executables; dispatch the input metadata guard outside that mount after its private-path proof, and LocalSearch inside the mount with disposable Home cwd/private `/tmp`. Preserve all Meson executables. Cost if wrong: an omitted/incorrectly dispatched test could overstate coverage; check introspected counts and actual results. Unix-socket truncation was observed; not every original fixture failure root cause is proven.
4. Use bounded private0400 runtime snapshots with the existing uncached thumbnail queue after sealed anonymous FD access failed in the installed sandbox. Cost if wrong: extra bounded I/O and possible leftovers after abnormal app termination until runtime cleanup. Normal completion deletes inputs; cancellation-specific coverage remains above.
5. Clear GTK focus before closing an inactive slot only when that slot still owns focus. Reuse existing GTK APIs; do not modify the library or claim its baseline retention fixed. Cost if wrong: background-tab focus regressions; both close timings, native navigation/Grid/List and preserved entry focus must remain tested.
6. Preserve older provider behavior when ParentHandle is absent; enforce actual exported parent identity when supplied (Sushi45+). Cost if wrong: providers before45 cannot isolate another client's preview ownership. The tested reference provider is50.0-1.fc44; keep this boundary explicit.

## Rulings on every declined-to-judge item

1. **Operations and DnD:** remain mandatory Stage4 work under the full goal; preview tests do not pass them. Cost if wrong: incomplete or incorrectly targeted everyday file operations.
2. **Real remote services and networking:** actual SFTP/SMB authentication/cancel, delays/disconnect/reconnect/monitoring and private network namespace remain mandatory Stage5. Archive tests do not establish them. Cost if wrong: remote privacy, stale-state or authentication failures go unobserved.
3. **Accessibility and resources:** actual screen reader/tree, visual scaling/RTL, real10k/100k/deep-directory measurements and resource/lifetime comparisons remain mandatory. Cost if wrong: inaccessible controls, stalls or leaks despite functional fixture passes.
4. **CI and RPM transactions:** observed CI and real disposable install/update/restore/failure transactions remain mandatory Stage6. Neither workflow files nor mocked deployment output pass them. Cost if wrong: release and recovery failures are shipped.
5. **GTK and historical observations:** application focus mitigation does not fix GTK itself; original portal/Anthy/pointer/strace root causes remain recorded where unresolved. Cost if wrong: those failures recur or are incorrectly attributed to this component.
6. **X11 and older Sushi:** current evidence is Fedora44 GNOME/Wayland and Sushi50. Preserve native X11/legacy code but do not claim runtime acceptance for them. Primary-host completion does not establish cross-client ownership on pre45 providers. Cost if wrong: untested alternative-session/provider regressions; test them before broadening release claims.

## Evidence boundary

Before correction: both full Meson38/38, actual inspector47/47, genuine navigation10/10, delayed history4/4 and maintained Python89/89 (including22actual Sushi C scenarios across profiles, native persistence and six I/O observations). The first aggregate's87/89 observer failure remains recorded; the later GDBusProxy fixture correction preserved strict unique-owner acknowledgment checks. Correction tests and the final full aggregate must be recorded below before component completion. Production installation and public branches remain unchanged.

## Correction evidence and final gate

All three Important findings were reproduced before correction. Monitor content/rename passed1/1 while preserving focus/history/action owner (`preview-review-monitor-{red,first}.log`). Real Sushi pending deselection/removal passed2/2 after cancel-on-empty and live target revalidation (`preview-review-pending-{deselection,removal}-red.log`, `preview-review-pending-first.log`). Native filesystem readiness and native-path remote text/image policy passed3/3 after their separate RED logs; inotify observed no content open for the cached remote-policy cases (`preview-review-filesystem-first.log`). Real pending filesystem wait hide/dispose/replacement passed3/3, including weak widget destruction, after the missing observable availability phase RED (`preview-review-filesystem-transition-red.log`, `preview-review-filesystem-second.log`). Logs share the `/tmp/nautilus-finder-` prefix.

The final aggregate passed both complete Meson38/38 profiles; each native UI executable passed15ordinary cases with14explicit process/service skips, covered separately. Actual-backend inspector53/53, genuine navigation10/10, delayed history4/4 and maintained Python89/89 passed. Python includes26actual Sushi C scenarios across profiles, native dconf process restart and six actual I/O observations. The final gate checked immutable application/test diffs, exact executable/case counts, no skipped external acceptance and complete Python output before committing. Full output `/tmp/nautilus-finder-preview-task4-final.log`; per-profile detailed logs `/tmp/nautilus-finder-preview-task4-final-{build-miller,build-preview-canonical}-testlog.txt`. No second reviewer was used.

A concurrent pre-correction rerun failed87/89 because the Python module had already imported newly added pending paths before those C cases were rebuilt. Its assertion was missing `ok4/pending-deselection`, not a product reproduction; it is retained as `preview-task4-gates-second.log`. The final gate froze all test dispatch/source/binaries. Earlier first-aggregate observer alias failures also remain recorded. Neither failed aggregate was relabeled green.

This completes the preview component plan. The two deferred minors, six executor rulings, six declined-item rulings and all later full-goal acceptance remain above. No production package or public branch was changed.
