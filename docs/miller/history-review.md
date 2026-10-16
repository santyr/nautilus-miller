# History and navigation review

Fresh independent read-only review covered immutable `9321e1c6f..6d652eebe`, the history plan, full contract, interaction policy and ledger rulings. It found three Important issues, no Critical issues and no separate Minor findings. The reviewer independently reproduced all three using a temporary executable and guarded mounted-home sessions; it inspected the supplied full-suite evidence without independently rerunning that suite. Probe source and logs: `/tmp/miller-history-review-32sjuemi/{review.c,background.log,early-flat.log,new-focus.log}`.

Status: the single correction pass is complete with fresh passing full and external regressions. No second reviewer was dispatched. The history/navigation component is ready to continue into Stage3; the larger Finder completion goal remains open.

## Important corrections

1. **Pending background state was overwritten by provisional widgets.** `capture_state()` now returns the authoritative pending immutable snapshot until newer interaction cancels it. The maintained regression saves a restored unmapped tab, closes/reopens it while still unmapped, checks its parent context and horizontal state, then activates it and checks selection/action ownership. Original focused RED: `/tmp/nautilus-finder-history-review-background-red.log`.
2. **Selection during flat loading lost to an old Columns snapshot.** Pending explicit selections apply before FilesView clears `loading`; the slot now uses automatic-selection provenance instead of dropping every loading-time notification. Tests cover queued explicit Grid/List selection and actual model selection during a real loading batch. RED: `/tmp/nautilus-finder-history-review-flat-red.log` and `/tmp/nautilus-finder-history-review-loading-model-selection-red.log`. Automatic model clearing has a separate unchanged-behavior guard; no new model-clearing implementation remains.
3. **Explicit focus into the provisional endpoint failed to cancel old context.** Cancellation now precedes the same-active-view shortcut. Automatic loading focus excludes pending restoration; container/slot focus requests enter the saved column under restoration scope. Direct focus into a particular listing remains a newer choice. RED: `/tmp/nautilus-finder-history-review-focus-red.log`. The regression checks actual GTK focus and the window's file-action owner, without executing a destructive operation.

The initial focus correction exposed automatic traversal and search-cancel regressions, both corrected within this same pass. GDB traced the first through strip `focus/grab_focus` and the second through `hide_query_editor → Window.grab_focus → Slot.grab_focus`; logs `/tmp/nautilus-finder-history-review-{clear,search}-focus-backtrace.log`. Failed native runs are preserved as `/tmp/nautilus-finder-history-review-{automatic-focus,search-cancel}-red.log`. This was a focus-path fault; an ineffective experimental model-clear marker was removed. The subsequent ordinary native20-case run passed in `/tmp/nautilus-finder-history-review-history-green4.log`; strip10 passed in `/tmp/nautilus-finder-history-review-state-green.log` before the final slot-only adjustment. Those focused results do not substitute for the fresh final suite.

## Final verification

Fresh full build and profile isolation passed, `/tmp/nautilus-finder-history-review-{full-build,profile}.log`. Full private-session Meson **36/36 passed**, `/tmp/nautilus-finder-history-review-suite.log`, with detailed `/tmp/nautilus-finder-history-review-suite-testlog.txt`. Its state executable has44 registered cases:30 ordinary snapshot/native cases pass;14 external cases explicitly skip without their fixtures. The enhanced unmapped-close/reopen, queued Grid/List selection, loading-model selection, automatic clear and window action-owner assertions all pass in that final run.

Separate fresh actual mounted-home navigation **10/10 passed**, `/tmp/nautilus-finder-history-review-navigation.log`, and actual delayed archive **4/4 passed**, `/tmp/nautilus-finder-history-review-delayed.log`. These external results supply the acceptance that the normal skips cannot. The previously verified Python home5/session4/backend9/fixture4/IMEguard3/deployment37 code is unchanged; logs remain `/tmp/nautilus-finder-navigation-{home-final,session,backend,fixtures,ime-guards,deployment}.log`. Historical portal and intermittent Anthy observations remain visible; these results do not rewrite their history.

## Rulings I made

These preserve every plan ruling and every behavior the reviewer set aside. All three reproduced findings were retained at Important and entered the correction pass.

