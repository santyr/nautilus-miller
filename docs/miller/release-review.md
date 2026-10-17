# Release component review and decisions

Fresh independent read-only review: `81240946e059e41c5264dafbfa4c492f93d0af0f..36fad47d99708ede1909e77f6d8616dae9a2f04f`, 2026-10-17. Reviewed complete diff, goal, plan, ledger rulings, native harness/deployment paths, built artifacts and recorded logs. No second DnD review or second release reviewer.

No Critical or Important implementation findings. The reviewer independently checked all manifest checksums, matching main/extensions/devel identities/dependencies and canonical assets, source archive equality to commit608a4426a, real transaction logs, native-close large/deep logs and unchanged installed activation-helper evidence. Production C is unchanged by this release component. Remote CI was explicitly unresolved when reviewed.

## Minor M1

The current acceptance table still described finished packages/publication/handoff as pending. This is corrected in the already planned Task4 status reconciliation, with links to [release readiness](release-readiness.md). It requires no runtime fix or new feature. No deferred release-review minor remains.

## Reviewer boundaries — final rulings

Each numbered boundary was explicitly declined by the reviewer and ruled on by the implementer. These are limits on evidence, not passing compatibility claims.

1. **Repeated DnD review:** retain the completed independent [DnD review](dnd-review.md) and its corrections because production code is unchanged. Cost if wrong: a prior undetected drag defect could remain.
2. **Exhaustive remote/GVfs/OpenPGP/extensions:** retain existing native providers and disclose missing runtime certification under the amended goal. Cost: untested backend or plugin integration may fail.
3. **Outgoing foreign receipt, exact modifiers/removal/backend combinations:** preserve prior explicit deferrals; actual local/incoming evidence does not prove these combinations. Cost: a gap specific to those combinations may remain.
4. **Automatic reveal after window shrink:** disclose cached active column can remain offscreen; horizontal scroll/navigation remain usable. Cost: users may need an extra reveal action.
5. **Broad performance/accessibility/display certification:** accept focused native keyboard/control/RTL and large/deep cleanup evidence; do not infer full screen-reader/display/resource coverage. Cost: untested presentation or resource issues may remain.
6. **Full desktop/reboot/power-loss/host deployment:** keep separate from disposable transactions/private activation and require the goal's separate host deployment instruction. Cost: live-session/recovery behavior is not fully demonstrated.
7. **Optional-devel transaction:** metadata, assets, dependencies and digests are checked; real transactions cover this host's main/extensions pair. Cost: actual devel installation is unexercised.
8. **More rootless privileged portal infrastructure:** retain recorded failures and stop expanding unrelated CI infrastructure; observe the intended GitHub environment. Cost: that local portal timing remains unresolved.
9. **Remote CI success:** require an actual observed run; workflow syntax and local results cannot substitute. Cost: handoff completion waits for that observation.
10. **Full installer/rollback end-to-end:** distinguish existing deployment regressions, actual DNF transactions and unchanged activation-helper verification from an entire live desktop deployment. Cost: combined live-session installer failure paths remain unexercised.

## Operational decisions carried from the ledger

- Continue authorized inline release preparation without another design gate; publication/host installation still need applicable authorization. Cost: routine release choices are made by the implementer.
- Prepare independent CI/test/docs inputs before the final package archive, then close artifact-consuming transactions after the build. Cost: preparation overlaps rather than following a strictly serial task order.
- Use the whole disposable container Home for installed RPM verification instead of an extra UID-masking Home namespace. Native `rpm -V` must see root ownership; existing private bus/XDG and owned-container marker stay in force. Cost: this activation check does not itself exercise the extra Home wrapper; maintained source suites do.
- Stop unrelated rootless compositor/portal expansion and use actual intended GitHub CI. Cost: local privileged portal timing remains unverified; local39/40 is not green CI.
- Use original explicit public `santyr/nautilus-miller` authorization for feature-branch publication and CI. Cost: source is public before final host handoff; no main merge, binary release or host install is implied.
- Permit native user namespaces only on the disposable GitHub-hosted VM, preserving the bwrap/Home test guards. Cost: unprivileged namespaces are available to code on that ephemeral CI VM, with no secrets and read-only repository permissions. Ubuntu's host restriction is documented in the [official release notes](https://documentation.ubuntu.com/release-notes/24.04/).

See [release readiness](release-readiness.md) for final observed CI and deployment status, and the local release evidence bundle for raw logs and ledger.

## Final CI correction verification

Post-review changes are limited to the workflow's ephemeral hosted-VM namespace
allowance and existing Papers/FFmpeg test dependencies, plus final documentation.
Actual run39071637198 atf69f31027 passes both profiles,40/40 Meson executables and
37 deployment regressions each. Native namespace failure was reproduced before
the allowance; PDF capability False→True and real existing WebM/WAV fixture
encoding confirm the missing dependencies. No application rendering, transfer
code or maintained assertion was changed to obtain this result. The earlier
failures and canceled run remain in the release evidence.

The complete review disposition, original ledger and exact raw evidence are
preserved in the verified local release bundle listed in
[release readiness](release-readiness.md#durable-release-evidence). The completed
plan's scratch directory is removed after that preservation; the working branch
and package artifacts remain for host deployment handoff.
