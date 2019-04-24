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
mt_split2mono_translate_map_parent() {
    local parent="$1"
    local first="$2"
    local first_parent_override="$3"

    # TODO: add a testcase for merge commits, checking that only the first
    # parent is overridden.
    if [ $first -eq 1 -a -n "$first_parent_override" ]; then
        mt_split2mono $parent >/dev/null ||
            error "overridden parent of '$commit' must be translated first"
        echo "$first_parent_override"
        return 0
    fi
    mt_split2mono $parent
}
mt_split2mono_translate_map_parents() {
    local main_commit=$1
    local main_parent_override="$2"
    local commit=$3
    shift 3

    [ "$commit" = "$main_commit" ] || main_parent_override=
    local p mp
    local retval=0
    local -a mparents
    local first=1
    # TODO: add a testcase for merge commits, checking that only the first
    # parent is overridden.
    for p in "$@"; do
        if mp=$(mt_split2mono_translate_map_parent $p $first \
            "$main_parent_override"); then
            mparents=( "${mparents[@]}" "$main_parent_override" )
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
    local tree=$2
    if [ $splitdir = - ]; then
        # Dereference if this is a root.
        git ls-tree --full-tree $tree ||
            error "could not dereference tree '$tree'"
        return 0
    fi
    printf "%s %s %s\t%s\n" 040000 tree $tree $splitdir
}

mt_split2mono_translate_list_tree_with_dups() {
    local splitdir="$1"
    local tree=$2
    local mparents="$3"
    shift 3
    local -a skipped_dirs
    skipped_dirs=( "$@" )
    local fparent=${mparents%% *}

    if [ -z "$fparent" ]; then
        # Unparented monorepo commit, from a history that eventually got merged
        # in.  Just list it.
        mt_split2mono_translate_list_split_tree $splitdir $tree
        return 0
    fi
    if [ $fparent = "$mparents" ]; then
        # Only one parent.  List its tree, and then override with this tree.
        git ls-tree --full-tree $fparent &&
            mt_split2mono_translate_list_split_tree $splitdir $tree
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
    local  rev  mode  type  sha1  name d skip in_d mp
    local lrev lmode ltype lsha1 lname
    for mp in $mparents; do
        rev=$(mt_llvm_svn_base $mp) || {
            echo "poison mktree with junk"
            error "could not extract base LLVM rev"
        }
        git ls-tree --full-tree $mp | sed -e "s,^,$rev ," || {
            echo "poison mktree with junk"
            error "sed substitution issue for rev '$rev'"
        }
    done |
    sort --stable --field-separator='	' -k2,2 | uniq |
    while read -r rev mode type sha1 name; do
        if [ "$type" = blob ]; then
            d=-
        else
            d=$name
        fi

        # Skip this if it's coming from the split commit.
        [ "$d" = "$splitdir" ] && continue

        # Print this if it's our first sighting of name.
        # FIXME: this will fail to delete blobs when there is no out-of-tree
        # '-' dir.
        if [ ! "$lname" = "$name" ]; then
            # if --first-parent has this, it'll be printed here
            printf "%s %s %s\t%s\n" $mode $type $sha1 "$name"
            lrev=$rev lmode=$mode ltype=$type lsha1=$sha1 lname=$name
            skip=
            continue
        fi

        # If the content is identical don't look any further.
        [ $mode = $lmode -a $type = $ltype -a $sha1 = $lsha1 ] &&
            continue

        # If --first-parent wants priority then ignore its competition.
        # TODO: add a testcase where we have an out-of-tree '-' dir
        if [ -z "$skip" ]; then
            skip=0
            for in_d in "${skipped_dirs[@]}"; do
                [ "$d" = "$in_d" ] || continue
                skip=1 && break
            done
        fi
        [ $skip -eq 1 ] && continue

        # Associate LLVM revs with each change, and let the newest one win.
        #
        # FIXME: this will do the wrong thing for split histories not based on
        # LLVM at all.  Probably drop the trailer in that case and have the rev
        # be implicitly 0, but need good tests ahead of that.
        #
        # FIXME: this is completely wrong if a release branch and mainline get
        # merged.
        #
        # FIXME: this provides a pretty weird heuristic for things that don't
        # come from LLVM upstream.
        #
        # FIXME: this adds a trailer we probably don't need long-term.  The
        # reason for the trailer is laziness of implementation: it's easier
        # than tracking out-of-band in split2mono.db.  But if this is the
        # approach we take we should probably just implement and drop the
        # trailer.
        #
        # Note: An alternative, but expensive, heuristic is to use `git log --
        # <path>` and compare timestamps.  Look at `git blame` for an
        # implementation of that.
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
        # the dir in question).  This is probably too slow.

        # If ct isn't newer than lct, stick with what we have.
        [ $rev -gt $lrev ] || continue

        # Print out an override.
        printf "%s %s %s\t%s\n" $mode $type $sha1 "$name"
        lrev=$rev lmode=$mode ltype=$type lsha1=$sha1 lname=$name
    done

    # Print out the tree we actually care about.
    mt_split2mono_translate_list_split_tree $splitdir $tree
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
    trailers="--trailer apple-llvm-split-commit:$commit"
    trailers="$trailers --trailer apple-llvm-split-subdir:$splitdir"
    [ "$splitdir" = - ] || trailers="$trailers/"

    # Prefix the parents with '-p'.
    local p_cmd rev p_rev
    p_cmd=
    for mp in $mparents; do
        p_rev=$(mt_llvm_svn_base $mp) ||
            error "could not find LLVM base rev for '$mp'"
        if [ -z "$rev" ] || [ "$rev" -lt "$p_rev" ]; then
            rev=$p_rev
        fi
        p_cmd="$p_cmd${p_cmd:+ }-p $mp"
    done
    [ -n "$rev" ] || error "no $MT_LLVM_SVN_BASE_TRAILER in $mparents"
    trailers="$trailers --trailer $MT_LLVM_SVN_BASE_TRAILER:$rev"

    git log -1 --format=%B $commit |
    git interpret-trailers $trailers |
    run git commit-tree $p_cmd $newtree
}

mt_split2mono_translate_commit() {
    local parent_override=
    if [ "$1" = --override-parent ]; then
        parent_override="$2"
        shift 2
    fi
    local commit="$1"
    local tree="$2"
    local d="$3"
    local parents="$4"
    local ds="$5"

    # Lookup the split commit's parents' monorepo commits.  This will
    # return non-zero if one of the parents is not mapped (and was pushed
    # on the stack).
    local mparents=
    if [ -z "$parents" ]; then
        # May have been mapped by mt_split2mono_translate_map_parents.
        [ ${#MT_SPLIT2MONO_TRANSLATE_MPARENTS[@]} -eq 0 ] ||
            mparents="${MT_SPLIT2MONO_TRANSLATE_MPARENTS[*]}"
    else
        local first=1 p= mp=
        for p in $parents; do
            mp=$(mt_split2mono_translate_map_parent $p $first \
                "$parent_override") ||
                return 1
            mparents="$mparents${mparents:+ }$mp"
            first=0
        done
    fi

    # commit, map, and pop
    newtree=$(mt_split2mono_translate_make_tree "$d" $tree "$mparents" $ds) ||
        exit 1
    newcommit=$(mt_split2mono_translate_commit_tree "$d" $commit "$mparents" \
        $newtree) ||
        exit 1
    mt_split2mono_insert $commit $newcommit ||
        error "failed to insert mapping '$commit' -> '$newcommit'"
    echo "$newcommit"
}
