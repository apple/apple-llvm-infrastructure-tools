# vim: ft=sh

mt_db() { printf "%s\n" refs/mt/mt-db; }
mt_db_worktree() { printf "%s\n" "$GIT_DIR"/mt-db.checkout; }

mt_db_init() {
    [ -z "$MT_DB_INIT_ONCE" ] || return 0
    MT_DB_INIT_ONCE=1

    MT_DB_SPLIT2MONO_DB=split2mono.db
    MT_DB_SVN2GIT_DB=svn2git.db

    local ref=$(mt_db)
    if git rev-parse --verify $ref^{commit} >/dev/null 2>&1; then
        case $count in
            2) true ;;
            *) error "expected 2 commits in $(mt_db)" ;;
        esac
        return 0
    fi
    mt_db_make_ref && mt_db_make_worktree
}

mt_db_make_ref() {
    local tree commit ref=$(mt_db)
    tree=$(run git mktree </dev/null) ||
        error "failed to create an empty tree object for $ref"
    commit=$(run git commit-tree -m "Initialize refs/mt/mt-db" "$tree") ||
        error "failed to create initial commit for $ref"
    run git update-ref $ref $commit "" ||
        error "failed to create $ref"
}

mt_db_make_worktree() {
    local wt="$(mt_db_worktree)"
    run git worktree "$wt" "$(mt_db)" ||
        error "failed to create worktree for $(mt_db) at '$wt'"
    local count
    count=$(run git --work-tree "$wt" rev-list HEAD --count) ||
        error "failed to get number of commits in $(mt_db)"
    case $count in
        1) true ;;
        2) return 0 ;;
        *) error "expected 1 or 2 commits in $(mt_db)" ;;
    esac

    local svn2git split2mono
    svn2git="$(build_executable svn2git)" ||
        error "could not build svn2git"
    split2mono="$(build_executable split2mono)" ||
        error "could not build split2mono"

    # Only one commit.  Make some databases.
    svn2git_db="$wt"/"$MT_DB_SVN2GIT_DB"
    split2mono_db="$wt"/"$MT_DB_SPLIT2MONO_DB"
    {
        mkdir "$split2monodb" &&
            "$svn2git" create "$svn2git_db" &&
            "$split2mono" create "$split2mono_db"
    } || error "could not create initial db for '$ref'"

    {
        run git --work-tree "$wt" add "$svn2git_db" "$split2mono_db" &&
            run git --work-tree "$wt" commit -am "mt-db 0"
    } || error "could not commit empty db for '$ref'"
}

mt_db_save_worktree() {
    local ref=$(mt_db) wt="$(mt_db_worktree)"
    run git --work-tree "$wt" add -u ||
        error "failed to add current mt-db to the index"
    local new old
    new=$(run git --work-tree "$wt" write-tree) ||
        error "failed to write new tree for $ref"
    old=$(run git rev-parse $ref:) ||
        error "failed to parse old tree for $ref"
    [ "$new" = "$old" ] && return 0

    local subject num
    subject="$(git log --format=%s "$ref")" ||
        error "failed to find extract subject from $ref"
    num=${subject#mt-db }
    num=$(( $num + 1 )) || error "failed to rev mt-db"
    run git --work-tree "$wt" commit --amend -m "mt-db $num" ||
        error "failed to commit mt-db to $ref"
}
