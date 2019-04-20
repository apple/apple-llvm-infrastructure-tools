#!/bin/bash

error() {
    printf "error: %s\n" "$*" >&2
    exit 1
}

SELF="$0"
SUBCMD="$1"
[ -n "$SUBCMD" ] || error "missing subcommand"
shift

CONSTRUCTED="$SELF"-"$SUBCMD"
which "$CONSTRUCTED" >/dev/null || error "unknown subcommand '$SUBCMD'"

exec "$CONSTRUCTED" "$@"
