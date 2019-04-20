#!/bin/bash

error() {
    printf "error: %s\n" "$*" >&2
    exit 1
}

DIR="$(dirname "$0")"
SELF="$(basename "$0")"
SUBCMD="$1"
shift
D="$DIR"/"$SELF".d
[ -d "$D" ] || error "no subcommands in '$D'"
if [ -n "$SUBCMD" ]; then
    CONSTRUCTED="$D"/"$SELF"-"$SUBCMD"
    which "$CONSTRUCTED" >/dev/null || error "unknown subcommand '$SUBCMD'"
    exec "$CONSTRUCTED" "$@"
fi

# Figure out name.
NAME=
for n in ${SELF//-/ }; do
    if [ "$NAME" = "git apple" -a "$n" = llvm ]; then
        NAME="$NAME-$n"
        continue
    fi
    NAME="$NAME${NAME:+ }$n"
done

# List subcommands
printf "%s:\n" "$NAME"
(cd "$D" && ls -d "$SELF"-*) |
grep -v '\.d$' |
sed -e s/^$SELF-/'    '/
