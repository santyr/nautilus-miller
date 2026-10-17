# Native drag-and-drop verification

Authority: [full goal](finder-completion-goal.md), [drag design](../superpowers/specs/2026-10-17-miller-dnd-design.md) and [plan](../superpowers/plans/2026-10-17-miller-dnd.md). Source base for Task1 `b66a52f7b30cbc9508a449a3d209a9ea50031a5d`; full DnD base `deb79d68a`. This component is in progress. Full Stage4, C06 and release acceptance remain open.

## Reused fixture and baseline

The existing operation fixture/helpers now live in test-only `test/automated/miller-test-operations.[ch]`, linked only to their consuming display executables. Native model selection, guarded real input, private Home checks, progress/undo completion, exact bytes/sentinels and retained child cleanup remain the existing implementations. Both main and normal-app child bind their already verified borrowed input. The original operation application/clipboard roles remain separate.

Fresh actual operation baseline63/63 passed before extraction (`/tmp/nautilus-finder-dnd-task1-operations-baseline.log`) and again after extraction (`...-task1-shared-fixture-operations.log`), with real deletion/Trash and batch child markers, no skips. The helper extraction changes no product API. A new native DnD executable increases the Meson executable total from39 to40.

## Actual drag input and fixture corrections

The new test uses compositor-delivered primary press, threshold motion, modifiers, release/Escape, actual GtkDragSource/GdkDrag callbacks and the real native file-list provider. It checks exact initiating file identity/count, actual negotiated action, finished/cancelled native jobs, literal file bytes, unchanged siblings, destination model rows and post-drag normal navigation. Model selection and window/directory setup are controlled native APIs; the drag is genuine, with no emitted drag signals or direct transfer calls.

