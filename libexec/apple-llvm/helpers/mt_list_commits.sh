# helpers/mt_list_commits.sh

mt_list_commits() {
    local d="$1"
    shift
    run printf "start %s\n" "$d" &&
    run git log --format="%H %ct" --first-parent "$@" &&
    run printf "all\n" &&
    run git log --date-order --reverse --date=raw \
        --format="%m%H %T %P%x00%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B%x00" \
        --boundary "$@" &&
    run printf "done\n"
}

mt_list_modified_top_level_paths() {
    local src="$1"
    local dst="$2"
    local included="$3"
    local ignored="$4"

    # Find the subset of those paths that were modified.  Using 'diff' here (or
    # an optimized equivalent) instead of 'log --format=' is an
    # under-approximation, but it will only be wrong if:
    #
    #   - all changes to a particular file/path in the monorepo root made in
    #     this range have been reverted within the same range; or
    #   - all changes to a particular sub-project made in this range have been
    #     reverted.
    #
    # These situations are unlikely and unimportant enough in the context of
    # repeat that we probably don't care about missing those.
    #
    # The algorithm here is:
    #
    #   1. List the trees of dst and src.
    #   2. Sort them.
    #   3. Delete any rows that are repeated.  This will leave behind entries
    #      in either 'dst' or 'src' that aren't matched in the other.
    #   4. Simplify to just the name.
    #   5. Unique the names (otherwise changed paths show up twice).
    #   6. Skip top-level paths that should be ignored.

    # Set up the logic for ignoring/including top-level paths.
    local begin= decl=
    begin="BEGIN { allow_undeclared = 0;"
    for decl in $included; do
        if [ ! "$decl" = "-" ]; then
            # Add this to the include list.
            begin="$begin include[\"$decl\"] = 1;"
            continue
        fi

        # Pull in undeclared paths from the monorepo root.
        begin="$begin allow_undeclared = 1;"
        for decl in $ignored; do
            begin="$begin ignore[\"$decl\"] = 1;"
        done
    done
    begin="$begin }"

    # Do the work.
    {
        git ls-tree "$dst"
        [ "${src:-$ZERO_SHA1}" = $ZERO_SHA1 ] || git ls-tree "$src"
        # Format: <mode> SP <type> SP <sha1> TAB <name>
    } | sort | uniq -u | awk '{ print $4 }' | uniq |
        awk "$begin"'
            include[$0]       { print; next }
            !allow_undeclared { next }
            ignore[$0]        { next }
                              { print }'
}

mt_find_last_sha1_for_changes() {
    local src="$1"
    local dst="$2"
    local ignored="$3"
    local included="$4"

    local begin= decl= extra_repeated=

    # Ignore everything in the set ignored - included.
    begin="BEGIN {"
    for decl in $ignored; do
        begin="$begin ignore[\"$decl\"] = 1;"
    done
    for decl in $included; do
        begin="$begin ignore[\"$decl\"] = 0;"
    done
    begin="$begin }"

    # Find the subset of those paths that were modified.  Using 'diff' here
    # instead of 'log --format=' is an under-approximation, but it will only be
    # wrong if:
    #
    #   - all changes to a particular file/path in the monorepo root made in this
    #     range have been reverted (effectively) within the same range; or
    #   - all changes to a particular subproject made in this range have been
    #     reverted (effectively).
    #
    # This combination is unlikely enough that we probably don't care about
    # missing those.
    run git diff --name-only "$src".."$dst" | sed -e 's,/.*,,' | sort -u |
        run awk "$begin"' ignore[$0] { next } { print }'
}
