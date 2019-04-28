// call_git.h
#pragma once

#include "error.h"
#include <cstdio>
#include <string>
#include <spawn.h>
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
static int call_git(char *argv[], char *envp[], git_reader reader,
                    void *rcontext, git_writer writer, void *wcontext) {
  if (strcmp(argv[0], "git"))
    return error("wrong git executable");

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

  if (pipe(fromgit) || posix_spawn_file_actions_init(&file_actions) ||
      cleanup.set(file_actions) ||
      posix_spawn_file_actions_addclose(&file_actions, fromgit[0]) ||
      posix_spawn_file_actions_adddup2(&file_actions, fromgit[1], 1) ||
      (writer ? (pipe(togit) ||
                 posix_spawn_file_actions_addclose(&file_actions, togit[1]) ||
                 posix_spawn_file_actions_adddup2(&file_actions, togit[0], 0))
              : posix_spawn_file_actions_addclose(&file_actions, 0)) ||
      posix_spawn(&pid, argv[0], &file_actions, nullptr, argv, envp) ||
      close(fromgit[1]) || (writer && close(togit[0])))
    return error("failed to spawn git");

  if (writer) {
    FILE *file = fdopen(fromgit[1], "1");
    if (!file)
      return error("failed to open stream to git");
    bool stop = false;
    while (!stop)
      if (writer(wcontext, file, stop)) {
        fclose(file);
        return 1;
      }
    if (fclose(file))
      return error("problem closing pipe writing to git");
  }

  FILE *file = fdopen(fromgit[0], "r");
  if (!file)
    return error("failed to open stream from git");

  size_t length = 0;
  while (char *line = fgetln(file, &length)) {
    if (!length || line[length - 1] != '\n') {
      fclose(file);
      return error("expected newline");
    }
    if (int status = reader(rcontext, std::string(line, line + length - 1))) {
      fclose(file);
      return status == EOF ? 0 : 1;
    }
  }
  if (!feof(file)) {
    fclose(file);
    return error("failed to read from git");
  }

  int status = 0;
  if (fclose(file) || wait(&status) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status))
    return error("git failed");

  return 0;
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

