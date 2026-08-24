#!/bin/sh
set -eu

binary=${1:?kilix-techno binary required}
scratch=$(mktemp -d /tmp/kilix-techno-cli-XXXXXX)
cleanup() {
    rm -rf -- "$scratch"
}
trap cleanup EXIT HUP INT TERM

project=$scratch/session.ktech
wave=$scratch/session.wav

"$binary" --version | grep -q '^kilix-techno 0\.1\.0$'
"$binary" --new --headless --seed 77 --save "$project" |
    grep -q '^project=new$'
test -s "$project"
"$binary" --open "$project" --headless | grep -q '^tempo=126$'
ln -s "$project" "$scratch/link.ktech"
if "$binary" --open "$scratch/link.ktech" --headless >/dev/null 2>&1; then
    echo 'test-cli: symlink project input accepted' >&2
    exit 1
fi
if "$binary" --new --headless --save "$project" >/dev/null 2>&1; then
    echo 'test-cli: unsafe overwrite succeeded' >&2
    exit 1
fi
"$binary" --export "$project" "$wave" --bars 1 --sample-rate 8000 |
    grep -q '^exported '
test "$(dd if="$wave" bs=1 count=4 2>/dev/null)" = RIFF
if "$binary" --export "$project" "$wave" >/dev/null 2>&1; then
    echo 'test-cli: unsafe export overwrite succeeded' >&2
    exit 1
fi
if "$binary" --bars 0 >/dev/null 2>&1; then
    echo 'test-cli: invalid bar count accepted' >&2
    exit 1
fi

echo 'test-cli: OK'
