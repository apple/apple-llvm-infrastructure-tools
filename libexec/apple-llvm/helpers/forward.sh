#!/bin/bash

forward() {
    local subcmd="$1"
    shift

    usage() { echo "usage: $(print_cmdname) <sub-command>"; }

    if [ -z "$subcmd" ]; then
        # List subcommands
        printf "%s:\n" "$(print_cmdname)"
        printf "    %s\n" $(forward_list)
        return 0
    fi

    # Make sure it's a legal subcommand.
    local constructed="$APPLE_LLVM_LIBEXEC_DIR"/"$COMMAND"-"$subcmd"
    for sub in $(forward_list); do
        [ "$sub" = "$subcmd" ] || continue
        which "$constructed" >/dev/null ||
            error "broken subcommand '$subcmd'"
        exec "$constructed" "$@"
    done

    error "unknown subcommand '$subcmd'"
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
            [ "${cmd:0:$prev_len}" = "$prev_prefix" ] && continue
            printf "%s\n" ${cmd:$len}
            prev=$cmd
        done
    }
}
