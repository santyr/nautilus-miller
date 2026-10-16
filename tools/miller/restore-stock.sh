#!/usr/bin/env bash
set -euo pipefail
miller_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$miller_tools/deploy.py" restore "$@"
