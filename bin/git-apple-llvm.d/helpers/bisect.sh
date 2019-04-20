# vim: ft=sh

bisect() {
    local foo="$1" high="$2" low="$3"

    # As an optimization, look at close-by commits first.  This avoids doing a
    # bisect of 300k+ commits for incremental updates.
    if [ -z "$low" ]; then
        local x="$(run --hide-errors git rev-list --reverse "$high" -1000 |
        head -n 1)"
        [ -n "$x" ] && eval "$foo \"\$x\"" && low="$x"
    fi

    local mid= prevmid=
    mid=$(run --hide-errors git rev-list --bisect $high --not $low)
    while [ ! "$mid" = "$prevmid" ]; do
        if [ -n "$mid" ] && eval "$foo \"\$mid\""; then
            low=$mid
        else
            high=$mid
        fi
        prevmid=$mid
        mid=$(run --hide-errors git rev-list --bisect $high --not $low)
    done
    echo "$low"
}
