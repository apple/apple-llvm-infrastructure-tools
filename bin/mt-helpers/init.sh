# vim: ft=sh

is_function() {
    [ "$(type "$1" 2>/dev/null)" = function ]
}

helper() {
    is_function "$1" ||
        . "$(dirname "$0")"/mt-helpers/"$1".sh;
}

run() {
    [ "${VERBOSE:-0}" = 0 ] || echo "# $*" >&2
    "$@"
}
error() {
    if is_function usage; then
        usage >&2
    fi
    printf "error: %s\n" "$*" >&2
    exit 1
}

log() {
    printf "%s\n" "$*" >&2
}
