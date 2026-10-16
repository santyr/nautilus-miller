# Input review and remaining acceptance

Fresh independent read-only review of `70cef4ba2e44ef79b5d5d6b2105090b26ff13b2f..f5c2365a383e14183388bd917a605d52390bc64b` found no Critical findings, one Important finding and one Minor. This is the input component of Stage2; it does not close Stage2 or C01–C16.

## Important correction

An unconsumed modifier-only press reached the engine's generic reset path. Consequently, `b` followed by Shift+E became `E`, losing the existing prefix and potentially selecting an unrelated file. The reviewer reproduced it with genuine private-compositor input, `/tmp/nautilus-finder-input-review-mixed-case.log`.

The committed engine test was extended with actual Shift+E mid-prefix input and Backspace assertions. It failed before correction (`bE` never appeared; the incorrect suffix eventually expired). After allowing the IM context to process an event, the engine now propagates unconsumed pure modifiers without clearing the prefix. Actual modified shortcuts still reach their existing handling. The regression also checks a standalone Control press/release preserves the prefix. RED/GREEN: `/tmp/nautilus-finder-input-review-shift-{red,green}.log`.

The correction passed the full private-session suite **35/35**, including nine engine cases and the genuine Shift/Control regression, `/tmp/nautilus-finder-input-review-suite.log`; detailed `/tmp/nautilus-finder-input-review-testlog.txt`. External Anthy conversion/tab-switch/tab-close passed again after the suite rebuilt all consumers, `/tmp/nautilus-finder-input-review-anthy-final.log`. Native dconf restart passed with writer PID76331 and reader77124, `/tmp/nautilus-finder-input-review-restart.log`. Profile isolation and diff checks passed. The helper/session/deployment suites had just passed3/3,4/4 and37/37; their code was unchanged by this correction. No second reviewer was requested; this was one red/green correction pass plus the full suite.

## Deferred minors

- Rejected-length feedback can outlive its correction: Backspace does not clear `limit_hit`, and an over-limit commit from an empty prefix does not arm expiry. Normal accepted typing, Escape/reset or the existing prefix timeout dismisses it, but the hint can remain misleading. Carry this into the required Stage5 input/accessibility polish; no selection or filesystem change is involved.

## Rulings I made

1. Continue native staged execution without repeated design/preference approval: the full supplied contract explicitly directs that workflow. Cost if wrong: reversible input choices may need revision; production deployment remains separately authorized.
2. Use NFKC plus Unicode case folding for comparisons, retaining entered buffers and actual file identities. Cost if wrong: compatibility-equivalent names may share a prefix match; first current-sort match wins, with no rename or activation.
3. Replace the Blueprint directory output with individual UI output targets: a real Preferences test exposed directory mtimes hiding a changed source, even after a full build. Cost if wrong: another supported builder may need a Meson target-layout adjustment; runtime APIs are unchanged.
4. Complete genuine file clipboard, drag and undo correctness, including explicit Ctrl+C/V propagation, in Stage4. This input review found no demonstrated shortcut defect but does not establish those workflows. Cost if wrong: native operation reuse could conceal an integration failure.
5. Complete all Home/End/Page, multiselection, middle-click and modifier-activation combinations during broader keyboard/operation acceptance. Cost if wrong: tested key boundaries could conceal another native-input regression.
6. Anthy proves the exercised real composition path; other engines, layouts, candidate controls and deliberately delayed provider replies remain required broader input/resilience checks. Cost if wrong: engine-specific behavior or late replies could still fail.
7. Complete top-level application/window focus departure and all dialog/inspector control interactions with the broader focus/preview stages. Listing and tab departure are tested; no additional concrete defect was found. Cost if wrong: focus outside those covered boundaries could retain stale input state.
8. Keep real100k-directory/backend responsiveness benchmarks mandatory Stage5 work. The injected loaded-model case proves bounded scan yielding, not enumeration, latency percentiles, monitors, CPU/RSS or upstream comparison. Cost if wrong: an algorithm pass could be mistaken for responsive real browsing.
9. Keep long branch/tab lifecycle resource stress mandatory Stage5 work. Explicit cleanup and disposal tests do not establish settled resource bounds. Cost if wrong: slower leaks or cache growth may remain.
10. Complete stale selection/model safety under delayed remote/backend mutation. Current generations, loading reset, synchronous selection observation and source cancellation cover the tested transitions only. Cost if wrong: slow services may reveal another stale transition.
11. Complete actual screen-reader, RTL, high-contrast, scaling and enlarged-font acceptance. Plain hint text/accessibility properties do not close C13. Cost if wrong: controls may remain difficult to discover or use.
12. Complete history/session restoration in the remaining Stage2 work. Separate-process native dconf acceptance proves only width/default/input-preference persistence. Cost if wrong: returning to history/tabs may lose active selection or scroll state.
13. Preview, real operations/drag, remote services, packaging transactions, observed release CI and rollout remain mandatory full-contract work; allC01–C16 stay open. Cost if wrong: an input component pass could conceal missing everyday-manager functionality.
14. Session guards address accidental, stale or copied metadata and prevent host input delivery; they are not certified against a malicious actor controlling the same account. Cost if wrong: same-user adversarial tampering could bypass assumptions beyond this test-safety model.
15. Explicit Blueprint outputs and schema dependencies are verified on the documented Fedora toolchain; other supported build environments still need CI evidence. Cost if wrong: a different builder could expose portability errors.

The reviewer inspected the supplied full-suite, Anthy and restart logs, and independently ran only the guarded Shift probe. The correction and subsequent gates belong to the implementer. No production install, host input injection, external message or real user-file/dictionary write occurred.
