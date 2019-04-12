helper build_executable
helper mt_llvm_svn
helper mt_llvm_svn2git

mt_split2mono_init() {
    [ -n "$MT_SPLIT2MONO_INIT_ONCE" ] && return 0
    MT_SPLIT2MONO_INIT_ONCE=1

    local ref=refs/mt/split2mono
    local d
    d="$(run mktemp -d -t mt-split2mono)" || error "could not create tempdir"
    local db="$d"/split2mono.db
    MT_SPLIT2MONO_DB="$db"

    local sha1 oldsha1 status=0 name
    mt_register_paths_to_clean_up "$d"
    if sha1=$(run --hide-errors git rev-parse $ref^{tree}); then
        MT_SPLIT2MONO_SHA1=$sha1
        for name in commits index upstreams; do
            local blob
            blob=$(git ls-tree --full-tree $sha1 -- $name | awk '{print $3}') ||
                error "could not unpack $sha1 from $ref"

            local gitfile
            gitfile=$(cd "$db" &&
                run git --git-dir "$GIT_DIR" unpack-file $sha1) ||
                error "could not unpack $sha1 from $ref"

            run mv "$d/$gitfile" "$db"/$name ||
                error "could not rename unpacked $sha1 from $ref"
        done
        return 0
    fi
    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    MT_SPLIT2MONO_SHA1=0000000000000000000000000000000000000000
    run "$split2mono" create "$MT_SPLIT2MONO_DB"
}

mt_split2mono_save() {
    local ref=refs/mt/split2mono
    local sha1
    sha1=$(mt_split2mono_mktree) ||
        error "failed to build tree for split2mono.db"
    run git update-ref $ref $sha1 $MT_SPLIT2MONO_SHA1 ||
        error "could not save split2mono.db to $ref; updated by another job?"
}
mt_split2mono_mktree() {
    local sha1
    for name in commits index upstreams; do
        sha1=$(run git hash-object -w -- "$MT_SPLIT2MONO_DB") ||
            error "could not hash mt-split2mono db"
        printf "100644 blob %s \t%s\n" "$sha1" "$name"
    done |
    run git mktree
}

mt_split2mono() {
    local split="$1"

    # FIXME: This could be too slow.  Instead of requiring a double lookup, we
    # should consider moving this logic to mt_split2mono_translate_commit and
    # savning this.  The upside of the current approach is that we avoid
    # bloating split2mono.db with 200k+ extra commits.  Bloating the DB could
    # slow down accesses, as well as adding overhead when creating, extracting,
    # and transferring the refs.
    local rev
    if rev=$(mt_llvm_svn "$split"); then
        mt_llvm_svn2git $rev
        exit $?
    fi

    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    run "$split2mono" lookup "$MT_SPLIT2MONO_DB" "$split"
}

mt_split2mono_insert() {
    local split2mono
    split2mono="$(build_executable split2mono)" ||
        error "could not build or find split2mono"
    run "$split2mono" insert "$MT_SPLIT2MONO_DB" "$@"
}
