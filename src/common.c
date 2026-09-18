#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void emit(FILE *f, const char *prefix, const char *fmt, va_list ap) {
  if (prefix) fputs(prefix, f);
  vfprintf(f, fmt, ap);
  fputc('\n', f);
  fflush(f);
}

void sh_out(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(stdout, NULL, fmt, ap); va_end(ap);
}
void sh_note(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(stdout, "  ", fmt, ap); va_end(ap);
}
void sh_warn(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(stderr, "warning: ", fmt, ap); va_end(ap);
}
void sh_fail(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(stderr, "error: ", fmt, ap); va_end(ap);
}

const char *sh_cg_error(CGError err) {
  switch (err) {
    case kCGErrorSuccess:          return "kCGErrorSuccess";
    case kCGErrorFailure:          return "kCGErrorFailure";
    case kCGErrorIllegalArgument:  return "kCGErrorIllegalArgument";
    case kCGErrorInvalidConnection:return "kCGErrorInvalidConnection";
    case kCGErrorInvalidContext:   return "kCGErrorInvalidContext";
    case kCGErrorCannotComplete:   return "kCGErrorCannotComplete";
    case kCGErrorNotImplemented:   return "kCGErrorNotImplemented";
    case kCGErrorRangeCheck:       return "kCGErrorRangeCheck";
    case kCGErrorTypeCheck:        return "kCGErrorTypeCheck";
    case kCGErrorInvalidOperation: return "kCGErrorInvalidOperation";
    case kCGErrorNoneAvailable:    return "kCGErrorNoneAvailable";
    default: break;
  }
  static char buf[32];
  snprintf(buf, sizeof buf, "CGError(%d)", (int)err);
  return buf;
}

void sh_step(const char *label, CGError err) {
  sh_note("%-24s = %d (%s)", label, (int)err, sh_cg_error(err));
}

int sh_run(const char *const argv[]) {
  if (!argv || !argv[0]) return -1;
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    execvp(argv[0], (char *const *)argv);
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* retry */ }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int sh_capture(const char *const argv[], char *buf, size_t buflen) {
  if (buf && buflen) buf[0] = '\0';
  int fds[2];
  if (pipe(fds) != 0) return -1;

  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }

  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    /* 丢掉 stderr：defaults 读不存在的键时会刷一堆噪声 */
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) dup2(devnull, STDERR_FILENO);
    close(fds[1]);
    execvp(argv[0], (char *const *)argv);
    _exit(127);
  }

  close(fds[1]);
  size_t used = 0;
  for (;;) {
    char chunk[256];
    ssize_t got = read(fds[0], chunk, sizeof chunk);
    if (got <= 0) break;
    if (buf && used + 1 < buflen) {
      size_t room = buflen - 1 - used;
      size_t take = (size_t)got < room ? (size_t)got : room;
      memcpy(buf + used, chunk, take);
      used += take;
      buf[used] = '\0';
    }
  }
  close(fds[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* retry */ }

  if (buf && used) {
    while (used > 0 && (buf[used - 1] == '\n' || buf[used - 1] == '\r')) buf[--used] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0'; /* 只保留首行 */
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
