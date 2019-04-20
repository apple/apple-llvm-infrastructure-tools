#!/bin/bash

forward() {
    DIR="$(dirname "$0")"
    local subcmd="$1"
    shift
    local d="$COMMAND_DIR"/"$COMMAND".d
    [ -d "$d" ] || error "no subcommands in '$d'"
    if [ -n "$subcmd" ]; then
        CONSTRUCTED="$d"/"$COMMAND"-"$subcmd"
        which "$CONSTRUCTED" >/dev/null || error "unknown subcommand '$subcmd'"
        exec "$CONSTRUCTED" "$@"
    fi

    # Figure out name.
    local name part
    for part in ${COMMAND//-/ }; do
        if [ "$name" = "git apple" -a "$part" = llvm ]; then
            name="$name-$part"
            continue
        fi
        name="$name${name:+ }$part"
    done

    # List subcommands
    printf "%s:\n" "$name"
    (cd "$d" && ls -d "$COMMAND"-*) |
    grep -v '\.d$' |
    sed -e s/^$COMMAND-/'    '/
    return 0
}
