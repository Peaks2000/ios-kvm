#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../vendor/hakios"
exec ssh -F /dev/null -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new \
  -o UserKnownHostsFile=ssh/known_hosts -i ssh/client_ed25519 \
  -p "${QEMU_SSH_PORT:-2222}" root@127.0.0.1 "$@"
