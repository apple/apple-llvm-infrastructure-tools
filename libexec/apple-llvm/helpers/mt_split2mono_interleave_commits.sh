# vim: ft=sh

helper mt_split2mono_translate_commit
helper bisect

mt_split2mono_check_skips() {
    for s in "$@"; do
        mt_split2mono "$s" || exit 1
    done
}

mt_split2mono_check_refdirs() {
    #error "mt_split2mono_check_refdirs not implemented"
    true
}

mt_split2mono_list_new_split_commits() {
    local branch="$1"
    shift
    local rd r d not format
    for rd in "$@"; do
        r="${rd%:*}"
        d="${rd##*:}"
        format="%H %ct %T $d %P<%an<%ae<%ad<%cn<%ce<%cd"
        not="$(git show-ref "$d"/mt-split | awk '{print $1}')"
        run git log --date=raw $r --not $not --format="$format" --reverse |
            cat -n
        run git log --date=raw $r --not $not --format="$format" --first-parent |
            cat -n
    done |
    sort --stable    -k 2,2 | # %H
    uniq -c -f 1            | # skip cat -n, prepend count
    sort          -n -k 2,2 | # cat -n
    sort --stable    -k 6,6 | # $d
    sort --stable -n -k 4,4   # %ct
}

mt_split2mono_is_new_commit() { ! mt_split2mono "$@" >/dev/null 2>&1; }
mt_split2mono_interleave_commits() {
    local branch="$1"
    local repeat="$2"
    local refdirs="$3"
    local d rd ds ref
    for rd in "$all_ds"; do
        d="${rd##*:}"
        ref=refs/heads/mt/$branch/$d/mt-split
        run git rev-parse --verify $ref^{commit} >/dev/null 2>&1 ||
            continue

        # Add d to ds if it has already been referenced.
        ds="$ds${ds:+ }$d"
    done
    local count order commit ct tree d rest
    local is_branch_commit parents ad an ae cd cn ce override
    local i=0 n=0 head=
    local prev=$(run git rev-parse --verify refs/heads/$branch^{commit} \
        2>/dev/null)
    while read count order commit ct tree d rest; do
        mt_split2mono_is_new_commit "$commit" ||
            continue

        parents="${rest%%<*}"; rest="${rest#*<}"
        an="${rest%%<*}"     ; rest="${rest#*<}"
        ae="${rest%%<*}"     ; rest="${rest#*<}"
        ad="${rest%%<*}"     ; rest="${rest#*<}"
        cn="${rest%%<*}"     ; rest="${rest#*<}"
        ce="${rest%%<*}"     ; rest="${rest#*<}"
        cd="${rest%%<*}"     ; rest="${rest#*<}"

        if [ "$count" = 2 -a -n "$prev" ]; then
            override="--override-parent $prev"
        else
            override=
        fi

        head=$( \
            GIT_AUTHOR_NAME="$an"  GIT_COMMITTER_NAME="$cn"  \
            GIT_AUTHOR_DATE="$ad"  GIT_COMMITTER_DATE="$cd"  \
            GIT_AUTHOR_EMAIL="$ae" GIT_COMMITTER_EMAIL="$ce" \
            run mt_split2mono_translate_commit               \
            $override $commit $tree "$d" "$parents" "$ds") ||
            error "could not translate $commit for $d"

        # Add d to ds if it's the first it has been updated.
        ref=refs/heads/mt/$branch/$d/mt-split
        git rev-parse --verify $ref^{commit} >/dev/null 2>&1 ||
            ds="$ds${ds:+ }$d"

        # Update refs.
        if [ "$count" = 2 ]; then
            run git update-ref refs/heads/$branch $head "$prev" ||
                error "could not update $branch to $head"
            prev=$head
        fi
        git update-ref $ref $commit ||
            error "could not update $ref to $commit"

        i=$(( $i + 1 ))
        [ $i -eq 50 ] || continue

        # Give a progress update.
        n=$(( $n + $i ))
        i=0
        printf "    interleaved %8d commits\n" $n
    done

    # Give a final count.
    n=$(( $n + $i ))
    [ $i -eq 0 ] ||
        printf "    interleaved %8d commits\n" $n
}
