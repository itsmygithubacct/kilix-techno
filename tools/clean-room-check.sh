#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

fail()
{
    printf 'clean-room: %s\n' "$*" >&2
    exit 1
}

sha256()
{
    sha256sum "$1" | awk '{print $1}'
}

valid_hex()
{
    value=$1
    length=$2
    test "${#value}" -eq "$length" || return 1
    case "$value" in
        *[!0-9a-f]*) return 1 ;;
    esac
}

# Include untracked, non-ignored files so a pre-staging check cannot silently
# miss a newly created application artifact.
bad=$(git ls-files --cached --others --exclude-standard | awk '
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

dependency_manifest=packaging/dependency-provenance.tsv
test -f "$dependency_manifest" || fail "missing $dependency_manifest"
tab=$(printf '\t')
dependency_names=
dependency_count=0
while IFS="$tab" read -r name revision license_path license_digest extra; do
    case "$name" in
        ''|'#'*) continue ;;
    esac
    test -z "${extra:-}" || fail "extra dependency manifest fields for $name"
    case "$name" in
        *[!a-zA-Z0-9._-]*) fail "unsafe dependency name: $name" ;;
    esac
    case "$license_path" in
        ''|/*|*../*|*/..|..) fail "unsafe license path for $name" ;;
    esac
    valid_hex "$revision" 40 || fail "invalid revision for $name"
    valid_hex "$license_digest" 64 || fail "invalid license digest for $name"
    if printf '%s\n' "$dependency_names" | grep -Fqx -- "$name"; then
        fail "duplicate dependency manifest entry: $name"
    fi
    dependency_names="${dependency_names}
${name}"
    dependency_count=$((dependency_count + 1))

    path="third_party/$name"
    test -d "$path" || fail "missing dependency: $name"
    actual=$(git -C "$path" rev-parse --verify HEAD) ||
        fail "cannot read dependency revision: $name"
    test "$actual" = "$revision" || fail "wrong $name pin: $actual"
    test -z "$(git -C "$path" status --porcelain=v1)" ||
        fail "dirty dependency: $name"
    test -f "$path/$license_path" || fail "$name has no $license_path"
    actual_digest=$(sha256 "$path/$license_path")
    test "$actual_digest" = "$license_digest" ||
        fail "$name license digest is $actual_digest, expected $license_digest"
done < "$dependency_manifest"

submodule_count=$(git config -f .gitmodules --get-regexp \
    '^submodule\..*\.path$' | wc -l | tr -d ' ')
test "$dependency_count" -eq "$submodule_count" ||
    fail "dependency manifest has $dependency_count entries for $submodule_count submodules"
git config -f .gitmodules --get-regexp '^submodule\..*\.path$' |
while IFS=' ' read -r key submodule_path extra; do
    test -z "${extra:-}" || fail "unsafe submodule path record: $key"
    case "$submodule_path" in
        third_party/*) name=${submodule_path#third_party/} ;;
        *) fail "submodule outside third_party: $submodule_path" ;;
    esac
    case "$name" in
        ''|*/*) fail "nested or empty direct submodule: $submodule_path" ;;
    esac
    printf '%s\n' "$dependency_names" | grep -Fqx -- "$name" ||
        fail "submodule missing from dependency manifest: $name"
done

if [ -x /home/pleb/bin/hygiene-scan ]; then
    /home/pleb/bin/hygiene-scan --recurse
fi
printf '%s\n' 'clean-room: passed'