1. Continue native staged work under the supplied full contract and existing authorization, without repeated preference gates. Cost if wrong: reversible interaction decisions need revision; this does not authorize production deployment or real-data changes.
2. Supplement existing bookmarks with immutable in-memory values; do not introduce another history/session framework. Cost if wrong: more complex future restoration may need factoring; there is no new persistent history format to migrate.
3. Promote the existing ListBase scroll-window getter into the internal public header. Cost if wrong: internal API visibility needs reconsideration; extension ABI is unchanged.
4. Invalidate snapshots when a native bookmark follows a renamed endpoint rather than rewriting every stored URI. Cost if wrong: prior selections/positions are lost across rename; broader ancestor mutation remains required Stage5 work.
5. Extend the delay task to asynchronous non-native monitor setup because the actual GTK stack stalled in inherited synchronous GVfs monitor creation. Cost if wrong: shared remote monitoring requires additional regression/lifetime validation. Native creation remains unchanged; live supported remote watches remain a mandatory gate.
6. Mask the actual passwd home, rebind only the checkout read-only using an already opened FD, and start the client compositor inside that namespace. Cost if wrong: extra compositor startup overhead and a Linux/Bubblewrap test dependency; source-path scaffolding appears in the disposable home.
7. Actual filesystem consequences of the focus mismatch remain Stage4 acceptance. Correct the demonstrated action owner now; genuine operations, captured destinations, conflicts, undo and drag must still be proven. Cost if wrong: an untested operation boundary may still target incorrectly.
8. Complete SFTP/SMB live monitor events, setup-gap reconciliation, authentication, disconnect/reconnect and backend cancellation in Stage5. Archive evidence proves actual delayed enumeration, not those capabilities. Cost if wrong: backend-specific events or recovery could still fail.
9. Keep comparative resource/performance acceptance mandatory: deep paths, large selections, history growth, sustained worker cancellation and real10k/100k directories. Cost if wrong: clear individual ownership may conceal an event-loop or steady-state resource problem.
10. Complete broader ancestor deletion/rename, permissions, broken-link and remote error recovery in Stage5. The endpoint-rename ruling alone stands. Cost if wrong: another mutation can invalidate a displayed branch incorrectly.
11. Do not add generation checks solely for hypothetical reentrant adjustment observers; the review found no concrete production failure beyond the corrected callbacks. Cost if wrong: a future observer that navigates or destroys state during adjustment application may require another cancellation fence.
12. Keep copied navigation bookmarks unnamed as native transient history objects; no custom saved-bookmark presentation regression was demonstrated. Cost if wrong: an uncommon navigation-metadata expectation may need an additional copied field.
13. Preserve one native history entry per committed endpoint and in-memory snapshots; no new coalescing or cross-process tab-session system. Cost if wrong: long browsing sessions may need later history policy work; no new persistent-session promise is made.
14. Keep broader integration gates open: modal/popover paths, live Starred indexing, sidebar volumes, command-line/FileManager1, extensions and release portal behavior. Cost if wrong: the focused initial-focus and special-view tests can miss a different entry-point failure.
15. Preview, external-provider focus/restart, screen readers, scaling/RTL visuals, real benchmarks, observed CI and actual disposable RPM transactions remain required later stages. Cost if wrong: a passing history component could conceal unfinished everyday functionality; it is not a release claim.
16. Keep the intermittent Anthy warning open. Its installed-provider explanation is a candidate diagnosis; unchanged-code green reruns do not resolve it. Cost if wrong: composition can still fail under the uncharacterized timing.
17. Attribute independent execution accurately: the reviewer ran only its three additional regressions; the executor ran the recorded suites. Cost if wrong: treating supplied green logs as an independent rerun would overstate validation.
18. Preview spec/plan drafts and subsequent test/docs edits are outside the immutable review range. The later QR addition is also outside that range and was checked as an exact copy of the user-supplied asset. Cost if wrong: later changes need their own acceptance/review appropriate to their scope; this review cannot certify them.

## Deferred minors

This review added none. The earlier [layout Fit allowance](layout-review.md) and [input length-warning feedback](input-review.md) remain recorded Stage5 work, not waived issues.

## Continuing scope

This component does not close C01–C16. Continue the mandatory preview, operations/real clipboard/DnD, remote/error, accessibility/performance, CI and release work. The installed file manager and queued offline update transaction have not been changed by this work.
