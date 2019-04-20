helper mt_split2mono
helper mt_llvm_svn
helper mt_llvm_svn2git

mt_split2mono_map_llvm() {
    local split="$1"

    # Check the commit.
    local head
    head=$(run --hide-errors git rev-parse "$split"^{commit}) ||
        error "could not extract commit from '$split'"

    # Confirm this is a valid LLVM commit.
    local rev
    rev=$(mt_llvm_svn $head) ||
        error "could not extract LLVM revision from '$split'"
    mt_llvm_svn2git "$rev" >/dev/null ||
        error "LLVM revision '$rev' (a.k.a. '$split') is not yet mapped"

    log "Looking for the first unmapped commit..."
    LOW=$(bisect mt_llvm_svn2git_is_commit_mapped $head^) || exit 1

    # Update all the refs.
    log "Mapping commits..."

    local mono
    run git rev-list --reverse $head --not $LOW |
    while read split; do
        rev=$(mt_llvm_svn $split 2>/dev/null) ||
            error "invalid LLVM commit '$split'"
        mono=$(mt_llvm_svn2git "$rev" 2>/dev/null) ||
            error "unmapped LLVM revision '$rev' (a.k.a. '$split')"
        printf "%s %s\n" $split $mono
    done |
    mt_split2mono_insert
}
