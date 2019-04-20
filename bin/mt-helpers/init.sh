# vim: ft=sh

. "$(git --exec-path)/git-sh-setup"
COMMAND="$(basename "$0")"
COMMAND_FROM_GIT="${COMMAND#git-}"

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

is_function() { [ "$(type -t "$1" 2>/dev/null)" = function ]; }
helper() {
    is_function "$1" && return 0
    local path="$(dirname "$0")"/mt-helpers/"$1".sh
    . "$path" || error "internal: could not include '$path'"
}

showpids() { [ ! "${SHOWPIDS:-0}" = 0 ]; }
verbose() { [ ! "${VERBOSE:-0}" = 0 ]; }
run() {
    # Support hiding errors so that clients don't hide the verbose-mode
    # logging.
    local hide_errors=0
    if [ "$1" = --hide-errors ]; then
        hide_errors=1
        shift
    fi

    verbose && echo "#${SHOWPIDS:+ [$$]} $*" >&2

    if [ $hide_errors -eq 1 ]; then
        "$@" 2>/dev/null
    else
        "$@"
    fi
}
error() {
    local exit=1
    if [ "$1" = "--no-exit" ]; then
        shift
        exit=0
    fi

    printf "error: %s\n" "$*" >&2
    if is_function usage; then
        printf "\n"
        usage
    fi >&2
    if [ $exit -eq 1 ]; then
        exit 1
    fi
    return 1
}

log() {
    printf "%s\n" "$*" >&2
}
