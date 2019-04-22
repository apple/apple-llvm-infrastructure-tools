# vim: ft=sh
# bin/mt-helpers/mt_split2mono_translate_commit.sh

MT_SPLIT2MONO_TRANSLATE_TODO=()
MT_SPLIT2MONO_TRANSLATE_TODO_I=0
mt_split2mono_translate_empty() { [ $MT_SPLIT2MONO_TRANSLATE_TODO_I -eq 0 ]; }
mt_split2mono_translate_push() {
    # Access the array from 1.  Bash counts from 0, but some shells count from
    # 1 and bash won't care if we start higher.  Might as well be robust to a
    #   future migration to a simpler shell.
    MT_SPLIT2MONO_TRANSLATE_TODO_I=$(( MT_SPLIT2MONO_TRANSLATE_TODO_I + 1 ))
    MT_SPLIT2MONO_TRANSLATE_TODO[$MT_SPLIT2MONO_TRANSLATE_TODO_I]="$1"
}
mt_split2mono_translate_pop() {
    mt_split2mono_translate_empty && return 1

    # Don't bother unsetting the array entry.  No one is looking.
    MT_SPLIT2MONO_TRANSLATE_TODO_I=$(( MT_SPLIT2MONO_TRANSLATE_TODO_I - 1 ))
}
mt_split2mono_translate_top() {
    mt_split2mono_translate_empty && return 1
    echo "${MT_SPLIT2MONO_TRANSLATE_TODO[$MT_SPLIT2MONO_TRANSLATE_TODO_I]}"
}

MT_SPLIT2MONO_TRANSLATE_MPARENTS=()
mt_split2mono_translate_map_parents() {
    local main_commit=$1
    local main_parent_override="$2"
    local commit=$3
    shift 3

    local p
    local retval=0
    local -a mparents
    local first=1
    for p in "$@"; do
        # TODO: add a testcase for merge commits, checking that only the first
        # parent is overridden.
        if [ $first -eq 1 -a -n "$main_parent_override" -a \
            $commit = $main_commit ]; then
            mt_split2mono $p >/dev/null ||
                error "overridden parent of '$commit' must be translated first"
            mparents=( "${mparents[@]}" "$main_parent_override" )
        elif mp=$(mt_split2mono $p); then
            mparents=( "${mparents[@]}" "$mp" )
        else
            mt_split2mono_translate_push $p || exit 1
            retval=1
        fi
        first=0
    done
    [ $retval -eq 0 ] || return $retval
    MT_SPLIT2MONO_TRANSLATE_MPARENTS=( "${mparents[@]}" )
}

mt_split2mono_translate_list_split_tree() {
    local splitdir="$1"
    local commit=$2
    local tree
    if [ $splitdir = - ]; then
        # Dereference if this is a root.
        git ls-tree --full-tree $commit: ||
            error "could not dereference $commit"
    else
        tree=$(git rev-parse $commit:) ||
            error "problem with rev-parse"
        printf "%s %s %s\t%s\n" 040000 tree $tree $splitdir
    fi
}

