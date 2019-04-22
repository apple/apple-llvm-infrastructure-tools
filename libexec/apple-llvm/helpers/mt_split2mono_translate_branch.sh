# vim: ft=sh

helper mt_split2mono_translate_commit
helper bisect

mt_split2mono_translate_branch() {
    local branch
    local pos=0
    local repeat=
    local -a skips
    local -a refdirs
    local value=
    while [ $# -gt 0 ]; do
        case "$1" in
            --help)
                usage
                exit 0
                ;;
            --repeat|--repeat=*)
                parse_cmdline_option --repeat repeat
                shift $?
                ;;
            --skip|--skip=*)
                parse_cmdline_option --skip value
                shift $?
                skips=( "${skips[@]}" "$value" )
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
                shift
                ;;
        esac
    done
    [ -n "$branch" ] || error "missing <branch>"
    branch="${branch#refs/heads/}"

    [ ${#refdirs[@]} -gt 0 ] || error "missing <ref>:<dir>"
    mt_split2mono_check_refdirs "${refdirs[@]}"
    mt_split2mono_check_skips "${skips[@]}"

    [ -z "$repeat" ] || error "--repeat not implemented"

    mt_split2mono_list_new_split_commits "$branch" "${refdirs[@]}" |
    mt_split2mono_interleave_commits "$branch" "$repeat" "${refdirs[*]}" ||
        error "failure interleaving commits"
}

mt_split2mono_check_skips() {
    for s in "$@"; do
        mt_split2mono "$s" || exit 1
    done
}

mt_split2mono_check_refdirs() {
    #error "mt_split2mono_check_refdirs not implemented"
    true
}

mt_split2mono_is_new_commit() { mt_split2mono "$@" >/dev/null; }
mt_split2mono_list_new_split_commits() {
    local branch="$1"
    shift
    local rd r d head not
    for rd in "$@"; do
        r="${rd%:*}"
        d="${rd##*:}"
        head=$(git rev-parse --verify \
            refs/heads/mt/$branch/$d/mt-split^{commit} 2>/dev/null)
        not=$(bisect mt_split2mono_is_new_commit $r "$head")
        run git log --format="${d:--} %ct %H" \
            --first-parent --reverse $r --not $not
    done |
    sort --stable -n -k 2,2
}

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
        ds="$ds${ds:+ }$d"
    done
    local ct next head i=0 n=0
    while read d ct next; do
        mt_split2mono_translate_commit $d $next $head $ds ||
            error "could not translate $next for $d"
        head=$(mt_split2mono "$next") ||
            error "mapping not saved for '$next'"

        ref=refs/heads/mt/$branch/$d/mt-split
        git rev-parse --verify $ref^{commit} >/dev/null 2>&1 ||
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
