# vim: ft=sh
mt_llvm_svn_impl() {
    local -a extra
    local format="%s\n"
    if [ "$1" = "--append-commit" ]; then
        extra=( -e '^commit ' )
        format="%s %s\n"
    elif [ "$1" = "--check-timestamps" ]; then
        extra=( -e '^author ' -e '^committer ' )
    fi
    local sha1= committer= author=
    # TODO: testcase for not mapping other git-svn-id: urls.
    local urlstart=https://llvm.org/svn/llvm-project
    grep -e '^    \(llvm-svn: \|git-svn-id: '$urlstart'\)' "${extra[@]}" | {
    none=1
    while read key value rest; do
        # TODO: add a testcase for cherry-picks.
        [ -z "$author" -o -z "$committer" -o "$author" = "$committer" ] ||
            exit 1
        if [ "$key" = "llvm-svn:" ]; then
            none=0 author= committer=
            printf "$format" "$value" $sha1
        elif [ "$key" = "git-svn-id:" ]; then
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