First Grid/List attempts moved once immediately after press and never began a drag. The noninterfering input observer proved the correct source cell, primary button, modifier state and movement18px beyond the native8px threshold. [GTK4.22.5 source](https://raw.githubusercontent.com/GNOME/gtk/4.22.5/gtk/gtkdragsource.c) additionally requires movement after its100ms minimum interval. Holding140ms before motion makes all3 native Grid/List/Columns controls pass; no product/GTK change. Logs `...-task1-controls-first.log`, `...-task1-input-first.log`, `...-task1-list-first.log`, then `...-task1-controls-held.log`3/3. Temporary raw event instrumentation was removed. First compile's missing existing file-utilities declaration was fixed by including its header; not a product defect.

## Demonstrated Columns destination disappearance

The genuine empty-column drop case retained its destination through primary hold, then native drag preparation selected the original source file. The queued Miller selection handler collapsed the displayed destination child before drop. Both provider count1 and exact original URI passed; target mapping then failed. RED `/tmp/nautilus-finder-dnd-task1-empty-phase.log` and combined `...-task1-nine-first.log`.

A small ListBase signal now forwards the existing native GtkDragSource begin with its real GdkDrag. Miller owns that one drag reference until native finished/cancel/disposal and suppresses selection-driven navigation while it owns the drag. Column cleanup disconnects the ListBase signal. No transfer, hover, selection or directory engine is replaced. The original case plus Grid/List/Columns copy and self-drop cancel passed5/5 (`...-task1-source-selection-green.log`). The later combined11/11 run also proves ordinary column navigation resumes after native finish/cancel (`...-task1-eleven-first.log`). Full aggregate and component review remain pending.

## Native Wayland LINK boundary

The initial Ctrl+Shift LINK expectation failed identically in Grid, List and Columns: the native session negotiated MOVE2. Logs `...-task1-link-modifier-first.log`, `...-task1-grid-link-modifier-baseline.log`, `...-task1-list-link-modifier-baseline.log`. The [Wayland data-device action enum](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_data_device_manager-enum-dnd_action) has COPY/MOVE/ASK and no LINK. Do not convert a protocol MOVE to a different file operation. This is not a Miller-only defect, and no protocol workaround was added.

Maintained link-unavailable-wayland cases in all3 modes observe a real non-LINK native negotiation, then use genuine Escape before release, proving no job/destination and intact source bytes. The fixture cancellation does **not** mean the application automatically rejects Ctrl+Shift; on the tested host its cursor/action is MOVE. Use existing native Create Link commands (separately tested in operations) to create links. X11 or future extension support is not claimed.

## Current Task1 cases and limits

Actual combined11/11 passed, no skips: grid-folder-copy, list-folder-copy, column-folder-copy, between-visible-columns-copy, empty-column-background, inactive-pointer-destination, move-modifier, link-unavailable-wayland, grid-link-unavailable-wayland, list-link-unavailable-wayland, reject-self-drop. Seven accepted transfers use exactly one finished native job with exact bytes; four native cancels have no jobs or filesystem mutation. Copy retains source; Move removes only original source. Native selected-action feedback is checked, and explicit post-finish/cancel model navigation verifies the guard releases.

These are Task1 focused results. Spring-loading/cancellation snapshots, scrolling, external apps, tabs/windows/bookmarks/volumes, source/target mutation, pending clipboard destruction, remote disconnect, whole-component review, and all later full-goal gates remain mandatory. Current session/Home fixtures are not claimed host-network isolated; integrate and test that protection before actual remotes.

## Task1 complete development gate

The source-frozen gate completed with exit0 for all9 commands, and the strict verifier independently rechecked the current source fingerprint and actual case/lifecycle markers. Fingerprint `fcb5682c1cdac6f50b9c27e192ed8c58f29636e2cda015a599344023695d3578`.

- Both development and canonical Meson40/40, including operations63/63 and genuine DnD11/11 per profile; no operation/DnD acceptance skips. Both native portal smoke tests passed.
- Separate actual inspector53/53, navigation10/10 and intentionally delayed history4/4, without external acceptance skips.
- Maintained Python90/90 in188.420s, including actual preview I/O, persistence and Sushi workflows across both profiles.

Driver `/tmp/nautilus-finder-dnd-task1-driver.log`, aggregate `/tmp/nautilus-finder-dnd-task1-final.log`, detailed `/tmp/nautilus-finder-dnd-task1-final-{build-miller,build-preview-canonical}-testlog.txt`. Runner and verifier are plan-owned under `.superpowers/sdd/2026-10-17-miller-dnd/`. This closes Task1 only. Task2/3 and whole-component review remain open; no host installation or push occurred.

## Task2 in progress: native spring disclosure

Base `6f8cc4e298064d8d724e4d17fe0ad1c99024c010`. First genuine hover regression failed after all11 Task1 controls passed: a real hover disclosed the folder but replaced the original dragged file selection (`/tmp/nautilus-finder-dnd-task2-spring-first.log`). The existing native hover callback now offers a handled spring-load signal before replacing selection; only Miller handles it. Native Grid/List fallback,500ms timer, preferences and transfer machinery remain unchanged. Miller reuses its immutable snapshot and retains the native source view during its drag. Temporary columns leave the slot/history unchanged; a native accepted-drop notification commits one ordinary endpoint navigation.

The first corrected run preserved selection/history but exposed a fixture error: the ordinary focus helper waited for an active toplevel during the compositor drag grab. Actual Grid and Columns controls both show window-active0 with cached GtkListItemWidget focus. Drag-time tests now assert cached source focus and native action ownership; post-drag tests still require normal active-window focus. Logs `...-task2-spring-green-first.log` and `...-task2-spring-focus.log`.

Exact cancellation snapshot comparison then found a real restoration defect: captured horizontal1235 returned as1240. Instrumented capture remained1235; a bounded captured stack resolves to cancel_restore←set_active_view←focus_enter before restoration completed (`...-task2-restore-stack.log`; prior snapshot/prepared/observed logs retained). Native focus return after the grab was treated as a newer user navigation. A narrow pending-drag-restoration marker now preserves that expected same-source focus return. Existing explicit click/selection/navigation invalidation remains; temporary stack/debug instrumentation was removed.

Focused Grid control plus hover-preserves-source-selection, nested-spring-cancel and spring-drop-retains-destination pass4/4 (`...-task2-spring-green.log`): exact original URI, actual directory models, zero pre-drop jobs/history, cached focus/action ownership, two nested native hovers with exact cancellation snapshot restoration, or one finished native copy job/exact bytes and one accepted history entry. These are focused development results, not Task2/component completion. Remaining leave/newer-navigation/old-drag/delayed-origin and broader lifetime cases, full regressions and whole-component review remain open.

Further Task2 characterization: ancestor backtracking passed unchanged. Leaving the strip while the native drag remains live failed to restore the provisional branch (`...-task2-lifecycle-first.log`). A separate newer-navigation case committed the unrelated sibling in the slot, then old cancellation incorrectly restored the strip's obsolete endpoint (`...-task2-newer-first.log`). Native strip-motion leave now defers restoration until the native accepted-drop callback can win; existing navigation/restore entry points invalidate provisional snapshots. The leave idle is cancelled on reentry, native acceptance, finish and disposal.

The first leave-observer run exposed the fixture's old pointer reset through the surface corner before every movement (`...-task2-lifecycle-green-first.log`). That intentionally crosses the strip boundary and removes provisional descendants. The fixture now preserves its proved initial fullscreen coordinates and sends continuous relative motion, including the18px intra-row hover motion. No native signals are emitted or bypassed.

All six current spring cases pass in the canonical profile (`...-task2-canonical-spring-first.log`): source selection, nested cancel, accepted destination, ancestor backtracking, leave restoration, and newer navigation. A combined development17-case run stopped in the inherited List link-unavailable control before native drag begin after9 passes (`...-task2-seventeen-first.log`); it is a failed aggregate, not a pass or established product root cause. Separate unchanged attempts are being characterized. Operations63 regression is also running. Old-drag overlap, deliberately late origin completion, foreign drops, full Task2/component gates and review remain open.

### Current focused regression result

The full native operations executable passed63/63 with no skips and all normal-app deletion/batch markers (`/tmp/nautilus-finder-dnd-task2-operations-first.log`). The List failure recurred in the Grid→List sequence, while3 isolated List runs, a temporary native input-observer sequence and5 allocation-observer sequences passed unchanged. The input observer confirmed actual modifiers/button/motion/release; it was removed. Failed source allocation was y92 versus final y144 in passing starts, supporting a pending-row-layout hypothesis. No failed run captured both allocation values, so that exact failure attribution is still unproven. No sparse-motion workaround or production drag-start change was made.

The fixture now waits for3 real GTK layout ticks after mapping, then reacquires the exact file cell before sending input; all native input/payload/action/job/filesystem assertions remain. Both complete current17-case development and canonical runs pass with17 native begin/payload/end observations,8 accepted drops,9 cancellations and no skips. Logs `/tmp/nautilus-finder-dnd-task2-seventeen-{build-miller,build-preview-canonical}.log`; both processes exited0 and a strict focused count/marker check passed. Earlier failed aggregates remain visible. This is focused coverage, not the Task2 complete gate: old-drag overlap, deliberately late origin, full40-executable regressions and whole-component review remain open.

Current source fingerprint after the focused run: `465b9aa418a2b009a88fdb825d1ddb6a325b822b4c313e2a09c1bfd1902955b9`. Sources/binaries were unchanged while those two runs executed. This is not a replacement for the planned source-frozen full gate.

## Task2 actual delayed origin and overlapping native drags

The existing state/preview backend metadata/control helpers now share one test-only implementation in miller-test-backend.[ch], preserving their owned metadata/socket/path/mode guards. This reuses the guarded PIDfd backend controller; no new backend or production API. Pre-extraction actual history4/4 and inspector53/53 passed (`...-task2-backend-baseline-{state,preview}.log`), then both passed after extraction (`...-task2-backend-shared-{state,preview}.log`). The first new test compile called a private location helper; it was corrected to the existing public get_pending_location before runtime acceptance.

A real older archiveA navigation was held pending by the identified private paused GVfs backend while a real local file drag spring-loaded a writable folder. On resume an independent actual NautilusFile ready observer completed; the older slot request then overwrote the provisional branch. RED `...-task2-backend-shared-late-first.log`. The existing miller_location_changing callback now calls existing end_location_change for older pending navigation. This cancels its native ready/mount callbacks, with no alternate history/generation system or stop/reload of the drag source. GREEN `...-task2-late-green.log`: the same independent metadata observer completes after resume without changing the committed origin, source/branch/action ownership, history, zero jobs or exact cancellation snapshot.

The actual old-drag overlap case passed first without another product correction (`...-task2-overlap-first.log`). A second native pointer drag starts and spring-loads from another owned directory inside the first GtkDragSource drag-end callback; the first native end/cancel emission has not unwound. After it returns, the second branch/focus/action context and native identity survive, and cancelling the second restores its own exact snapshot. Both payloads, bytes, sentinels and zero-job outcomes are checked. No drag signals are emitted by the fixture; concurrent physical grabs are not claimed.

Both complete real-backend19-case focused runs pass, no skips: `/tmp/nautilus-finder-dnd-task2-nineteen-{build-miller,build-preview-canonical}.log`. Each has20 actual native payload/begin/end observations (the overlap case contains2 drags),8 accepted drops and12 cancellations, one actual old-origin ready callback and the overlap marker. Both processes exited0 and a strict case/marker check passed. The full source-frozen40/63/19-per-profile/53/10/4/Python90 gate is still required before Task2 completion. Ordinary Meson explicitly skips only the external-only late-origin case; both separate actual backend runs must execute it without a skip.

### Task2 final complete development gate

All11 gate commands exited0; exec39029 is terminal0. Fresh verify-task2.py rechecked the current source fingerprint, both complete Meson40/40 (including operations63/63 each), both actual delayed-backend DnD19/19 runs (20 real drags each), actual inspector53/53, navigation10/10, delayed history4/4 and Python90/90 in186.490s. Each ordinary Meson DnD run executes18 actual cases with one explicit external-only late-origin skip; both separate actual backend runs execute all19 without skips. Both real portal smoke tests passed. Source/binaries were frozen throughout; fingerprint `a79cc33dc5f1bc0425b89e1adb173cfd16cd4d44972b3f5cb601ada7c1e395d3`.

Driver `/tmp/nautilus-finder-dnd-task2-driver.log`, aggregate `/tmp/nautilus-finder-dnd-task2-final.log`, parts0..10, detailed `...-task2-final-{build-miller,build-preview-canonical}-testlog.txt`. Plan-owned runner/common/verifier are retained until whole-component completion. This closes Task2 development acceptance only. Task3 must still exercise explicit tab/unmap/removal/repeated lifetime, scrolling, foreign source/sink and native sidebar/volume/window paths; one whole-component review and later full-goal/release gates remain. No production installation or push occurred.

## Task3 in progress: native scrolling and status overlay

Base `9fd54474318f7a33f3d236edb925534e2ca59e36`. Task3 has started; it is not complete or reviewed. Tagged [GTK4.22.5 GtkListBase](https://raw.githubusercontent.com/GNOME/gtk/4.22.5/gtk/gtklistbase.c) already supplies native drag-edge scrolling for its list adjustments. The outer column strip is a different GtkScrolledWindow/viewport; [GtkViewport](https://raw.githubusercontent.com/GNOME/gtk/4.22.5/gtk/gtkviewport.c) positions its child at minus the horizontal adjustment in both text directions. No replacement vertical engine was added.

Actual Grid/List controls passed while the first Columns vertical attempt stayed0. Pointer-picked bounds showed the initial bottom8px point hit the outer horizontal overlay scrollbar (`...-task3-{grid-vertical-edge-scroll,list-vertical-edge-scroll,vertical-edge-scroll}-diagnostic.log`). Moving24px inside the native30px list edge exposed a second, genuine integration gap: the narrow column's selection FloatingBar covers that edge and its old hide handler sees ordinary pointer motion only (`...-task3-vertical-pick.log`). The strengthened test first needed to await asynchronous status presentation (`...-task3-floating-bar-red.log` fixture failure); it then correctly failed because the bar stayed visible during a real file drag (`...-task3-floating-bar-ready-red.log`).

A first reused timer-based DropControllerMotion hide exposed the list but gave GTK no new motion when the bar disappeared; scrolling remained0 (`...-task3-vertical-green-horizontal-red.log`). A diagnostic real1px motion then started the existing vertical engine (`...-task3-vertical-motion.log`); that diagnostic nudge was removed. The FloatingBar now suppresses its non-interactive overlay for the native drop visit, restoring it on leave/reparent, using its existing controller/cleanup structure. Ordinary pointer hiding keeps its old timer; interactive/loading Stop keeps its existing guard. The test requires visible status before drag, hidden status during the visit, the actual source list/adjustment under the edge pointer, native scrolling without another nudge, center stability, and restored status after cancel. Grid/List/Columns pass3/3 (`...-task3-native-vertical-visit-green.log`).

Actual outer horizontal scrolling failed1235→1235; stop-on-cancel failed the same missing-motion prerequisite. The first RTL setup changed only the window and failed its readiness range; setting the GTK default direction before fixture construction, restoring it at teardown and asserting the actual Miller direction produced the genuine RTL0→0 RED (`...-task3-rtl-ready-red.log`). The strip now reuses its existing native DropControllerMotion for one owned horizontal frame callback:32logical-pixel edges, maximum600px/s, elapsed clamp50ms, bounded adjustment and physical RTL geometry. It cancels on center/leave/new navigation/accept/end/deactivation/unmap/dispose and does no directory queries. Existing minimal-reveal work is cancelled while deliberate edge scrolling owns the viewport.

Focused six-case run passed (`...-task3-edge-six-first.log`): native Grid/List/Columns vertical0→216/290/63, horizontal1235→1011, RTL0→226, center/cancel stability, measured speed bound and an intentional300ms pause of the owned test main context resuming35.3px within the clamped allowance. This deliberate fixture pause is not reported as a product latency measurement. Two retained flat vertical controls increase final planned DnD cases33→35; all original14 Task3 workflows remain required.

The horizontal case now also completes an actual native copy into the visible ancestor after scrolling, checking one finished job, exact bytes and destination model, source retention and untouched other destinations (`...-task3-post-edge-copy-fixed.log`). Its first authoring script failed syntax before changing source; the shell then ran the unchanged cancellation case (`...-task3-post-edge-copy.log`), which is not copy acceptance. The corrected authoring/build/run sequence uses fail-fast shell behavior. The current25 authored cases in both real-backend profiles and operations63 are running as relevant regression checks; these are not the final35/64/full40/review gate.

Before completing this component, also add actual deeper hover-pair visibility/third-hover input coverage. The two-folder cancel test proves actual models and restoration, but does not by itself prove every newly disclosed child is physically visible. The current reconcile reveal hint follows the active source; whether a deeper hover needs a different reveal target is a hypothesis until genuine geometry/input reproduces it. No model-only or manual-scroll substitute closes that requirement.

### Current Task3 edge regression checkpoint

Both actual real-backend25-case runs passed with no skips, exec32090/43110 terminal0: `/tmp/nautilus-finder-dnd-task3-edge-twentyfive-{build-miller,build-preview-canonical}.log`. Each has26 actual native begin/payload/end observations,9 accepted drops and17 cancellations; strict case-name checks include all previous19, two flat vertical controls and four Columns edge cases. Actual metadata completion, overlapping end,3 native vertical-adjustment hits, frame-gap bound and post-edge native copy/model markers are present. Current actual operations63/63 also passed, exec46482 terminal0 (`...-task3-edge-operations.log`), including all native deletion/batch child markers.

Fresh plan-owned verify-edge-regression.py passed and matched the pre-run source fingerprint `6df7a99d34b19b114b6c62eceabd9352201ecce1882ffa15bc725e97f0e9e75f`; no app/test/tools/data or test-binary mutation occurred during the relevant run in its build. Canonical compilation completed before its drag run. These are focused/relevant regressions, not the pending full40/64/35-or-recorded-additions/review gate. Task3 is uncommitted/incomplete;10 original drag workflows, clipboard after actual tab destruction, deeper hover visibility, full component review, actual remotes and release gates remain required.

### Deeper spring-loaded pair visibility

Two genuine LTR/RTL tests reproduce the second hover opening a child outside the viewport: LTR child x1045+280 exceeds1040; RTL child x-285. The source remained the active action owner, so reconcile selected its reveal hint. Miller now reuses the existing minimal pair reveal for the actual hovered parent and child. Three genuine successive hovers pass in each direction, with both pair members visible, original payload/selection/action owner, unchanged history/no jobs, and exact cancel restoration (`/tmp/nautilus-finder-dnd-task3-hover-visibility-{ltr,rtl}-{red,green}.log`). No new hover or directory engine. These two retained cases raise final planned DnD35→37/current27. Remaining Task3 interoperability/lifetime cases and full regression/review gates remain open.

### Pending clipboard read after tab destruction

The64th operations case reuses the existing foreign clipboard provider and the native Copy/Paste path. With that owned provider stopped, Paste starts in a parent with a child disclosed; a new tab opens, then the originating tab closes through AdwTabView. Weak observations prove its slot and FilesView finalize before the provider resumes. The provider is resumed and reaped; no native job or destination write occurs and the surviving tab retains focus and its sentinels.

The first actual run confirmed native cancellation and owner release, then failed because expected `G_IO_ERROR_CANCELLED` was logged as a fatal test warning (`...-task3-clipboard-close-first.log`). The callback now quietly returns on that cancellation; other errors retain their warning. No new clipboard/lifetime mechanism was added. The new case and retained tab-switch and ordinary foreign-provider cases pass3/3 (`...-task3-clipboard-close-green.log`). Broader relevant regressions are running; this does not complete Task3.

### Native reuse audit and tab integration — revised scope

The user's2026-10-17 goal revision makes this an alternate view over Nautilus. Drag payload/negotiation and folder hover remain in GtkDragSource/NautilusListBase; transfers remain in `nautilus_dnd_perform_drop` and native jobs; vertical edge scrolling remains GTK's list implementation. Miller adds only source/action ownership, temporary branch snapshot coordination, existing minimal reveal, and outer-strip scrolling. The status overlay consumes native drag motion to expose GTK's existing vertical edge path. Clipboard destruction uses the existing cancellable, not an additional lifetime mechanism.

A genuine native drop onto another tab passed first run without a product change (`/tmp/nautilus-finder-dnd-task3-tabs-first.log`). The destination tab had a child endpoint with its parent still owning file actions. Nautilus's existing AdwTabBar handler placed the exact file in the child, left its parent and unrelated directories untouched, and preserved both histories. This specifically checks the endpoint/action-column distinction; it does not claim every upstream tab behavior was re-certified.

### Foreign GTK drag integration

A separate ordinary GTK client offers public GdkFileList data using a native GtkDragSource in a small mapped window. The existing owned-process/private-input/private-Home helpers guard and reap it. The recipient observes actual `text/uri-list` MIME and a native GdkDrop with no local GdkDrag. Direct incoming COPY passed without product changes. Hover then reproduced a real integration defect: the old hook required a local source drag and fell through to native flat-view activation, committing navigation during the drag (`...-task3-foreign-normal.log`, strengthened `...-foreign-identity-red.log`).

The hook now uses the current native GdkDrop as the temporary snapshot's identity. Existing branch snapshot/leave restoration/accepted-drop navigation are shared with local drags; the active selection is preserved for foreign hover. There is no new hover timer, protocol negotiation, directory model or transfer engine. Focused foreign direct COPY, hover/cancel (exact original snapshot), hover/drop (one native copy and one history commit), and two retained local nested-hover controls pass5/5 (`...-task3-foreign-expected-focused.log`, process12434 exit0).

Fixture failures remain recorded separately: buffered/pre-initialization marker protocol, a later test missing its expected-source reference, and moving/unmapping the source window during a drag. The working helper makes no window changes during its drag. These failures are not reported as Nautilus regressions.

### Native drag origin destruction

The initiating tab can close while its genuine drag has opened a temporary child. The new case uses the existing native close-page pipeline and observes actual cancellation/end, weak slot/view finalization, zero jobs and unchanged surviving tab location/history/focus/sentinels. It passes first run without another product change (`...-task3-tab-close-first.log`, process99932 exit0).

### Explicit remaining evidence limits

Outgoing source payloads still use Nautilus's unchanged public GdkFileList provider and GTK negotiation. Actual foreign-sink delivery is **unverified**. The newly prototyped sink check could not reliably arrange the owned windows/input coordinates in the private compositor; no failed attempt is counted as an outgoing pass or as a product defect. The complete prototype and all failed logs are retained in the plan workspace pending the component evidence archive. Its unaccepted registration and the experimental shared pointer-helper edits were removed from maintained code; no existing accepted regression was removed. This follows the revised goal's focus on demonstrated column integration defects and preserves honest compatibility claims.

The fixture also observed that shrinking an already focused window can leave its cached active column outside the viewport. This is an observed layout-policy follow-up for review; the outgoing prototype's controlled scroll setup was not treated as evidence that automatic resize reveal works. Broader backend/mount/removal matrices from the former goal are deferred, not passed. Current source/prototype boundaries and rulings are recorded in the component ledger.

### Current native integration regression checkpoint

Both development and canonical profiles pass all32 maintained genuine drag cases with the actual delayed backend, no skips (`/tmp/nautilus-finder-dnd-task3-native-integration-{build-miller,build-preview-canonical}.log`; processes84693/56013 terminal0). Each run observes30 local native drags plus3 real foreign drags: the overlap case contains2 physical drags. Local observers report10 accepted/20 canceled; foreign cases include2 native copies and1 exact-snapshot cancellation. Tab endpoint targeting, weak slot/view release, old metadata/native-end overlap, all edge controls and twelve physical hover-pair member bounds pass. Fresh `verify-native-integration.py` confirms these markers and unchanged pre-run fingerprint `38459c535c7f0b8d7a39fc776ccf8232ab3b53a7f3598d6e183cc2dd6a520d81`. This is a component checkpoint before the one fresh whole-component review. Full release regression and packages remain pending; outgoing foreign delivery remains unverified.


## Component closure after native lifecycle review

One whole-component [review/correction pass](dnd-review.md) is complete. Three
Important findings have actual RED→GREEN evidence: reentrant restore/close UAF
(ASAN), asynchronous status overlay mapping during a stationary native drag, and
new folder navigation while a native recipient holds completion after release.
Corrections retain the callback owner, use GTK child visibility, and distinguish
native gesture release from retained transfer lifetime. No replacement file-job,
vertical-scroll, hover, clipboard or extension engine was introduced.

The fresh source-frozen checkpoint passes both full Meson40/40, operations64/64
each, actual delayed-backend DnD35/35 each without skips, inspector53/navigation10/
delayed4, Python90/90 in187.814s and targeted ASAN1/1. Exec70095 and the fresh
strict verifier both exit0. Fingerprint and exact logs are in the review record.
The35 cases reflect authored regressions, not a retained historical numeric gate.
Outgoing foreign receipt, exact backend/modifier/removal combinations and the
window-shrink limitation remain explicit in all11 rulings; deferred tests are not
passes. This closes Task3/current drag component. Release preparation and a
separately authorized host deployment remain.
