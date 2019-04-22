# vim: ft=sh

bisect() {
    local foo="$1" high="$2" not="$3"

    is_function "$foo" || error "'$foo' is not a function"

    # As an optimization, look at close-by commits first.  This avoids doing a
    # bisect of 300k+ commits for incremental updates.
    if [ -z "$not" ]; then
        local x="$(run --hide-errors git rev-list --reverse "$high" -1000 |
        head -n 1)"
        [ -n "$x" ] && $foo "$x" && not="$x"
    fi

    local mid= prevmid=
    mid=$(run --hide-errors git rev-list --bisect $high --not $not)
    while [ ! "$mid" = "$prevmid" ]; do
        if [ -n "$mid" ] && $foo "$mid"; then
            not="$not${not:+ }$mid"
        else
            high=$mid
        fi
        prevmid=$mid
        mid=$(run --hide-errors git rev-list --bisect $high --not $not)
    done
    printf "%s\n" "$not"
}
