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
                ;;
        esac
    done
    check_upstreams "${upstreams[@]}"
    check_skips "${skips[@]}"
}

mt_split2mono_check_skips() {
    for s in "$@"; do
        mt-split2mono "$s" || exit 1
    done
}

mt_split2mono_check_upstreams() {
    error "mt_split2mono_check_upstreams not implemented"
}


mt_split2mono_get_split_ref() {
    git rev-parse refs/mt/branch/split/$1/$2/$3^{commit} 2>/dev/null
}
mt_split2mono_set_split_ref() {
    git update-ref refs/mt/branch/split/$1/$2/$3 $4
}

# open fifos for each split dir
fifos="$(mktemp -d)"
dirs=()
for rd in ${refdirs[@]}; do
    r="${rd%:*}"
    d="${rd##*:}"
    dirs=( "${dirs[@]}" "$d" )
    next=$(get_split_ref $branch $d next 2>/dev/null)
    head=$(get_split_ref $branch $d head 2>/dev/null)
    fifo="$fifos"/$d
    mkfifo "$fifo" || exit 1
    eval "git rev-list --first-parent --reverse $r --not $skips ${next:-$head} >&$fifo" &
    [ -n "$next" ] && continue
    if [ -n "$head" ]; then
        read -u "$fifo" next
        [ -n "$next" ] && set_split_ref $branch $d next $next
        continue
    fi
    while read -u "$fifo" commit; do
        mt-lookup-commit $commit >/dev/null && continue
        set_split_ref $branch $d next $commit
        set_split_ref $branch $d tail $commit
        break
    done
done

select_next() {
    local next d date best bestd bestdate=$(( 0x7fffffffffffffff ))
    for d in ${dirs[@]}; do
        next=$(get_split_ref $branch $d next) || continue
        date=$(git log --format=format:%ct $next) || exit 1
        if [ $date -lt $bestdate ]; then
            [ -n "$bestd" ] && printf "%s " "$bestd"
            bestd="$d"
            best=$next
            bestdate=$date
        else
            printf "%s " "$d"
        fi
    done
    # Save the best for last.
    if [ -n "$best" ]; then
        printf "%s" $bestd
        return 0
    fi
    return 1
}

bhead=$(git rev-parse refs/heads/$branch^{commit} 2>/dev/null)
while true; do
    ds_best_last=$(select_next) || return 0
    other_ds="${ds_best_last##* }"
    nextd="${ds_best_last% *}"
    next=$(get_split_ref $branch $nextd next)
    bhead=$(mt-split2mono-translate-commit \
        $nextd $next $bhead ${bhead:+$other_ds}) ||
        exit 1

    # update everything
    head=$next
    read -u "$fifos"/$nextd new
    prefix=refs/mt/branch/split/$branch
    {
        printf "update %s/next/%s %s %s\n" $prefix $nextd $new $next
        printf "update %s/head/%s %s\n"    $prefix $nextd $head
        printf "update refs/heads/%s %s\n" $branch $bhead
    } |
    git update-ref --stdin || exit 1
done
