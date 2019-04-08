# vim: ft=sh
mt_llvm_svn_impl() {
    local -a extra
    [ "$1" = "--prepend-commit" ] && extra=( -e '^commit ' )
    run grep -e '^    \(git-svn-id\|llvm-svn\): ' "${extra[@]}" |
    while read key value junk; do
        if [ "$key" = "llvm-svn:" ]; then
            printf "%s\n" "$value"
        elif [ "$key" = "git-svn-id:" ]; then
            printf "%s\n" "${value#*@}"
        else
            # This is the sha1, if kept.
            printf "%s " "$value"
        fi
    done
}
mt_llvm_svn() {
    run git rev-list --format=raw -1 "$1" | mt_llvm_svn_impl
}
