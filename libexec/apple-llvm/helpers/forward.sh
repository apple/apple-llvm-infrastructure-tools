#!/bin/bash

forward() {
    local subcmd="$1"
    shift

    usage() {
        printf "usage: %s <subcommand>\n" "$(print_cmdname)"
        printf "\n"
        printf "    subcommands:\n"
        printf "        %s\n" $(forward_list)
        return 0
    }

    if [ -z "$subcmd" ]; then
        usage
        return 0
    fi

    # Make sure it's a legal subcommand.
    local constructed="$APPLE_LLVM_LIBEXEC_DIR"/"$COMMAND"-"$subcmd"
    for sub in $(forward_list); do
        [ "$sub" = "$subcmd" ] || continue
        which "$constructed" >/dev/null ||
            usage_error "broken subcommand '$subcmd'"
        exec "$constructed" "$@"
    done

    usage_error "unknown subcommand '$subcmd'"
}

forward_list() {
    {
        cd "$APPLE_LLVM_LIBEXEC_DIR" &&
            ls $COMMAND-*
    } |
    sort |
    {
        local prev_prefix prev_len prev
        local      prefix      len cmd 
        prefix=$COMMAND-
        len=${#prefix}
        while read cmd; do
            prev_prefix=$prev-
            prev_len=${#prev_prefix}
            if [ "${cmd:0:$prev_len}" = "$prev_prefix" ]; then
                case "$prev" in
                    # Catch false positives.
                    *-split2mono|*-svn2git) true ;;
                    *) continue ;;
                esac
            fi
            printf "%s\n" ${cmd:$len}
            prev=$cmd
        done
    }
}
