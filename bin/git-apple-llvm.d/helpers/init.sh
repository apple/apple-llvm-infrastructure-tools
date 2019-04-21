# vim: ft=sh

[ "${TRACE_GIT_SH_SETUP:-0}" = 0 ] || set -o xtrace
. "$(git --exec-path)/git-sh-setup"
[ "${TRACE_GIT_SH_SETUP:-0}" = 0 ] || set +o xtrace
[ "${TRACE:-0}" = 0 ] || set -o xtrace

COMMAND="$(basename "$0")"
COMMAND_DIR="$(dirname "$0")"
APPLE_LLVM_HELPERS_PATH="$(dirname "$BASH_SOURCE")"
APPLE_LLVM_BIN_DIR="$(dirname "$APPLE_LLVM_HELPERS_PATH")"/..

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
    local path="$APPLE_LLVM_HELPERS_PATH"/"$1".sh
    . "$path" || die "internal: could not include '$path'"
}

print_cmdname() {
    local name part
    for part in ${COMMAND//-/ }; do
        case "$name" in
            "")
                name="$part"
                ;;
            git|"git apple-llvm"|"git apple-llvm mt")
                name="$name $part"
                ;;
            "git apple-llvm am"|"git apple-llvm ci")
                name="$name $part"
                ;;
            *)
                name="$name-$part"
                ;;
        esac
    done
    printf "%s\n" "$name"
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

parse_cmdline_option() {
    local param="$1"
    local var="$2"
    shift 2
    [ "${1%%=*}" = "$param" ] ||
        error "internal: expected '$1' to be '$param[=*]'"
    local value ret
    if [ "$param" = "$1" ]; then
        value="$2"
        ret=2
    else
        value="${1#*=}"
        ret=1
    fi
    eval "$var=\"\$value\""
    return $ret
}
