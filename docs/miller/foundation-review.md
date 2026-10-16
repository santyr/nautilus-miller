# Foundation review and correction

Scope: foundation plan (Stages 0–1), baseline `8618e792713ae6b248c4ca7d740ae78ce9d3040b` through `ca4d08549050acf878dec5ab7f6cf47a2bc05779`. Fresh independent read-only review found no Critical findings, one Important finding and no deferred Minors. The full completion goal and C01–C16 remain open.

## Important correction

`rpmkeys --checksig` alone accepted an unsigned but digest-valid artifact under this host's RPM configuration. Consequently it could not establish the promised Fedora rollback provenance. A real disposable unsigned RPM with stock package metadata reproduced acceptance before the correction (`DeploymentSignatures.test_unsigned_stock_identity_is_not_trusted`). Validation now requires valid signatures and digests in an isolated keyring containing only the current host Fedora release's primary signing key. Both actual saved stock RPMs passed this stricter read-only check. No host transaction occurred.

Red: `/tmp/nautilus-finder-signature-red.log` (assertion: no exception for unsigned RPM). Green: `/tmp/nautilus-finder-signature-green.log`, **37/37 Python tests**. Six added focused runtime helper tests cover held-operation drain refusal, matching activation, mismatched service owners, old executable inode, incorrect package and incorrect folder preference, mocking only their external dependencies. These supplement orchestration tests; they do not replace disposable transaction or real desktop acceptance.

Full post-correction regression: private Mutter/Wayland + private D-Bus and disposable XDG state, **29/29 Meson executables passed**, portal 0.95 s. Log `/tmp/nautilus-finder-foundation-review-suite.log`. Cleanup emitted expected bus/compositor shutdown messages after the passing summary. Existing AT-SPI startup failure in the private session remains a Stage 5 harness issue, not a screen-reader pass.

## Rulings I made

1. Advance the narrow unchanged-parent-selection correction from foundation Task 4 into Task 2: wrong action ownership blocked trustworthy integration testing. Cost if wrong: changed task ordering, with expanded targeting acceptance still required.
2. Carry transient empty non-rightmost ancestor acceptance into Stage 5 mutation/backend fixtures: a valid ancestor normally contains the displayed child. Cost if wrong: a transient wrong-destination case remains open; ordinary parent background and empty child are tested.
3. Keep inline inspector and complete external preview ownership in mandatory Stage 3. Cost if wrong: discovery tests alone could conceal first-request, focus or stale-window faults; C07/C08 remain open.
4. Keep width persistence, minimal reveal, Unicode/IME typeahead and complete history restoration in mandatory Stage 2. Cost if wrong: foundation remains unsuitable as a completed Finder replacement; C09/C13 remain open.
5. Keep genuine clipboard, drag-and-drop, spring loading and complete operation acceptance in mandatory Stage 4. Cost if wrong: synthetic tests could conceal compositor/input and destination faults; C02–C06 remain open.
6. Keep actual SFTP/SMB, performance, screen-reader, RTL/scaling and transient ancestor invalidation in mandatory Stage 5. Cost if wrong: local tests could conceal remote/lifetime/accessibility defects; C10/C13/C14 remain open.
7. Keep observed GitHub CI, real disposable package transactions and interruption recovery in mandatory Stage 6. Cost if wrong: mock success could conceal release and recovery failures; C15/C16 remain open.
8. Keep the installed host unchanged: current authorization covers isolated completion work, not another production installation. Cost if wrong: new development capabilities are not yet available in the user's default manager; installation awaits separate explicit deployment instruction.

## Deferred minors

None. No second review was requested; the Important correction received one red/green fix pass and a full green suite. No later-stage requirement was waived by this review.
