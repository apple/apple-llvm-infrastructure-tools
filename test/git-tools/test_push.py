"""
  Tests for the `git apple-llvm push` tool.
"""

from click.testing import CliRunner
from git_apple_llvm.git_tools.push import git_apple_llvm_push
from git_apple_llvm.git_tools import git_output
from monorepo_test_harness import commit_file


def test_push_invalid_source_ref(cd_to_monorepo):
    result = CliRunner().invoke(git_apple_llvm_push, ['foo:dest'],
                                mix_stderr=True)
    assert 'refspec "foo" is invalid' in result.output
    assert result.exit_code == 1


def test_push_invalid_dest_ref(cd_to_monorepo):
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:dest'],
                                mix_stderr=True)
    assert 'refspec "dest" is invalid' in result.output
    assert result.exit_code == 1


def test_push_unsupported_def_ref(cd_to_monorepo_clone):
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:llvm/master'],
                                mix_stderr=True)
    assert 'destination Git refspec "llvm/master" cannot be pushed to' in result.output
    assert result.exit_code == 1


def test_push_up_to_date(cd_to_monorepo_clone):
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master'],
                                mix_stderr=True)
    assert 'No commits to commit: everything up-to-date' in result.output
    assert result.exit_code == 0


def test_push_clang_commit(cd_to_monorepo_clone,
                           monorepo_simple_clang_remote_git_dir,
                           capfd):
    current_clang_top = git_output('rev-parse', 'master',
                                   git_dir=monorepo_simple_clang_remote_git_dir)

    file_contents = 'internal: cool file'
    commit_file('clang/a-new-file', file_contents)
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master',
                                                      '--merge-strategy=ff-only'])
    assert 'Pushing to clang' in result.output
    assert result.exit_code == 0
    captured = capfd.readouterr()

    new_clang_top = git_output('rev-parse', 'master',
                               git_dir=monorepo_simple_clang_remote_git_dir)
    # Verify that the `git push` output is printed.
    assert f'{new_clang_top} -> master' in captured.err
    assert new_clang_top != current_clang_top
    assert git_output('rev-parse', 'master~1',
                      git_dir=monorepo_simple_clang_remote_git_dir) == current_clang_top
    assert git_output('show', f'master:a-new-file',
                      git_dir=monorepo_simple_clang_remote_git_dir) == file_contents


def test_push_root_commit(cd_to_monorepo_clone,
                          monorepo_simple_root_remote_git_dir,
                          capfd):
    current_root_top = git_output('rev-parse', 'internal/master',
                                  git_dir=monorepo_simple_root_remote_git_dir)

    file_contents = 'internal: cool file'
    commit_file('root-file', file_contents)
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master',
                                                      '--merge-strategy=ff-only'])
    assert 'Pushing to monorepo root' in result.output
    assert result.exit_code == 0
    captured = capfd.readouterr()

    new_root_top = git_output('rev-parse', 'internal/master',
                              git_dir=monorepo_simple_root_remote_git_dir)
    # Verify that the `git push` output is printed.
    assert f'{new_root_top} -> internal/master' in captured.err
    assert new_root_top != current_root_top
    assert git_output('rev-parse', 'internal/master~1',
                      git_dir=monorepo_simple_root_remote_git_dir) == current_root_top
    assert git_output('show', f'internal/master:root-file',
                      git_dir=monorepo_simple_root_remote_git_dir) == file_contents


def test_push_prohibited_split_dir(cd_to_monorepo_clone):
    commit_file('libcxxabi/testplan', 'it works!')
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master',
                                                      '--merge-strategy=ff-only'],
                                mix_stderr=True)
    assert 'push configuration "internal-master" prohibits pushing to "libcxxabi"' in result.output
    assert result.exit_code == 1


def test_push_many_llvm_commits(cd_to_monorepo_clone,
                                monorepo_simple_llvm_remote_git_dir):
    current_llvm_top = git_output('rev-parse', 'master',
                                  git_dir=monorepo_simple_llvm_remote_git_dir)
    for i in range(0, 50):
        commit_file(f'llvm/a-new-file{i}', 'internal: cool file')
    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master',
                                                      '--merge-strategy=ff-only'],
                                mix_stderr=True)
    assert 'pushing 50 commits, are you really sure' in result.output
    assert result.exit_code == 1

    result = CliRunner().invoke(git_apple_llvm_push, ['HEAD:internal/master',
                                                      '--merge-strategy=ff-only',
                                                      '--push-limit=51'],
                                mix_stderr=True)
    assert 'Pushing to llvm' in result.output
    assert result.exit_code == 0
    assert git_output('rev-parse', 'master~50',
                      git_dir=monorepo_simple_llvm_remote_git_dir) == current_llvm_top
