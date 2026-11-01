#!/bin/bash
# Repair only the verified mode-000/invalid-owner container migration fault.
set -euo pipefail
cd "$(dirname "$0")"
case ${1:-} in
    '') apply=0 ;;
    --apply) apply=1 ;;
    -h|--help)
        echo 'Usage: bash tools/repair-installcoordination.sh [--apply]'
        echo 'Inspect the known installcoordinationd directory fault; --apply repairs only that exact fault.'
        exit 0 ;;
    *) echo 'Usage: bash tools/repair-installcoordination.sh [--apply]' >&2; exit 2 ;;
esac
(( $# <= 1 )) || exit 2
exec bash ./ssh.sh -o BatchMode=yes -o ConnectTimeout=8 \
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
    "/var/jb/bin/sh -s -- $apply" <<'GUEST'
set -eu
apply=$1
base=/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd
# Refuse symlinks throughout the selected container path.
for dir in /private/var/containers /private/var/containers/Shared /private/var/containers/Shared/SystemGroup "$base" "$base/Library" "$base/Library/Caches"; do
    if [ -L "$dir" ] || [ ! -d "$dir" ]; then
        echo "Missing directory or symlink; leaving it unchanged: $dir" >&2
        exit 1
    fi
done
for name in Library Library/Caches; do
    dir=$base/$name
    state=$(/var/jb/usr/bin/stat -c '%u:%g:%a' "$dir")
    case $state in
        4294967294:4294967294:0)
            echo "Known migration fault: $dir ($state)"
            if [ "$apply" = 1 ]; then
                /var/jb/usr/bin/chown 501:501 "$dir"
                if [ "$name" = Library ]; then /bin/chmod 0755 "$dir"; else /bin/chmod 0700 "$dir"; fi
                /var/jb/usr/bin/stat -c 'Repaired: %n %u:%g mode=%a' "$dir"
            fi ;;
        *) echo "Unchanged: $dir ($state)" ;;
    esac
done
GUEST
