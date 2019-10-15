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
    run "$split2mono" compute-mono "$MT_DB_SPLIT2MONO_DB" "$MT_DB_SVN2GIT_DB" \
        "$split"
}

mt_split2mono_insert() {
    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    run "$split2mono" insert "$MT_DB_SPLIT2MONO_DB" "$@"
}
