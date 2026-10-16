#!/usr/bin/env bash
set -euo pipefail
miller_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export GSETTINGS_SCHEMA_DIR="$miller_root/build-miller/data"
exec "$miller_root/build-miller/src/nautilus" "$@"
