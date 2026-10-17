#!/usr/bin/env bash
set -euo pipefail
miller_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$miller_root"
if [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
    echo 'Commit source changes before building the release archive.' >&2
    exit 1
fi
miller_top=${MILLER_RPM_TOP:-"$miller_root/build-rpm"}
mkdir -p "$miller_top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
cp packaging/fedora/nautilus-miller.spec "$miller_top/SPECS/"
cp packaging/fedora/default-terminal.patch "$miller_top/SOURCES/"
git archive --format=tar --prefix=nautilus-50.4/ HEAD | xz > "$miller_top/SOURCES/nautilus-50.4.tar.xz"
git rev-parse HEAD > "$miller_top/source-commit.txt"
rpmbuild -ba --define "_topdir $miller_top" "$miller_top/SPECS/nautilus-miller.spec" "$@"
