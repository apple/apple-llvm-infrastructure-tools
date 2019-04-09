// svn2git-insert.c
#include <fcntl.h>
#include <stdio.h>

void exit(int);
int convert(int ch) {
  switch (ch) {
  default:
    __builtin_unreachable();
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    return ch - '0';
  case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    return ch - 'a' + 10;
  }
}
int show_progress(int n, int total) {
  return fprintf(stderr, "   %9d / %d commits mapped\n", n, total) < 0;
}
int error(const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  return 1;
}
int usage(const char *msg, int argc, const char *argv[]) {
  error(msg);
  fprintf(stderr, "usage: %s <db> [<count>]\n", argv[0]);
  return 1;
}

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing <db>", argc, argv);
  const char *db = argv[1];
  const char *countstr = argv[2];
  int total = 0;
  if (argc >= 3)
    if (sscanf(countstr, "%d", &total) != 1)
      return usage("invalid <count>", argc, argv);

  // TODO: add test where commits are added in two chunks.  Initial commits
  // will be deleted if we use fopen instead of open/fdopen.
  int dbfd = open(db, O_WRONLY | O_CREAT);
  if (dbfd == -1)
    return usage("could not open <db> file descriptor", argc, argv);
  FILE *out = fdopen(dbfd, "wb");
  if (!out)
    return usage("could not open <db> stream", argc, argv);

  char sha1[64];
  char binsha1[64];
  int rev;
  int n = 0;
  while (scanf("%d %s", &rev, sha1) == 2) {
    const int offset = sha1[0] == '-' ? 1 : 0;
    for (int i = 0 + offset; i < 40 + offset; i += 2)
      binsha1[i / 2 ] = (convert(sha1[i]) << 4) | convert(sha1[i + 1]);
    if (fseek(out, rev * 20, SEEK_SET))
      return error("could not seek to rev");
    int written = fwrite(binsha1, 1, 20, out);
    if (!written)
      return error("no bytes written");
    if (written != 20) {
      error("not enough bytes written");
      return 2; // file is now invalid...
    }
    if ((++n % 5000) == 0)
      if (show_progress(n, total))
        return error("could not show progress");
  }
  if (show_progress(n, total))
    return error("could not show progress");
  if (fclose(out))
    return error("could not close <db>");
  return 0;
}