mt_split2mono_translate_list_tree_with_dups() {
    local splitdir="$1"
    local commit=$2
    local mparents="$3"
    shift 3
    local -a skipped_dirs
    skipped_dirs=( "$@" )
    local fparent=${mparents%% *}

    if [ -z "$fparent" ]; then
        # Unparented monorepo commit, from a history that eventually got merged
        # in.  Just list it.
        mt_split2mono_translate_list_split_tree $splitdir $commit
        return 0
    fi
    if [ $fparent = "$mparents" ]; then
        # Only one parent.  List its tree, and then override with this tree.
        git ls-tree --full-tree $fparent &&
            mt_split2mono_translate_list_split_tree $splitdir $commit
        return 0
    fi

    # Multiple parents.
    # - list each tree, prefixing entries with which parent commit owns it
    # - sort by the tree entry name
    #     - stable so that relative order of parents for each entry does not
    #       change
    # - reprint tree entries that are the best we have "so far"
    # - finally override with the split tree at hand
    #
    # FIXME: not convinced this works for top-level entries that get deleted
    #
    # TODO: add a testcase where sorting matters for putting other split dirs
    # next to each other.
    local  d  ct  mp  mode  type  sha1 name skip in_d
    local ld lct lmp lmode ltype lsha1
    for mp in $mparents; do
        git ls-tree --full-tree $mp | sed -e "s,^,$mp ,"
    done |
    sort --stable --field-separator='	' -k2,2 | uniq |
    while read -r mp mode type sha1 name; do
        if [ "$type" = blob ]; then
            d=-
        else
            d=$name
        fi

        # Skip this if it's coming from the split commit.
        [ "$d" = "$splitdir" ] && continue

        # Print this if it's our first sighting of d.
        if [ ! "$ld" = "$d" ]; then
            # if --first-parent has this, it'll be printed here
            printf "%s %s %s\t%s\n" $mode $type $sha1 "$name"
            ld=$d lmp=$mp lmode=$mode ltype=$type lsha1=$sha1
            skip= lct=
            continue
        fi

        # If the content is identical don't look any further.
        [ $mode = $lmode -a $type = $ltype -a $sha1 = $lsha1 ] &&
            continue

        # If --first-parent wants priority then ignore its competition.
        if [ -z "$skip" ]; then
            skip=0
            for in_d in "${skipped_dirs[@]}"; do
                [ "$d" = "$in_d" ] || continue
                skip=1 && break
            done
        fi
        [ $skip -eq 1 ] && continue

        # Associate timestamps with each change, and let the newest timestamp
        # win.  This could be expensive.
        #
        # FIXME: this is expensive in practice.
        #
        # FIXME: the heuritistic doesn't seem to be working, or we're getting
        # the timestamps from the wrong commits.
        #
        # This heuristic just finds the most recent non-merge commit in each
        # history that touched the path.
        #
        # FIXME: This logic might be sketchy.  Skipping merge commits is
        # a bit questionable.  We may need to model timestamps more clearly
        # somehow.  Maybe this is good enough for llvm-project-v0, but perhaps
        # not the final llvm-project.
        #
        # Note: An obvious alternative is to blindly take the most recent
        # commit (including merges!).  However, this will do the wrong thing
        # in a common case:
        #
        #   - tree entry: a directory, D, in github/llvm/llvm-project that
        #     there's no downstream split repo for.
        #
        #   - two parents, both with changes to this directory.
        #       - parent A: merges r200 from llvm.org on April 1st, which
        #         brings in r199 which changes D.
        #       - parent B: merges r100 from llvm.org on April 2nd, which
        #         brings in r99 which changes D.
        #       - this commit: merges A and B.
        #
        #   - the correct D is the one from r199
        #       - commit dates of --no-merges would give r199
        #       - commit dates including merges would give r99
        #
        # Note: Another alternative (not yet deeply considered) is to --grep
        # for apple-llvm-split-subdir: and llvm-svn: trailers, and use the
        # newest timestamp that has one of those (for ...-subdir, only include
        # the dir in question).  This could be too slow.

        # Grab timestamps.  Note that lct is grabbed lazily, only now.
        [ -n "$lct" ] ||
            lct=$(git log -1 --date-order --format=format:%ct $lmp -- "$name")
        ct=$(git log -1 --date-order --format=format:%ct $mp -- "$name")

        # If ct isn't newer than lct, stick with what we have.
        [ $ct -gt $lct ] || continue

        # Print out an override.
        printf "%s %s %s\t%s\n" $mode $type $sha1 "$name"
        ld=$d lct=$ct lmp=$mp lmode=$mode ltype=$type lsha1=$sha1
    done

    # Print out the tree we actually care about.
    mt_split2mono_translate_list_split_tree $splitdir $commit
}
mt_split2mono_translate_list_tree() {
    # Remove duplicate tree entries, treating repeats as overrides.
    mt_split2mono_translate_list_tree_with_dups "$@" |
    awk -F'\t' '{x[$2]=$0} END {for(n in x) print x[n]}'
}
mt_split2mono_translate_make_tree() {
    # Make a tree out of this listing!
    mt_split2mono_translate_list_tree "$@" | git mktree
}

