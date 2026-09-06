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
# The owner lock is released when the owner exits: the mark stays, empty and
# lockable.
test ! -s "$own/kilix-techno/autosave.owner"
flock -n "$own/kilix-techno/autosave.owner" true

# A claim is a pending decision that an edit cancels: toggle, Ctrl+S (asks),
# toggle again, Ctrl+S (asks again, does not replace), quit.
before5=$(cksum <"$shared")
(sleep 1; printf '\r+++'; sleep 1; printf '\023'; sleep 1; printf '\r'; sleep 1; printf '\023'; sleep 1; printf 'q'; sleep 1; printf 'q') |
    session "$scratch/own-5.log"
test "$(cksum <"$shared")" = "$before5"
test "$(grep -c 'press Ctrl+S again to replace it' "$scratch/own-5.log")" -ge 2
! grep -Fq 'replacing the previous one' "$scratch/own-5.log"

# A guest --open session autosaves to a session slot, then saves its file and
# exits with nothing unsaved: the slot is removed on the way out.
before=$(ls "$own/kilix-techno"/autosave.session-*.state | wc -l)
"$binary" --new --headless --save "$own/song.ktech" >/dev/null
(sleep 1; printf '\r'; sleep 3; printf '\023'; sleep 1; printf 'q') |
    XDG_DATA_HOME=$own timeout 15s script -qec \
        "$binary --open $own/song.ktech --cell --no-audio" /dev/null >"$scratch/own-6.log" 2>&1
grep -Fq 'Project saved' "$scratch/own-6.log"
test "$(ls "$own/kilix-techno"/autosave.session-*.state | wc -l)" -eq "$before"

# Ownership is a claim, not a belief. With the shared slot absent, a session
# still cannot own it while another running process holds the owner lock; a
# mark nobody holds, whatever it contains, is free.
mark=$scratch/mark
mkdir -m 700 -p "$mark/kilix-techno"
: >"$mark/kilix-techno/autosave.owner"
# The holder execs into sleep so that killing it releases the lock; flock(1)
# with a child would leave the child holding it.
sh -c 'flock -n 9 || exit 1; exec sleep 30' 9>"$mark/kilix-techno/autosave.owner" &
holder=$!
sleep 0.5
(sleep 1; printf '\r'; sleep 1; printf '\023'; sleep 1; printf 'q'; sleep 1; printf 'q') |
    XDG_DATA_HOME=$mark timeout 15s script -qec \
        "$binary --new --cell --no-audio" /dev/null >"$scratch/mark-1.log" 2>&1
kill "$holder" 2>/dev/null || true
wait "$holder" 2>/dev/null || true
test ! -e "$mark/kilix-techno/autosave.state"
grep -Fq 'Another running session owns the autosave' "$scratch/mark-1.log"
printf '%s\n' garbage >"$mark/kilix-techno/autosave.owner"
(sleep 1; printf '\r'; sleep 1; printf '\023'; sleep 1; printf 'q') |
    XDG_DATA_HOME=$mark timeout 15s script -qec \
        "$binary --new --cell --no-audio" /dev/null >"$scratch/mark-2.log" 2>&1
test -f "$mark/kilix-techno/autosave.state"
test ! -s "$mark/kilix-techno/autosave.owner"
flock -n "$mark/kilix-techno/autosave.owner" true

# A guest's second Ctrl+S takes the lock before replacing the shared slot, and
# is refused while another running session holds it.
sh -c 'flock -n 9 || exit 1; exec sleep 30' 9>"$own/kilix-techno/autosave.owner" &
holder=$!
sleep 0.5
before7=$(cksum <"$shared")
(sleep 1; printf '\r++++'; sleep 1; printf '\023'; sleep 1; printf '\023'; sleep 1; printf 'q'; sleep 1; printf 'q') |
    session "$scratch/own-7.log"
kill "$holder" 2>/dev/null || true
wait "$holder" 2>/dev/null || true
test "$(cksum <"$shared")" = "$before7"
grep -Fq 'Another running session owns the autosave; nothing replaced' "$scratch/own-7.log"

# An --open session never owns the shared slot, even when it is absent: its
# unsaved edit goes to a session slot and the shared slot is not created.
opened=$scratch/opened
mkdir -m 700 -p "$opened"
"$binary" --new --headless --save "$opened/song.ktech" >/dev/null
(sleep 1; printf '\r'; sleep 3; printf 'q'; sleep 1; printf 'q') |
    XDG_DATA_HOME=$opened timeout 15s script -qec \
        "$binary --open $opened/song.ktech --cell --no-audio" /dev/null >"$scratch/opened.log" 2>&1
test ! -e "$opened/kilix-techno/autosave.state"
test "$(ls "$opened/kilix-techno"/autosave.session-*.state | wc -l)" -eq 1

echo 'test-terminal: OK'
