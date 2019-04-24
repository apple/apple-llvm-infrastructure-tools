helper build_executable
helper mt_db
helper mt_llvm_svn
helper mt_llvm_svn2git

mt_split2mono_init() { mt_db_init; }
mt_split2mono_save() { mt_db_save; }

mt_split2mono() {
    local split="$1"

    # Check the commit map first.
    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    run "$split2mono" lookup "$MT_DB_SPLIT2MONO_DB" "$split"
    [ $? -eq 0 ] && return 0

    # FIXME: This could be too slow.  Instead of requiring a double lookup, we
    # should consider moving this logic to mt_split2mono_translate_commit and
    # saving this.  The upside of the current approach is that we avoid
    # bloating split2mono.db with 200k+ extra commits.  Bloating the DB could
    # slow down accesses, as well as adding overhead when creating, extracting,
    # and transferring the refs.
    local rev
    if rev=$(mt_llvm_svn "$split"); then
        mt_llvm_svn2git $rev
        return $?
    fi
    return 1
}

mt_split2mono_insert() {
    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    run "$split2mono" insert "$MT_DB_SPLIT2MONO_DB" "$@"
}
