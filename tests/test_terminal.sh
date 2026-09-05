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

# Shared-autosave ownership. autosave.state belongs to the session that loaded
# it or found it absent; a --new session started while it holds a project must
# autosave to its own slot and may replace the shared slot only by a second
# Ctrl+S. Before this rule, the second session's single toggle destroyed the
# first session's explicitly saved project after two seconds, silently.
# Each session makes a different edit, so an overwrite changes the checksum
# instead of rewriting the shared slot with identical bytes.
own=$scratch/own
shared=$own/kilix-techno/autosave.state
session() {
    XDG_DATA_HOME=$own timeout 15s script -qec \
        "$binary --new --cell --no-audio" /dev/null >"$1" 2>&1
}
(sleep 1; printf '\r'; sleep 1; printf '\023'; sleep 1; printf 'q') |
    session "$scratch/own-1.log"
test -f "$shared"
first=$(cksum <"$shared")
(sleep 1; printf '\r+'; sleep 3; printf 'q'; sleep 1; printf 'q') |
    session "$scratch/own-2.log"
test "$(cksum <"$shared")" = "$first"
test "$(ls "$own/kilix-techno"/autosave.session-*.state | wc -l)" -eq 1
grep -Fq 'this session autosaves to autosave.session-' "$scratch/own-2.log"
grep -Fq 'they stay in autosave.session-' "$scratch/own-2.log"
(sleep 1; printf '\r++'; sleep 1; printf '\023'; sleep 1; printf 'q'; sleep 1; printf 'q') |
    session "$scratch/own-3.log"
test "$(cksum <"$shared")" = "$first"
grep -Fq 'press Ctrl+S again to replace it' "$scratch/own-3.log"
test "$(ls "$own/kilix-techno"/autosave.session-*.state | wc -l)" -eq 2
(sleep 1; printf '\r+'; sleep 1; printf '\023'; sleep 1; printf '\023'; sleep 1; printf 'q') |
    session "$scratch/own-4.log"
test "$(cksum <"$shared")" != "$first"
grep -Fq 'replacing the previous one' "$scratch/own-4.log"
test "$(ls "$own/kilix-techno"/autosave.session-*.state | wc -l)" -eq 2
XDG_DATA_HOME=$own "$binary" --headless | grep -q '^project=autosave$'

echo 'test-terminal: OK'
