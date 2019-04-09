mt_llvm_svn2git_init() {
    [ -n "$MT_SVN2GIT_INIT_ONCE" ] && return 0
    MT_SVN2GIT_INIT_ONCE=1

    local file
    local ref=refs/mt/svn2git
    local d
    d="$(run mktemp -d -t mt-temp)" ||
        error "could not create tempdir"
    file="$d"/svn2git
    MT_SVN2GIT_FILE="$file"

    local sha1 oldsha1
    if sha1=$(run git rev-parse $ref^{blob} 2>/dev/null); then
        MT_SVN2GIT_SHA1=$sha1
        local gitfile=$(cd "$d" &&
            run git --git-dir "$GIT_DIR" unpack-file $sha1) ||
            error "could not unpack $sha1 from $ref"
        run mv "$d/$gitfile" "$file" ||
            error "could not rename unpacked $sha1 from $ref"
    else
        MT_SVN2GIT_SHA1=0000000000000000000000000000000000000000
        touch "$file" ||
            error "could not create temp file for $ref"
    fi
    mt_register_paths_to_clean_up "$d"
}

mt_llvm_svn2git_save() {
    local sha1
    sha1=$(run git hash-object -w -- "$MT_SVN2GIT_FILE") ||
        error "could not hash mt-svn2git db"

    local ref=refs/mt/svn2git
    run git update-ref $ref $sha1 $MT_SVN2GIT_SHA1 ||
        error "could not save mt-svn2git to $ref; updated by another job?"
}

mt_llvm_svn2git() {
    local rev="$1"
    local seek sha1
    seek=$(( $rev * 20 )) ||
        error "invalid rev '$rev'"
    sha1=$(xxd -s $seek -g 0 -c 20 -l 20 -p "$MT_SVN2GIT_FILE" 2>/dev/null) ||
        return 1
    if [ -z "$sha1" -o "$sha1" = 0000000000000000000000000000000000000000 ]; then
        return 1
    fi
    echo "$sha1"
}

mt_llvm_svn2git_insert() {
    local mapper
    mapper="$(mktemp -t svn2git-insert)" ||
        error "could not create temp file for commit mapper"
    local src="$(dirname "$0")"/../src/svn2git-insert.c
    run clang -O2 -o "$mapper" "$src" &
    local clang=$!
    local count
    [ "$#" -le 0 ] || count="$(run git rev-list --count "$@")"
    wait $clang || error "could not build commit mapper"
    run "$mapper" "$MT_SVN2GIT_FILE" $count
}
