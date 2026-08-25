#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

bad=$(git ls-files | awk '
    /^third_party\// { next }
    tolower($0) ~ /\.(html?|mjs|cjs|js|css|png|jpe?g|gif|webp|svg|wasm)$/ {
        print
    }
')
if [ -n "$bad" ]; then
    printf '%s\n' "clean-room: excluded application-owned paths:" >&2
    printf '%s\n' "$bad" >&2
    exit 1
fi

expected='chip-sequencer 972e67b9d24bc1e35aef4fdaaad96922cb3fe033
pcm-mixer 5f073f6a33e5caa3e36c1209bf65b8a780025d9b
kitty-terminal-session b6b4e1e4b6933d60bfc919a90d8e1f6f86b20e77
soft-raster 2b0c241729dec182a87ebab78edd10b142edf828
kilix-state 35f742d6cf2f7635b43157fcf6f2e0264bf0d761'
printf '%s\n' "$expected" | while read -r name revision; do
    path="third_party/$name"
    actual=$(git -C "$path" rev-parse HEAD)
    test "$actual" = "$revision" || {
        printf 'clean-room: wrong %s pin: %s\n' "$name" "$actual" >&2
        exit 1
    }
    test -f "$path/LICENSE" || {
        printf 'clean-room: %s has no root LICENSE\n' "$name" >&2
        exit 1
    }
done

if [ -x /home/pleb/bin/hygiene-scan ]; then
    /home/pleb/bin/hygiene-scan --recurse
fi
printf '%s\n' 'clean-room: passed'
