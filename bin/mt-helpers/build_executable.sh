build_executable() {
    local srcdir="$1"
    local name="$2"
    local srcfile="$srcdir"/"$name".cpp
    local makefile="$srcdir"/Makefile
    [ -r "$srcfile" ] || error "could not read '$srcfile'"

    # Hash the files in src/, but don't bother with system headers.
    local sha1 f path
    sha1="$(
    for f in $(cd "$srcdir" && ls *.h *.cpp); do
        path="$srcdir"/$f
        printf "%s " "$f"
        git hash-object --stdin <"$path"
    done | git hash-object --stdin)"


    local d="$TMPDIR"/mt-build-executable/$sha1
    [ -d "$d" ] || build_executable_impl "$srcdir" "$d"
    verbose && log "Checking for $name in $d"
    local execpath="$d"/"$name"
    [ -x "$execpath" ] || error "no executable for '$name'"
    echo "$execpath"
}
build_executable_impl() {
    local srcdir="$1"
    local d="$2"
    local -a extra
    verbose || extra=( -s )
    run make "${extra[@]}" -C "$srcdir" D="$d" ||
        error "could not build executables"
}
