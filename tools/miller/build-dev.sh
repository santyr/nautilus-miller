#!/usr/bin/env bash
set -euo pipefail
miller_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
meson setup "$miller_root/build-miller" "$miller_root" --prefix="$HOME/.local/opt/nautilus-miller" -Dprofile=Miller -Dtests=all "$@"
meson compile -C "$miller_root/build-miller"
