#!/bin/sh
set -eu

binary=${1:?kilix-techno binary required}
case $binary in
    /*) ;;
    *) binary=$(CDPATH= cd -- "$(dirname -- "$binary")" && pwd)/$(basename -- "$binary") ;;
esac
command -v script >/dev/null 2>&1 || {
    echo 'test-terminal: SKIP (script unavailable)'
    exit 0
}
command -v timeout >/dev/null 2>&1 || {
    echo 'test-terminal: SKIP (timeout unavailable)'
    exit 0
}

scratch=$(mktemp -d /tmp/kilix-techno-terminal-XXXXXX)
log=$scratch/session.log
cleanup() {
    rm -rf -- "$scratch"
}
trap cleanup EXIT HUP INT TERM

(sleep 1; printf 'q') |
    XDG_DATA_HOME=$scratch/data timeout 8s script -qec \
        "$binary --new --cell --no-audio" /dev/null >"$log" 2>&1

escape=$(printf '\033')
grep -Fq "${escape}[?1049h" "$log"
grep -Fq "${escape}[?1049l" "$log"
grep -Fq "${escape}[?25h" "$log"
test "$(stat -c '%a' "$scratch/data/kilix-techno/settings.state")" = 600

export_log=$scratch/export-session.log
(sleep 1; printf '\005'; sleep 3; printf 'q') |
    XDG_DATA_HOME=$scratch/data timeout 10s script -qec \
        "cd $scratch && $binary --new --cell --no-audio --bars 1 --sample-rate 8000" \
        /dev/null >"$export_log" 2>&1
test "$(dd if="$scratch/kilix-techno-export.wav" bs=1 count=4 2>/dev/null)" = RIFF
grep -Fq "${escape}[?1049l" "$export_log"

echo 'test-terminal: OK'
