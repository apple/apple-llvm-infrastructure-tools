# vim: ft=sh

helper mt_split2mono
helper mt_llvm_svn2git
helper bisect

mt_split2mono_list_new_split_commits() {
    local branch="$1"
    shift
    local rd r d head tail format
    for rd in "$@"; do
        r="${rd%:*}"
        d="${rd##*:}"

        local tailref="refs/heads/${branch#refs/heads/}/$d/mt-split"
        local headref="refs/heads/${r#refs/heads/}"
        tail="$(run --hide-errors git rev-parse --verify $headref^{commit})"
        head="$(run --hide-errors git rev-parse --verify $tailref^{commit})" ||
            error "invalid ref '$headref' from '$rd'"
        [ "$tail" = "$head" ] && continue

        printf "start %s\n" "$d"
        run git log $head --not $tail --format="%H %ct" --first-parent
        printf "all\n"
        run git log $head --not $tail --format="%H %T %P"
        printf "done\n"
    done
}

mt_split2mono_interleave_commits() {
    local branch="$1"
    local repeat="$2"
    local refdirs="$3"

    ref=refs/heads/${branch#refs/heads/}
    local head
    head=$(run --hide-errors git rev-parse --verify $ref^{commit}) ||
        head=0000000000000000000000000000000000000000

    local d rd ref sha1
    local -a sha1dirs
    for rd in "$all_ds"; do
        d="${rd##*:}"
        ref=refs/heads/mt/${branch#refs/heads/}/$d/mt-split
        sha1=$(run --hide-errors git rev-parse --verify $ref^{commit}) ||
            sha1=0000000000000000000000000000000000000000
        sha1dirs=( "${sha1dirs[@]}" "$sha1:$d" )
    done

    local svn2git split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build split2mono"
    run "$split2mono" "$MT_DB_SPLIT2MONO_DB" "$MT_DB_SVN2GIT_DB" \
        "$head" "${sha1dirs[@]}"
}
