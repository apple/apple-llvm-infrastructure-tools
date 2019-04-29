// call_git.h
#pragma once

#include "error.h"
#include <cstdio>
#include <spawn.h>
#include <string>
#include <sys/errno.h>
#include <unistd.h>

template <class T>
static int forward_to_reader(void *context, std::string line) {
  return (*reinterpret_cast<T *>(context))(std::move(line));
}

template <class T>
static int forward_to_writer(void *context, FILE *file, bool &stop) {
  return (*reinterpret_cast<T *>(context))(file, stop);
}

typedef int (*git_reader)(void *, std::string);
typedef int (*git_writer)(void *, FILE *, bool &);
static int call_git_impl(char *argv[], char *envp[], git_reader reader,
                         void *rcontext, git_writer writer, void *wcontext) {
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

  if (pipe(fromgit) || posix_spawn_file_actions_init(&file_actions) ||
      cleanup.set(file_actions) ||
      posix_spawn_file_actions_addclose(&file_actions, fromgit[0]) ||
      posix_spawn_file_actions_adddup2(&file_actions, fromgit[1], 1) ||
      (writer ? (pipe(togit) ||
                 posix_spawn_file_actions_addclose(&file_actions, togit[1]) ||
                 posix_spawn_file_actions_adddup2(&file_actions, togit[0], 0))
              : posix_spawn_file_actions_addclose(&file_actions, 0)) ||
      posix_spawnp(&pid, argv[0], &file_actions, nullptr, argv, envp))
    return error("call-git: failed to spawn git");
  assert(pid > 0);

  auto capture_write_errors = [&](FILE *&file) {
    file = fdopen(togit[1], "w");
    if (!file)
      return error("call-git: failed to open stream to git");

    bool stop = false;
    while (!stop)
      if (writer(wcontext, file, stop))
        return 1;
    return 0;
  };
  auto capture_read_errors = [&](FILE *&file) {
    file = fdopen(fromgit[0], "r");
    if (!file)
      return error("call-git: failed to open stream from git");

    size_t length = 0;
    int interrupts = 0;
    do {
      while (char *line = fgetln(file, &length)) {
        interrupts = 0;

        // fgetln includes the newline.  length should never be zero.
        //
        // Note: the final line might NOT be terminated by a newline, if this
        // is pulling the body of a commit message which doesn't have one.
        if (!length)
          return error("call-git: fgetln: empty string");

        if (int status = reader(rcontext, std::string(line, line + length - 1)))
          return status == EOF ? 0 : 1;
      }

      // LLDB seems to send EINTR a lot.
      if (++interrupts >= 10)
        break;
    } while (ferror(file) && errno == EINTR);

    if (ferror(file))
      return error("call-git: fgetln: " + std::to_string(errno) + ": " +
                   std::string(strerror(errno)));
    if (!feof(file))
      return error("call-git: failed to read from git");
    return 0;
  };
  auto capture_errors = [&]() {
    if (close(fromgit[1]) || (writer && close(togit[0])))
      return error("call-git: failed to close pipe(s) to git");

    if (writer) {
      FILE *file = nullptr;
      bool failed = capture_write_errors(file);
      if (file && fclose(file))
        failed |= error("call-git: failed to close write-end");
      file = nullptr;
      if (failed)
        return 1;
    }

    FILE *file = nullptr;
    bool failed = capture_read_errors(file);
    if (file && fclose(file))
      failed |= error("call-git: closing the file from git failed");
    file = nullptr;
    return failed ? 1 : 0;
  };

  // LLDB sends EINTR a lot.
  int interrupts = 0;
  bool failed = capture_errors();
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

static int call_git(char *argv[], char *envp[], git_reader reader,
                    void *rcontext, git_writer writer, void *wcontext) {
  if (strcmp(argv[0], "git"))
    return error("wrong git executable");

  static bool once = false;
  static std::string git;
  if (!once) {
    once = true;
    auto git_reader = [&](std::string line) {
      git = line + "/git";
      return EOF;
    };
    const char *git_argv[] = {"git", "--exec-path", nullptr};
    char *git_envp[] = {nullptr};
    if (call_git_impl(const_cast<char **>(git_argv), git_envp,
                      forward_to_reader<decltype(git_reader)>, &git_reader,
                      nullptr, nullptr))
      return error("call-git: failed to scrape git --exec-path");
  }
  assert(!git.empty());
  argv[0] = const_cast<char *>(git.c_str());
  return call_git_impl(argv, envp, reader, rcontext, writer, wcontext);
}

template <class T>
static int call_git(const char *argv[], const char *envp[], T reader) {
  return call_git(const_cast<char **>(argv), const_cast<char **>(envp),
                  forward_to_reader<T>, reinterpret_cast<void *>(&reader),
                  nullptr, nullptr);
}
template <class T, class U>
static int call_git(const char *argv[], const char *envp[], T reader,
                    U writer) {
  return call_git(const_cast<char **>(argv), const_cast<char **>(envp),
                  forward_to_reader<T>, reinterpret_cast<void *>(&reader),
                  forward_to_writer<U>, reinterpret_cast<void *>(&writer));
}
