# vim: ft=sh
mt_llvm_svn_impl() {
    local -a extra
    local format="%s\n"
    if [ "$1" = "--append-commit" ]; then
        extra=( -e '^commit ' )
        format="%s %s\n"
    fi
    local sha1=
    grep -e '^    \(git-svn-id\|llvm-svn\): ' "${extra[@]}" | {
    none=1
    while read key value junk; do
        if [ "$key" = "llvm-svn:" ]; then
            none=0
            printf "$format" "$value" $sha1
        elif [ "$key" = "git-svn-id:" ]; then
            none=0
            printf "$format" "${value#*@}" $sha1
        else
            # This is the sha1, if kept.
            # TODO: add a testcase for when this skips over a commit that has
            # no rev.
            sha1="$value"
        fi
    done
    # TODO: add a testcase for this returning 0 when there is no output.
    exit $none
    }
}
mt_llvm_svn() {
    # TODO: add a testcase for this returning 0.
    git rev-list --format=raw -1 "$1" | mt_llvm_svn_impl
}
