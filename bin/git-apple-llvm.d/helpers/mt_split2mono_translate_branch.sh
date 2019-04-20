mt_split2mono_translate_branch() {
    local branch
    local pos=0
    local -a upstreams
    local -a skips
    local -a refdirs
    while [ $# -gt 0 ]; do
        case "$1" in
            --help)
                usage
                exit 0
                ;;
            --upstream)
                upstreams=( "${upstreams[@]}" "$1" )
                ;;
            --skip)
                skips=( "${skips[@]}" "$1" )
                ;;
            -*)
                error "unknown option '$1'"
                ;;
            *)
                if [ $pos -eq 0 ]; then
                    branch="$1"
                else
                    refdirs=( "${refdirs[@]}" "$1" )
                fi
                pos=1
                ;;
        esac
        shift
    done
    [ -n "$branch" ] || error "missing <branch>"
    [ ${#refdirs[@]} -gt 0 ] || error "missing <ref>:<dir>"
    mt_split2mono_check_upstreams "${upstreams[@]}"
    mt_split2mono_check_refdirs "${refdirs[@]}"
    mt_split2mono_check_skips "${skips[@]}"

    mt_split2mono_list_new_split_commits "${refdirs[@]}" |
    mt_split2mono_interleave_commits ||
        error "failure interleaving commits"
}

mt_split2mono_check_skips() {
    for s in "$@"; do
        mt_split2mono "$s" || exit 1
    done
}

mt_split2mono_check_upstreams() {
    error "mt_split2mono_check_upstreams not implemented"
}

mt_split2mono_check_refdirs() {
    error "mt_split2mono_check_refdirs not implemented"
}

mt_split2mono_list_new_split_commits() {
    # FIXME: Printing all of them and sorting is really slow, when we could
    # just take commits one at a time from n FIFOs, choosing the best of the
    # bunch.  The problem is that bash could run out of file descriptors.
    local rd r d head fifo
    for rd in "$@"; do
        r="${rd%:*}"
        d="${rd##*:}"
        head=$(git rev-parse refs/heads/mt/$branch/$d/mt-split 2>/dev/null)
        fifo="$fifos"/$d
        mkfifo "$fifo" || exit 1
        run git log --format="${d:--} %ct %H" \
            --first-parent --reverse $r --not $skips $head
    done |
    sort --stable -n -k 2,2
}

mt_split2mono_interleave_commits() {
    local refdirs="$1"
    local d rd ds ref
    for rd in "$all_ds"; do
        d="${rd##*:}"
        ref=refs/heads/mt/$branch/$d/mt-split
        run git rev-parse $ref^{commit} >/dev/null 2>&1 ||
            continue
        ds="$ds${ds:+ }$d"
    done
    local ct next head i=0 n=0
    while read d ct next; do
        head=$(mt_split2mono_translate_commit $d $next $head $ds) ||
            error "could not translate $next for $d"

        ref=refs/heads/mt/$branch/$d/mt-split
        git rev-parse $ref^{commit} >/dev/null 2>&1 ||
            ds="$ds${ds:+ }$d"
        {
            printf "update %s %s\n" $ref $next
            printf "update refs/heads/%s %s\n" $branch $head
        } |
        git update-ref --stdin || exit 1

        i=$(( $i + 1 ))
        [ $i -eq 5000 ] || continue
        n=$(( $n + $i ))
        i=0
        printf "    interleaved %8d commits\n" $n
    done
}
