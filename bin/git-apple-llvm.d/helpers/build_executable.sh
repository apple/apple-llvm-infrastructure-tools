build_executable() {
    local name="$1"
    local srcdir="$APPLE_LLVM_BIN_DIR"/../src
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


    local pd="$TMPDIR"mt-build-executable
    local d="$pd/$sha1"
    local execpath="$d"/"$name"
    if [ -x "$execpath" ]; then
        echo "$execpath"
        return
    fi

    run --hide-errors mkdir -p "$pd"
    [ -d "$pd" ] && build_executable_impl "$srcdir" "$d"
    local status=$?
    verbose && log "Checking for $name in $d"
    [ -x "$execpath" ] || error "no executable for '$name'"
    echo "$execpath"
    return $status
}
build_executable_impl() {
    [ -r "$d"/.done ] && return 0
    local srcdir="$1"
    local d="$2"
    local -a extra
    verbose || extra=( -s )

    run mkdir -p "$d" || exit 1
    make -C "$srcdir" "${extra[@]}" D="$d" 2>&1 |
    sed -e 's,^[^#],#'"${SHOWPIDS:+ [make]}"' &,' 1>&2 ||
        error "could not build executables"
}
