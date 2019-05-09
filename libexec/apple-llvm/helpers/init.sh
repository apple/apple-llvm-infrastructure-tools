# vim: ft=sh

[ "${TRACE_GIT_SH_SETUP:-0}" = 0 ] || set -o xtrace
. "$(git --exec-path)/git-sh-setup"
[ "${TRACE_GIT_SH_SETUP:-0}" = 0 ] || set +o xtrace
[ "${TRACE:-0}" = 0 ] || set -o xtrace

COMMAND="$(basename "$0")"
COMMAND_DIR="$(dirname "$0")"
APPLE_LLVM_HELPERS_PATH="$(dirname "$BASH_SOURCE")"

ZERO_SHA1=0000000000000000000000000000000000000000

is_function() { [ "$(type -t "$1" 2>/dev/null)" = function ]; }
helper() {
    is_function "$1" && return 0
    local path="$APPLE_LLVM_HELPERS_PATH"/"$1".sh
    . "$path" || die "internal: could not include '$path'"
}
helper canonicalize_path
APPLE_LLVM_HELPERS_PATH="$(canonicalize_path "$APPLE_LLVM_HELPERS_PATH")"
APPLE_LLVM_LIBEXEC_DIR="$(dirname "$APPLE_LLVM_HELPERS_PATH")"

awk_helper() {
    local name="$1"
    shift
    awk -f "$APPLE_LLVM_HELPERS_PATH"/"$name".awk "$@";
}

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
DRY_RUN=0
dry_run() { [ ! "${DRY_RUN:-0}" = 0 ]; }
run() {
    # Support hiding errors so that clients don't hide the verbose-mode
    # logging.
    local hide_errors=0
    local skip=0 space=" "
    if [ "$1" = --hide-errors ]; then
        hide_errors=1
        shift
    elif [ "$1" = --dry ]; then
        skip="$DRY_RUN"
        # TODO: add a test that --dry only uses an extra '#' when DRY_RUN.
        dry_run && space="#"
        shift
    fi

    if verbose; then
        printf "#%s%s" "$space" "${SHOWPIDS:+[$$] }"
        local sep= arg=
        for arg in "$@"; do
            printf "%s%q" "$sep" "$arg"
            sep=" "
        done
        printf "\n"
    fi >&2
    [ "${skip:-0}" -eq 0 ] || return 0

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
    [ $exit -eq 0 ] || exit 1
    return 1
}

usage_error() {
    local exit=1
    if [ "$1" = "--no-exit" ]; then
        shift
        exit=0
    fi

    error --no-exit "$@"
    if is_function usage; then
        printf "\n"
        usage
    fi >&2
    [ $exit -eq 0 ] || exit 1
    return 1
}

log() {
    printf "%s\n" "$*" >&2
}

parse_cmdline_option() {
    local param="$1"
    local var="$2"
    local spell="$3"
    local next="$4"
    local spell_noarg="${spell%%=*}"

    # Sanity check: param is (or is a glob for) spell.
    local leftover="${spell_noarg#$param}"
    [ -z "$leftover" ] ||
        error "internal: expected '$spell' to be '$param[=*]'"
    local value ret
    if [ "$spell" = "$spell_noarg" ]; then
        value="$next"
        ret=2
    else
        value="${spell#*=}"
        ret=1
    fi
    eval "$var=\"\$value\""
    return $ret
}
