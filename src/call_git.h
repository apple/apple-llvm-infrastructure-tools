// call_git.h
#pragma once

#include "error.h"
#include "read_all.h"
#include <cstdio>
#include <spawn.h>
#include <string>
#include <sys/errno.h>
#include <unistd.h>
#include <vector>

static int call_git_impl(char *argv[], char *envp[], const std::string &input,
                         std::vector<char> &reply) {
  reply.clear();

  static bool once = false;
  static bool trace_git = false;
  if (!once) {
    once = true;
    if (const char *var = getenv("MT_TRACE_GIT"))
      trace_git = strcmp(var, "0");
  }

  struct cleanup {
    posix_spawn_file_actions_t *file_actions = nullptr;
    ~cleanup() {
      if (file_actions)
        posix_spawn_file_actions_destroy(file_actions);
    }
    int set(posix_spawn_file_actions_t &file_actions) {
      this->file_actions = &file_actions;
      return 0;
    }
  } cleanup;

  int fromgit[2];
  int togit[2];
  pid_t pid = -1;
  posix_spawn_file_actions_t file_actions;

  char *default_envp[] = {nullptr};
  if (!envp)
    envp = default_envp;

  if (trace_git) {
    fprintf(stderr, "#");
    for (char **x = envp; *x; ++x)
      fprintf(stderr, " '%s'", *x);
    for (char **x = argv; *x; ++x)
      fprintf(stderr, " '%s'", *x);
    fprintf(stderr, "\n");
  }

  bool needs_to_write = !input.empty();
  if (pipe(fromgit) || posix_spawn_file_actions_init(&file_actions) ||
      cleanup.set(file_actions) ||
      posix_spawn_file_actions_addclose(&file_actions, fromgit[0]) ||
      posix_spawn_file_actions_adddup2(&file_actions, fromgit[1], 1) ||
      (needs_to_write
           ? (pipe(togit) ||
              posix_spawn_file_actions_addclose(&file_actions, togit[1]) ||
              posix_spawn_file_actions_adddup2(&file_actions, togit[0], 0))
           : posix_spawn_file_actions_addclose(&file_actions, 0)) ||
      posix_spawnp(&pid, argv[0], &file_actions, nullptr, argv, envp))
    return error("call-git: failed to spawn git");
  assert(pid > 0);

  bool failed = false;
  failed |= close(fromgit[1]);
  failed |= close(togit[0]);
  if (failed)
    return error("call-git: failed to close pipe(s) to git");

  auto write_all = [&](int fd) {
    size_t next_byte = 0;
    while (next_byte < input.size()) {
      auto num_bytes_written =
          write(fd, input.data() + next_byte, input.size() - next_byte);
      if (num_bytes_written == -1)
        return 1;
      next_byte += num_bytes_written;
    }
    return 0;
  };

  // Write to and read from Git.
  if (!input.empty())
    if (write_all(togit[1]) || close(togit[1]))
      return error("call-git: failed to read output");
  if (read_all(fromgit[0], reply) || close(fromgit[0]))
    return error("call-git: failed to read output");

  int interrupts = 0;
  int status = 0;
  pid_t waited4pid;
  while ((waited4pid = wait4(pid, &status, 0, nullptr))) {
    if (waited4pid != -1)
      break;
    // LLDB seems to send EINTR a lot.
    if (errno == EINTR)
      if (++interrupts < 10)
        continue;
    return error("call-git: wait4: " + std::to_string(errno) + ": " +
                 std::string(strerror(errno)));
  }
  if (waited4pid != pid)
    return error("call-git: wrong pid for git");
  if (WIFSIGNALED(status))
    return error("call-git: git was signalled with " +
                 std::to_string(WTERMSIG(status)));
  if (!WIFEXITED(status))
    return error("call-git: git stopped, but we're done");
  if (int exit_status = WEXITSTATUS(status))
    return error("call-git: git exited with status " +
                 std::to_string(exit_status));

  return failed ? 1 : 0;
}

static int call_git(char *argv[], char *envp[], const std::string &input,
                    std::vector<char> &reply) {
  if (strcmp(argv[0], "git"))
    return error("wrong git executable");

  static bool once = false;
  static std::string git;
  if (!once) {
    once = true;
    const char *git_argv[] = {"git", "--exec-path", nullptr};
    char *git_envp[] = {nullptr};
    if (call_git_impl(const_cast<char **>(git_argv), git_envp, input, reply))
      return error("call-git: failed to scrape git --exec-path");
    git.reserve(reply.size() + 4);
    git.assign(reply.begin(), reply.end());
    git += "/git";
    reply.clear();
  }
  assert(!git.empty());
  argv[0] = const_cast<char *>(git.c_str());
  return call_git_impl(argv, envp, input, reply);
}

static int call_git(const char *argv[], const char *envp[],
                    const std::string &input, std::vector<char> &reply) {
  return call_git(const_cast<char **>(argv), const_cast<char **>(envp), input,
                  reply);
}
