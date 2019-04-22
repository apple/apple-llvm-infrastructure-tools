# vim: ft=sh
MT_LLVM_SVN_BASE_TRAILER=apple-llvm-svn-base
mt_llvm_svn_impl() {
    local -a extra
    local format="%s\n"
    if [ "$1" = "--append-commit" ]; then
        extra=( -e '^commit ' )
        format="%s %s\n"
        shift
    else
        if [ "$1" = "--check-timestamps" ]; then
            extra=( -e '^author ' -e '^committer ' )
            shift
        fi
        if [ "$1" = "--apple-llvm-svn-base" ]; then
            extra=( "${extra[@]}" -e '^    '$MT_LLVM_SVN_BASE_TRAILER': ' )
            shift
        fi
    fi
    local sha1= committer= author=
    # TODO: testcase for not mapping other git-svn-id: urls.
    local urlstart=https://llvm.org/svn/llvm-project
    grep -e '^    \(llvm-svn: \|git-svn-id: '$urlstart'\)' "${extra[@]}" | {
    none=1
    while read key value rest; do
        # TODO: add a testcase for cherry-picks.
        if [ "$key" = "llvm-svn:" ]; then
            none=0 author= committer=
            printf "$format" "$value" $sha1
        elif [ "$key" = "git-svn-id:" ]; then
            [ -z "$author" -o -z "$committer" -o "$author" = "$committer" ] ||
                continue
            none=0 author= committer=
            printf "$format" "${value#*@}" $sha1
        elif [ "$key" = "apple-llvm-svn-base:" ]; then
            none=0 author= committer=
            printf "$format" "${value#*@}" $sha1
        elif [ "$key" = commit ]; then
            # TODO: add a testcase for when this skips over a commit that has
            # no rev.
            sha1="$value"
        elif [ "$key" = author ]; then
            author="$value $rest"
        elif [ "$key" = committer ]; then
            committer="$value $rest"
        else
            error "unexpected key '$key' while parsing raw log"
        fi
    done
    # TODO: add a testcase for this returning non-zero when there is no output.
    exit $none
    }
}
mt_llvm_svn() {
    # TODO: testcase for this returning non-zero when there is no output.
    # TODO: testcase for this returning non-zero when it's a cherry-pick.
    # TODO: testcase for not mapping other git-svn-id: urls.
    git rev-list --format=raw -1 "$1" | mt_llvm_svn_impl --check-timestamps
}

mt_llvm_svn_base() {
    # TODO: testcase for cherry-picked commits from LLVM, where we want
    # apple-llvm-svn-base: to win and git-svn-id: to be ignored.
    git rev-list --format=raw -1 "$1" |
        mt_llvm_svn_impl --check-timestamps --apple-llvm-svn-base
}
