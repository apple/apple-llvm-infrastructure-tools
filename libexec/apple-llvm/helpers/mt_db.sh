# vim: ft=sh

mt_db()          { printf "%s\n" refs/mt/mt-db; }
mt_db_upstream() { printf "%s.%s\n" "$(mt_db)" "$1"; }
mt_db_worktree()          { printf "%s\n"    "$GIT_DIR"/mt-db.checkout; }
mt_db_worktree_upstream() { printf "%s/%s\n" "$GIT_DIR"/mt-db-$1.checkout; }

mt_db_init() {
    [ -z "$MT_DB_INIT_ONCE" ] || return 0
    MT_DB_INIT_ONCE=1

    local ref=$(mt_db)
    run --hide-errors git symbolic-ref "$ref" ||
        error "expected symbolic ref: $ref"

    local wt="$(mt_db_worktree)"
    MT_DB_SVN2GIT_DB="$wt"/svn2git.db
    MT_DB_SPLIT2MONO_DB="$wt/"split2mono.db

    if ! run --hide-errors git rev-parse --verify $ref^{commit} >/dev/null; then
        mt_db_make_ref && mt_db_make_worktree
        return $?
    fi

    local count
    count=$(run --hide-errors git rev-list --count $ref) ||
        error "internal: failed to verify $ref"
    case $count in
        2) true ;;
        *) error "expected 2 commits in $ref" ;;
    esac
    if [ -d "$wt" ]; then
        # Reset to match the ref.
        run --hide-errors git -C "$wt" reset --hard "$ref" >/dev/null ||
            error "internal: failed to reset $wt"
    else
        # Handle a pre-existing ref with no worktree.
        mt_db_make_worktree
    fi
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
    local ref="$(mt_db)"
    # Don't use -q here because some bots have an old git-worktree that doesn't
    # have it.  Instead redirect to /dev/null manually.
    run git worktree add "$wt" "$ref" >/dev/null ||
        error "failed to create worktree for $ref at '$wt'"
    local count
    count=$(run git -C "$wt" rev-list --count HEAD) ||
        error "failed to get number of commits in $ref"
    case $count in
        1) true ;;
        2) return 0 ;;
        *) error "expected 1 or 2 commits in $ref" ;;
    esac

    local svn2git split2mono
    svn2git="$(build_executable svn2git)" ||
        error "could not build svn2git"
    split2mono="$(build_executable split2mono)" ||
        error "could not build split2mono"

    # Extract the name from the local ref.
    local localref
    localref="$(run git symbolic-ref "$ref")" ||
        error "$ref is not a symbolic ref"
    local name
    eval 'name="${localref#'"$ref"'.}"'
    [ ! "$name" = "$ref" ] || error "could not extract name"

    # Only one commit.  Make some databases.
    {
        run mkdir "$MT_DB_SPLIT2MONO_DB" &&
            run "$svn2git" create "$MT_DB_SVN2GIT_DB" &&
            run "$split2mono" create "$MT_DB_SPLIT2MONO_DB" "$name"
    } || error "could not create initial db for '$ref'"

    {
        run git -C "$wt" add "$MT_DB_SVN2GIT_DB" &&
            run git -C "$wt" add "$MT_DB_SPLIT2MONO_DB" &&
            run git -C "$wt" commit -q -am "mt-db 0" &&
            run git -C "$wt" update-ref $ref HEAD
    } || error "could not commit empty db for '$ref'"
}

mt_db_save() {
    local ref=$(mt_db) wt="$(mt_db_worktree)"
    run git -C "$wt" add -u ||
        error "failed to add current mt-db to the index"
    local new old
    new=$(run git -C "$wt" write-tree) ||
        error "failed to write new tree for $ref"
    old=$(run git rev-parse $ref:) ||
        error "failed to parse old tree for $ref"
    [ "$new" = "$old" ] && return 0

    local subject num
    subject="$(git log -1 --format=%s "$ref")" ||
        error "failed to find extract subject from $ref"
    num=${subject#mt-db }
    num=$(( $num + 1 )) || error "failed to rev mt-db"
    run git -C "$wt" commit -q --amend -m "mt-db $num" ||
        error "failed to commit mt-db to $ref"
    run git -C "$wt" update-ref $ref HEAD ||
        error "failed to update $ref to HEAD"
}

mt_db_merge_upstream() { mt_db_upstream_impl "$1" 0; }
mt_db_check_upstream() { mt_db_upstream_impl "$1" 1; }

mt_db_upstream_impl() {
    local upstream="$1" dryrun="$2"
    local uref="$(mt_db_upstream "$1")" uwt="$(mt_db_worktree_upstream "$1")"
    if [ -d "$uwt" ]; then
        run git -C "$uwt" reset --hard "$uref" ||
            error "failed to reset worktree to $uref at '$uwt'"
    else
        run git worktree add "$uwt" "$uref" >/dev/null ||
            error "failed to create worktree for $uref at '$uwt'"
    fi

    local usvn2gitdb="$uwt"/svn2git.db
    local usplit2monodb="$uwt/"split2mono.db
    [ -r "$usvn2gitdb" ] || error "could not read '$usvn2gitdb'"
    [ -r "$usplit2monodb" ] || error "could not read '$usplit2monodb'"

    local svn2git split2mono
    svn2git="$(build_executable svn2git)" ||
        error "could not build svn2git"
    split2mono="$(build_executable split2mono)" ||
        error "could not build split2mono"

    if [ "${dryrun:-0}" = 0 ]; then
        # Do the merge.
        run "$split2mono" upstream "$MT_DB_SPLIT2MONO_DB" "$usplit2monodb" ||
            error "could not update split2mono from '$upstream'"
        run cp "$usvn2gitdb" "$MT_DB_SVN2GIT_DB" ||
            error "could not update svn2git from '$upstream'"
    else
        # Check for things to do in the merge.
        run "$split2mono" check-upstream \
            "$MT_DB_SPLIT2MONO_DB" "$usplit2monodb" || return 1
        diff -q "$usvn2gitdb" "$MT_DB_SVN2GIT_DB" || return 1
    fi
}
