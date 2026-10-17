# Fresh whole-component review

Reviewer: /root/dnd_component_review, gpt-6-astra high; read-only, no subagents.
Range: deb79d68acf30e612ec1f7d7db4e01e9568fefaa → 23352c24d3fcf86e3affabb337e1263b05f53921.

## Strengths

Native payloads, negotiation, pointer destinations, transfers and vertical scrolling preserved. Miller additions concentrated around temporary branches/outer scrolling. Existing slot cleanup reused for delayed origins. Owned drag/drop identities and disconnected column handlers address lifecycle risks. Tests use actual input, exact bytes/jobs/history/selection and weak owner release.28 extracted helper bodies unchanged except input rename/click delegation. Read-only verifier passed recorded32-case runs both profiles; no fresh UI/full Meson run by reviewer.

## Important

I1: spring_leave_idle uses borrowed self, calls restore_state (reentrant active-view/loading notifications), then dereferences self after restoration's own temporary reference ends. Synchronous tab/view teardown can free owner before line596. Disposal cannot protect an already executing idle. Retain self for entire callback; add restoration-notification teardown regression.

I2: FloatingBar suppression only on enter/motion. FilesView delayed status callback can set visible TRUE during stationary drag, reopening edge obstruction; ordinary hover timeout can also restore a previously hidden bar. Conversely leave unconditionally reshows even if owner requested hidden. Separate suppression from owner visibility, including status changes and interactive Stop transitions; test delayed stationary presentation and removal.

I3: selection_idle treats retained file_drag as an active gesture until dnd-finished, whereas native drop-performed is mouse release/accepted drop and receiver may read slowly before dnd-finished. Folder selection is discarded and not replayed after release; explicit navigation leaves guard. Separate gesture/navigation guard from retained transfer lifetime; test delayed recipient completion then actual new folder selection. Static lifecycle finding, not outgoing interoperability acceptance. Primary GTK4.22.5 gdk/gdkdrag.c.

Critical: none established.

## Minor

M1: interaction-contract.md paragraph57 still describes completed cases and superseded matrices as required. Documentation consistency polish; align at future docs pass.

## Declined to judge

D1 Complete outgoing foreign-client delivery: explicitly unverified; native provider preservation is not runtime receipt.
D2 Separate-window MOVE, bookmark/volume transfers, remote disconnect and repeated stress matrices: revised-goal deferrals, not passes.
D3 Actual source/target GIO deletion pending drag hover/scroll: absent; operation monitor tests separate.
D4 Modifier changes while crossing row/background/inactive destinations in one gesture: fixed modifiers only; exact sequence unverified.
D5 Automatic active reveal after window shrink: observed real layout follow-up predates DnD, distinct from coordinate fixture failures.
D6 Rapid foreign leave/reentry while slow restoration still loading: inspected, timing unverified, no additional definite defect.
D7 Source-column pruning by hover into another branch: retention inspected; descendant tests do not establish every post-prune focus outcome.
D8 X11/non-file text/image/archive drops: unchanged native mechanisms; runtime evidence Wayland files.
D9 Deeper displayed endpoint retained after ancestor drop: approved displayed-branch retention; native pointer still controls transfer.
D10 Prior preview/operations internals/exhaustive upstream compatibility: outside component; only extraction/cancel adjustment reviewed.
D11 Archived Markdown hard breaks/helper trailing blanks: cosmetic, no functional finding.

Original pre-correction verdict: ready with fixes. Three focused lifecycle/presentation findings before component completion; deferred evidence remains explicit.


## Correction pass — native integration only

All three Important findings were accepted at their reported severity. No Critical
was established. One correction pass follows this one immutable whole-component
review; no second reviewer is used. The full correction checkpoint is now green; all three Important findings are
corrected and exercised. This closes the drag component under the revised goal.

### I1 — reentrant restoration owner lifetime

A genuine source drag, spring disclosure and native strip leave restore the old
branch. A real `notify::loading` handler synchronously closes its originating
AdwTabPage. The normal test proves weak slot/view release and surviving-tab
integrity, but cannot diagnose invalid memory accesses. Under AddressSanitizer the
unfixed callback actually reads freed coordinator memory after `restore_state`
returns: `/tmp/nautilus-finder-dnd-review-restore-asan-red.log`, exit250,
`heap-use-after-free` in `spring_leave_idle`.

The correction retains the coordinator for the whole leave callback and the
native strip-enter callback that can invoke the same reentrant restoration.
No new state or timer is introduced. The same ASAN test passes, exit0, in
`/tmp/nautilus-finder-dnd-review-restore-asan-green.log`, with actual tab closure,
weak coordinator/slot/view release, zero jobs and unchanged surviving state.

ASAN uses a privately extracted, signature-verified Fedora44
`libasan-16.2.1-2.fc44.x86_64.rpm`; SHA256
`7a537e0276154d7c92976790657337ef5d543b8401ce6dd278498403cfc36328`.
No host package was installed. The instrumented build has docs/introspection off,
`b_sanitize=address`, `b_lundef=false`, private library RPATH, and runs with
`ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`. This establishes this exercised
memory-access correction; it is not a leak-safety or exhaustive sanitizer claim.

### I2 — drag suppression versus owner status visibility