mt_split2mono_translate_commit_tree() {
    local splitdir="$1" commit="$2" mparents="$3" newtree="$4"

    # Add trailers so we can see where we've been.
    #
    # FIXME: Using git-interpret-trailers is sketchy, since it handles "---"
    # badly for our commit history.  Given that we can't trust
    # git-interpret-trailers --parse when mapping commits in
    # mt_llvm_svn2git_map, why should we trust it here?
    trailers=( --trailer apple-llvm-split-commit:$commit )
    [ -n "$splitdir" ] &&
        trailers=( "${trailers[@]}"
                   --trailer apple-llvm-split-subdir:$splitdir/ )

    # Prefix the parents with '-p'.
    local pcmd
    pcmd=()
    for mp in $mparents; do
        pcmd=( "${pcmd[@]}" -p $mp )
    done

    # Extract committer and author information.
    # TODO: add a test for committer and author information.
    local ad an ae cd cn ce
    ad="$(git log -1 --date=raw --format="%ad" $commit)" ||
        error "could not extract author date from '$commit'"
    an="$(git log -1 --date=raw --format="%an" $commit)" ||
        error "could not extract author name from '$commit'"
    ae="$(git log -1 --date=raw --format="%ae" $commit)" ||
        error "could not extract author email from '$commit'"
    cd="$(git log -1 --date=raw --format="%cd" $commit)" ||
        error "could not extract committer date from '$commit'"
    cn="$(git log -1 --date=raw --format="%cn" $commit)" ||
        error "could not extract committer name from '$commit'"
    ce="$(git log -1 --date=raw --format="%ce" $commit)" ||
        error "could not extract committer email from '$commit'"

    git log -1 --format=%B $commit |
    git interpret-trailers "${trailers[@]}" |
    GIT_AUTHOR_NAME="$an" GIT_COMMITTER_NAME="$cn" \
    GIT_AUTHOR_DATE="$ad" GIT_COMMITTER_DATE="$cd" \
    GIT_AUTHOR_EMAIL="$ae" GIT_COMMITTER_EMAIL="$ce" \
    run git commit-tree "${pcmd[@]}" $newtree
}

mt_split2mono_translate_commit() {
    # usage: mt-split2mono-translate-commit
    #           <dir> <commit> [<parent> <pdirs>...]
    #   <dir>       the directory to move to; use "" for top-level
    #   <commit>    commit hash to translate
    #   <parent>    overrides first-parent
    #   <pdirs>     dirs to take from <parent> instead of "newest"

    local splitdir="$1" main_commit="$2" main_parent_override="$3"
    shift 3
    local main_parent_override_dirs=( "$@" )

    # FIXME: If there's a big history to map, using a stack like this will be
    # very slow.  It would be better to git rev-list --reverse --topo-order.
    # However, what --not arguments do you we use?  Should we do a bisect,
    # similar to mt_llvm_svn2git_map, to find out?  But given that this is not
    # expected to be a linear history, how easy is that?
    #
    # Maybe we can just collect radars in chunks, going back 100 or so at a
    # time.  Then rev-list all the found radars in topological order to do the
    # real work.  As a first cut this stack is fine though.
    mt_split2mono_translate_push "$main_commit" ||
        error "could not push '$main_commit' onto the stack"

    while ! mt_split2mono_translate_empty; do
        # Take a look at the top of the stack.
        commit="$(mt_split2mono_translate_top)" ||
            error "could not pull out the top of the stack, '$commit'"
        [ -n "$commit" ] ||
            error "the stack is not working"

        # Pop it off if it's already mapped.
        #
        # Note: Maybe we should move or copy the mt_llvm_svn logic from
        # mt_split2mono to just below this, and map those commits explicitly.
        # See the FIXME there for more discussion.
        if mt_split2mono $commit >/dev/null; then
            mt_split2mono_translate_pop ||
                error "could not pop already-translated commit"
            continue
        fi

        # Lookup the split commit's parents' monorepo commits.  This will
        # return non-zero if one of the parents is not mapped (and was pushed
        # on the stack).
        mt_split2mono_translate_map_parents $main_commit \
            "$main_parent_override" $commit $(git rev-parse $commit^@) ||
            continue
        mparents="${MT_SPLIT2MONO_TRANSLATE_MPARENTS[*]}"

        # Collect the dirs hard-coded to the parent, if this is the main commit
        # we're mapping.
        [ $commit = $main_commit -a -n "$main_parent_override" ] &&
            firstpdirs=( "${main_parent_override_dirs[@]}" )

        # commit, map, and pop
        newtree=$(mt_split2mono_translate_make_tree "$splitdir" $commit \
            "$mparents" "${firstpdirs[@]}") ||
            exit 1
        newcommit=$(mt_split2mono_translate_commit_tree "$splitdir" $commit \
            "$mparents" $newtree) ||
            exit 1
        mt_split2mono_insert $commit $newcommit ||
            error "failed to insert mapping '$commit' -> '$newcommit'"
        mt_split2mono_translate_pop ||
            error "problem popping the top of the stack"
    done
    return 0
}
