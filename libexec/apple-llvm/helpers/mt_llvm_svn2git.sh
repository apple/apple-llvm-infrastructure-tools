helper build_executable
helper mt_db

mt_llvm_svn2git_init() { mt_db_init; }
mt_llvm_svn2git_save() { mt_db_save; }

mt_llvm_svn2git() {
    local rev="$1"

    local svn2git
    svn2git="$(build_executable svn2git)" ||
        error "could not build or find svn2git"
    run "$svn2git" lookup "$MT_DB_SVN2GIT_DB" "$rev"
}

mt_llvm_svn2git_insert() {
    local svn2git
    svn2git="$(build_executable svn2git)" ||
        error "could not build or find svn2git"
    local count
    [ "$#" -le 0 ] || count="$(run git rev-list --count "$@")"
    run "$svn2git" insert "$MT_DB_SVN2GIT_DB" $count
}

MT_LLVM_SVN2GIT_SKIP=afb1d31c54204b7f6c11e4f8815d203bcf9cffa3
mt_llvm_svn2git_is_commit_mapped() {
    local commit="$1"

    # Clump skipped commits with their parents.
    local rev
    if [ "$commit" = $MT_LLVM_SVN2GIT_SKIP ]; then
        rev=$(mt_llvm_svn $commit^) ||
            error "unexpected missing SVN rev for $commit^"
    elif [ -n "$commit" ]; then
        rev=$(mt_llvm_svn $commit) ||
            error "unexpected missing SVN rev for $commit"
    fi

    # TODO: write testcase that would catch passing "$1" here instead of "$rev".
    mt_llvm_svn2git "$rev" >/dev/null 2>&1
}
