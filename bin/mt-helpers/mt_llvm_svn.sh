# vim: ft=sh
mt_llvm_svn_impl() {
    local -a extra
    local format="%s\n"
    if [ "$1" = "--append-commit" ]; then
        extra=( -e '^commit ' )
        format="%s %s\n"
    fi
    local sha1=
    grep -e '^    \(git-svn-id\|llvm-svn\): ' "${extra[@]}" |
    while read key value junk; do
        if [ "$key" = "llvm-svn:" ]; then
            printf "$format" "$value" $sha1
        elif [ "$key" = "git-svn-id:" ]; then
            printf "$format" "${value#*@}" $sha1
        else
            # This is the sha1, if kept.
            sha1="$value"
        fi
    done
}
mt_llvm_svn() {
    git rev-list --format=raw -1 "$1" | mt_llvm_svn_impl
}
