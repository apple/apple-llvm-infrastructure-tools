# vim: set ft=bash
PROGRAM="$(basename "$0")"
EXECDIR="$(dirname "$0")"
[ "${EXECDIR:0:1}" = / ] || EXECDIR="$PWD/$EXECDIR"

run() {
    local arg
    {
        printf "#"
        for arg in "$@"; do
            printf " %q" "$arg"
        done
        printf "\n"
    } >&2
    "$@"
}
check() { run "$@" || exit $?; }
git() { command git -C "$REPO" "$@"; }

error() {
    printf "error: %s\n" "$*" >&2
    exit 1
}

execdir() {
    local func=run
    if [ "$1" = --check ]; then
        func=check
        shift
    fi
    local cmd="$1"
    shift
    $func "$EXECDIR"/"$cmd" "$@"
}
