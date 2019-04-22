# vim: ft=sh

# Support for canonicalizing paths when readlink -f is not available.
canonicalize_path() {
    local input="$1"
    local maxdepth=100
    local depth=0

    [ / = "${input:0:1}" ] || input="$PWD/$input"

    local output= current= trail="${input:1}"
    while [ -n "$trail" ]; do
        # Ignore leading / in trail.
        while [ / = "${trail:0:1}" ]; do
            trail="${trail:1}"
        done

        # Pull off the first element.
        if [ "$trail" = "${trail#*/}" ]; then
            # Also the last element...
            current="$trail"
            trail=
        else
            current="${trail%%/*}"
            trail="${trail#*/}"
        fi

        # Dereference symlinks.
        if symlink="$(readlink "$output/$current")"; then
            # Check max depth.
            [ $(( ++depth )) -gt $maxdepth ] &&
                error "$input: exceeded canonicalize_path() max symlink depth" \
                "of $maxdepth"

            # Follow symlink.
            if [ / = "${symlink:0:1}" ]; then
                output=
                trail="${symlink:1}/$trail"
            else
                trail="$symlink/$trail"
            fi
            continue
        fi

        # Handle '.' and '..'.
        case "$current" in
            "" | .)
                true
                ;;
            ..)
                [ -n "$output" ] ||
                    error "/ has no parent directory"
                output="${output%/*}"
                ;;
            *)
                output="$output/$current"
                ;;
        esac
    done

    echo "${output:-/}"
}

relative_canonicalize_path() {
    local absolute="$(canonicalize_path "$@")"
    local prefix="$PWD/"
    local len="${#prefix}"
    if [ "${absolute:0:$len}" = "$prefix" ]; then
        echo "${absolute:$len}"
    else
        echo "$absolute"
    fi
}
