# vim: ft=sh

. "$(git --exec-path)/git-sh-setup"
COMMAND="$(basename "$0")"

mt_register_paths_to_clean_up() {
    if [ -z "$MT_CLEANUP_ONCE" ]; then
        MT_CLEANUP_ONCE=1
        MT_CLEANUP_FILES=()
        trap mt_cleanup_paths \
            EXIT SIGHUP SIGINT SIGQUIT SIGILL SIGTRAP SIGABRT SIGEMT SIGFPE \
            SIGKILL SIGBUS SIGSEGV SIGSYS SIGPIPE SIGALRM SIGTERM SIGXCPU \
            SIGXFSZ SIGVTALRM SIGPROF SIGUSR1 SIGUSR2
    fi
    MT_CLEANUP_FILES=( "${MT_CLEANUP_FILES[@]}" "$@" )
}

mt_cleanup_paths()  {
    run rm -rf "${MT_CLEANUP_FILES[@]}"
}

NEXTFD=3
getnextfd() {
    echo $NEXTFD
    NEXTFD=$(( $NEXTFD + 1 ))
}

is_function() {
    [ "$(type -t "$1" 2>/dev/null)" = function ]
}

helper() {
    is_function "$1" && return 0
    local path="$(dirname "$0")"/mt-helpers/"$1".sh
    . "$path" || error "internal: could not include '$path'"
}

run() {
    [ "${VERBOSE:-0}" = 0 ] || echo "# $*" >&2
    "$@"
}
error() {
    printf "error: %s\n" "$*" >&2
    if is_function usage; then
        printf "\n"
        usage
    fi >&2
    exit 1
}

log() {
    printf "%s\n" "$*" >&2
}