A real FloatingBar is attached to the source view's actual overlay. A genuine
native drag enters while the independently controlled status owner is hidden;
the owner then requests presentation while the pointer remains stationary.
The unfixed widget maps into the native edge path: status-red.log, exit250.
The correction uses GTK child visibility for temporary drag suppression, leaving
the owner's visible property intact, tracks visits even when initially hidden,
and preserves interactive Stop transitions and the latest owner-hidden request.
The real widget regression passes in status-green.log, exit0. Original genuine
Grid/List/Columns native vertical scrolling controls also pass3/3 in
status-edge-controls.log; no vertical scrolling engine was introduced.
These paths have prefix `/tmp/nautilus-finder-dnd-review-`.

### I3 — pointer release versus retained data-transfer lifetime

A real GtkDropTargetAsync receives the exact public file payload but deliberately
holds native finish after accepted pointer release. The unfixed coordinator
ignores a subsequent genuine folder click: release-red.log, exit250.
The correction ends its gesture navigation guard on native `drop-performed`,
while retaining the source and GdkDrag until native finish/cancel. Spring guards
also consult the actual native pointer visit. The new click discloses before
finish, and late completion preserves the newer branch. The new case plus four
retained spring/overlap/foreign controls pass5/5 in release-green.log, exit0.
These paths have prefix `/tmp/nautilus-finder-dnd-review-`.
This same-process recipient lifecycle fixture is not outgoing foreign-client
interoperability or a file-operation acceptance test.

## Explicit rulings and costs

- D1: Outgoing foreign receipt remains unverified; native provider source reuse
  proves preservation only. Cost: no exercised outgoing sink delivery.
- D2: Separate-window MOVE/bookmark/volume/real-remote/stress matrices are deferred
  under the revised goal. Cost: exact combinations are untested.
- D3: Actual GIO source/target removal during drag remains unverified; operation
  monitor tests are separate evidence. Cost: that removal timing may conceal a gap.
- D4: Within-gesture modifier transitions remain unverified; fixed-modifier native
  negotiation is preserved and exercised. Cost: no exact transition acceptance.
- D5: Cached active columns can remain offscreen after shrinking the window;
  manual horizontal scrolling or column navigation is required. This pre-existing
  layout follow-up is not claimed fixed by hover-pair reveal tests.
- D6: Rapid foreign reentry during slow restoration is inspected but unexercised.
  Cost: a timing race may remain undiscovered.
- D7: Cross-branch source pruning retains payload ownership, and general tab
  destruction is exercised. Exact post-prune focus is unverified. Cost: that
  focus sequence may require a later correction.
- D8: Runtime evidence is Wayland file drags. X11 and other content use native
  paths but remain unverified. Cost: no exercised compatibility promise there.
- D9: An accepted ancestor drop retains an existing deeper displayed branch;
  native pointer destination controls transfer. Cost: displayed endpoint can
  differ from the transfer destination; this is the approved branch policy.
- D10: Unchanged preview/operation internals retain their earlier direct evidence
  and reviews, with a fresh regression checkpoint after these corrections.
  Cost: no new exhaustive upstream audit.
- D11: Archive Markdown/helper whitespace remains unchanged. Cost: cosmetic only.

Minor M1 remains deferred: the historical interaction-contract paragraph still
contains superseded acceptance wording. The active goal and explicit scope
amendments take precedence; this is documentation polish, not a passing matrix.


## Final correction checkpoint

All11 commands exited0, and a fresh independent read of their outputs through
`verify-review-correction.py` also exits0. Application/test/tools/data fingerprint:
`60a3a86987485266a54fc16c4d0154ed93aee23f909e40a4babbdc0503d17447`.
Those files and each running build's binaries remained unchanged throughout.
Release documentation/spec preparation outside those directories did not change
the tested application. Exec70095 is terminal0; no live component test remains.

- Both development and canonical full Meson40/40, with operations64/64 each,
  including native clipboard completion after originating-tab destruction.
- Both separate actual delayed-backend DnD35/35, no skips. Ordinary Meson has one
  explicit external-only late-origin skip, covered by these actual runs.
- Actual inspector53/53, genuine navigation10/10, delayed history4/4, and
  Python90/90 in187.814s, including native persistence and actual Sushi workflows.
- Targeted ASAN restoration-close1/1, following the reproduced memory RED.
- Both real native portal smoke tests pass (3.38s/3.26s).

Driver `/tmp/nautilus-finder-dnd-review-correction-runner.log`, aggregate
`/tmp/nautilus-finder-dnd-review-correction.log`, parts0..10, detailed
`/tmp/nautilus-finder-dnd-review-correction-{build-miller,build-preview-canonical}-testlog.txt`.
The plan-owned runner/verifier and raw failures/prototypes are preserved in the
component evidence archive before scratch cleanup. This closes the component,
not package/CI/readiness preparation or host deployment. No production install,
default change, offline reset or push occurred. All11 rulings and deferred MinorM1
above remain part of the handoff; they are not passing acceptance claims.


### Durable evidence

`build-evidence/miller-dnd-81240946e.tar.xz` is an ignored local evidence bundle,
1,194,268bytes, SHA256
`e456b62670af5f964a46e85600152267dbe20bd0f274bd543d49e28de1a0f59e`.
Its263 members preserve the completed plan ledger/briefs/runner/verifier/review,
immutable final diff, rejected outgoing fixture prototypes and raw passing and
failed logs. Every payload was checked against the internal SHA256 manifest before
plan scratch cleanup. Production application correction commit: `81240946e`.

Release documentation follow-up: deferred MinorM1 is now resolved by aligning the interaction contract with the revised goal and completed component evidence. Its original review/correction disposition above remains historical.
