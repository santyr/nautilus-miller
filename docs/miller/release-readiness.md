# Miller2 release readiness

Status: ready for the host deployment handoff. RPMs, disposable update/restore,
actual installed-package activation and GitHub CI pass. Independent release
review found no Critical/Important implementation defect; its stale-status minor
is resolved. Host deployment has not been performed by this completion goal.
[Independent review and decisions](release-review.md).

## Source and packages

Fedora44/x86_64, Nautilus50.4, release `1.miller2.fc44`. Public project:
[santyr/nautilus-miller](https://github.com/santyr/nautilus-miller).
RPM source commit: `608a4426a6a027f541829348184b9c7d00071958`.
The decompressed source archive exactly matches `git archive` of that commit.
Later workflow/documentation commits do not change the packaged application,
tests, native build configuration or deployment scripts.

| Package | SHA256 |
|---|---|
| `nautilus-50.4-1.miller2.fc44.x86_64.rpm` | `114afbca4a52767d63246a267ea40fe64c02a047ace2ba64fbb80060c76dcb93` |
| `nautilus-extensions-50.4-1.miller2.fc44.x86_64.rpm` | `7b39388beac6f51533645ac084ae6ba7044b1786fd131dfb6075de34c5f219c7` |
| `nautilus-devel-50.4-1.miller2.fc44.x86_64.rpm` | `0556580ced184e1670f570dbe0e4887284e1c4e3f27610085c3bb06ee95baabd` |
| `nautilus-50.4-1.miller2.fc44.src.rpm` | `5137ce0ba44a5d475cab67a3f8ace6876408f4a6f6ccb5f12b938b6c6f468acb` |

Binary packages are under `build-rpm/RPMS/x86_64`; source RPM under
`build-rpm/SRPMS`. Debug/source packages, source archive and full provenance are
listed in `build-rpm/release-manifest.json`, `source-commit.txt` and `SHA256SUMS`.
Every manifest checksum verifies. Local fork RPMs are unsigned; their digests
verify. Saved Fedora rollback RPM signatures verify. Main/extensions/devel have
matching versions/architecture and exact split dependencies. Canonical desktop,
services, schema, API4.1/ABI4 and devel assets are present; production packages
contain no development desktop identity.

## Current-goal evidence

| Requirement | Direct evidence |
|---|---|
| Native reuse | One existing FilesView per directory; native GIO/GVfs objects, operations/actions/clipboard, ListBase hover/provider and GTK vertical scrolling. Miller owns branch/action/focus/reveal coordination. [Operations](operations-review.md), [drag](dnd-review.md), [preview](preview-review.md). |
| Hierarchy/navigation/focus/history | Both-profile full40-executable component checkpoint; actual navigation10 and delayed history4. Includes parent/empty destinations, native tabs/history/special views and late completion. |
| Actions, clipboard and pointer destination | Actual native operations64 per profile and real delayed-backend drag35 per profile without skips; real bytes/jobs/untouched destinations and weak owners checked. Three drag review findings have RED→GREEN; teardown has actual ASAN UAF reproduction/correction. |
| Preview | Actual inspector53 and Python90, including native persistence and real Sushi workflows; existing glycin/thumbnail/Sushi providers retained. Further formats are optional. |
| Controls/readability/RTL | Existing keyboard UI and accessible control-name assertions pass in the full suite; actual native LTR/RTL hover pairs are physically visible. Prior layout screenshot has readable native styling. No comprehensive screen-reader/display certification is claimed. |
| Real large/deep branch and cleanup | Added existing-fixture smoke:12 levels/5000 actual regular files plus empty child, exact5001 rows, genuine Home/Right/Left and unrelated navigation, weak first/endpoint/coordinator release through existing native close. Both profiles pass. Observed entry+load2124.4/2117.5ms and worst10ms-heartbeat gaps156.2/116.3ms. These include typing/layout and concurrent private sessions, not pure enumeration/input latency or upstream benchmarks. |
| Extension boundary | Existing FilesView provider/action API and extension loader preserved. Host has neither seahorse-nautilus nor nautilus-python; runtime workflows remain unverified. [Compatibility](extension-compatibility.md). |
| Real package recovery | Rootless Fedora44 stock→fork, exact host-matching Miller1→Miller2, and fork→signed stock DNF transactions pass; rpm-V clean at each stage. Existing columns-view preference persists through1→2; native list-view set before stock schema downgrade and reads correctly afterward. |
| Installed runtime | Actual packaged `/usr/bin/nautilus` starts as a nonroot user on an owned private Mutter/bus in a disposable container. Existing `deploy.verify_running` verifies both canonical owners, installed ELF inode, exact split RPMs/integrity, native handler and columns-view preference. Existing Quit/drain reaps it. |
| Safe deployment phases | Existing unit checks cover signatures, original snapshot preservation, split validation, drain refusal, offline opt-in, changed payload, transaction/launch/verification failure and recovery journal. Transactions plus private packaged activation do not prove full desktop/reboot/power-loss behavior. |
| CI | Both-profile Fedora44 workflow retains native session/Home/input guards. Actual run39071637198 passes40/40 Meson executables and deployment regressions in each profile; see [CI history](#observed-github-ci). |

The complete drag checkpoint is recorded in [review/evidence](dnd-review.md),
including the verified263-member local archive and all11 rulings. Existing
component evidence applies to unchanged runtime source; the later large/deep case
adds focused evidence without repeating unrelated upstream certification.

## Observed GitHub CI

The workflow builds production and Miller profiles on a disposable GitHub-hosted
Ubuntu VM with a privileged Fedora44 container. It runs the existing Meson tests
and deployment regressions as a nonroot user with the canonical Home guard,
private session and native Cairo renderer. Actions are pinned; contents permission
is read-only. This does not require changing this laptop's host configuration.

| Run | Observed result |
|---|---|
| [39070565375](https://github.com/santyr/nautilus-miller/actions/runs/39070565375) | Both builds pass; guarded tests fail before application execution at native bwrap UID mapping. |
| [39070909427](https://github.com/santyr/nautilus-miller/actions/runs/39070909427) | Hosted-VM namespace correction passes, including real PNG sandboxing. Both profiles39/40; only PDF preview fails because the image lacks the host's Papers thumbnail provider. |
| [39071544094](https://github.com/santyr/nautilus-miller/actions/runs/39071544094) | Superseded/canceled when the existing media-fixture encoder dependency was identified; neither a pass nor a product test failure. |
| [39071637198](https://github.com/santyr/nautilus-miller/actions/runs/39071637198) | **Success** at source `f69f31027c8a6bdae3500890e9abd1124d1c3a7e`: both builds, Miller identity, native namespace preflight,40/40 Meson executables and37 deployment regressions per profile pass. Uploaded JSON logs independently checked:40 `OK` results each. |

Only CI environment dependencies/policy changed after the first actual run.
The existing Papers provider makes native PDF capability change False→True in
the disposable container. Fedora's ffmpeg-free successfully generates both exact
unchanged WebM/libvpx and WAV fixture commands. These checks establish dependency
capabilities; the complete green run also establishes the actual preview assertions.
No provider or rendered result is mocked and no maintained case is disabled.

Earlier local container diagnostics remain failures: native system bus/PNG
provider setup and Cairo selection corrected startup dependencies, then39/40
passed with an uncached-thumbnail failure at a nested proc mount denied by the
unprivileged rootless container. A local privileged rootless trial instead hit a
document-portal startup timeout before thumbnail execution. These are retained
as environment limitations, without counting them as green. The direct source-host
component checkpoint passes both40 and the separately recorded native provider
cases. The intended rootful GitHub environment supplies the green result above. Ordinary
Meson runs retain their standard external-fixture case skips; the separately
recorded real archive/drag/preview component runs supply those focused boundaries.
No comprehensive remote-service or third-party plugin certification is implied.

## Material limits

- Outgoing foreign-client receipt, exact within-gesture modifier transitions,
  source/target removal timing and some window/bookmark/volume/backend combinations
  remain unverified; source reuse alone is not runtime compatibility.
- The cached active column can remain offscreen after shrinking a window; use
  horizontal scrolling or column navigation. Hover-pair reveal is tested.
- Real SFTP/SMB and complete OpenPGP/provider workflows remain follow-ups. Current
  native drag runtime evidence is Wayland files. No new provider engine is added.
- The ASAN run targets reentrant restoration memory access with leak detection
  disabled; it is not a general leak/resource certification.
- Optional devel is built/metadata-checked; real package transactions match this
  host's main/extensions installation. Full-desktop failure injection and reboot
  recovery are unexercised. Prior review minors/rulings remain in their component
  records; the stale interaction-contract wording was aligned during this release.

## Deployment handoff

Run only after separately authorizing this host deployment, as the normal desktop
user. From this completion checkout:

```sh
(cd build-rpm && sha256sum -c SHA256SUMS)
./tools/miller/install-default.sh build-rpm/RPMS/x86_64/nautilus-50.4-1.miller2.fc44.x86_64.rpm
```

The existing installer selects native `columns-view`, verifies canonical owners,
installed executable and handler, and preserves the original rollback snapshot.
It requests sudo locally and refuses a pending offline transaction without the
explicit reset option. [Phase/recovery details](deployment.md).

Restore the saved signed stock packages and original settings with:

```sh
./tools/miller/restore-stock.sh
```

The host remains on the earlier Miller1 installation during this goal. A package
build, private activation check or container transaction is not host deployment.

## Durable release evidence

The ignored local bundle `build-evidence/miller-release-f69f31027.tar.xz` preserves
the complete release ledger, review/rulings, immutable diff, all four task
closures, passing/failed/canceled diagnostics, uploaded native CI logs, artifact
provenance and owned-container cleanup. It contains108 regular files:107 payloads
plus the internal SHA256 manifest. Every payload was verified before scratch
cleanup. Documentation snapshot: `241e13c50`; package/CI identities remain above.

Size:8,524,760bytes. SHA256:
`ece89f14952d5a8e765e321b6f488f56127ba6daf7f20403946c30c79084c23a`.
The adjacent `.sha256` file verifies the complete archive. Packages and the prior
component evidence bundles remain locally available; binaries were not published
as a GitHub release. The public `finder-completion` source branch is preserved.
