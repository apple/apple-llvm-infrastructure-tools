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
