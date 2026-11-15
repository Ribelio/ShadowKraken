/* See include/sm_syscall_checked.h. Console build only. */
#define SM_SYSCALL_CHECKED_IMPL
#include "sm_platform.h"
#include "sm_syscall_checked.h"

/* The SDK's own pattern (libc/mount.c): carry set means rax holds errno. */
static long checked_syscall6(long n, long a1, long a2, long a3, long a4,
                             long a5, long a6) {
  unsigned long ret;
  char iserror;
  register long r10 __asm__("r10") = a4;
  register long r8 __asm__("r8") = a5;
  register long r9 __asm__("r9") = a6;
  __asm__ __volatile__("syscall"
                       : "=a"(ret), "=@ccc"(iserror), "+r"(r10), "+r"(r8),
                         "+r"(r9)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                       : "rcx", "r11", "memory");
  return iserror ? -(long)ret : (long)ret;
}

static long checked_result(long ret) {
  if (ret < 0) {
    errno = (int)-ret;
    return -1;
  }
  return ret;
}

#define CHECKED(n, a1, a2, a3, a4, a5, a6)                                    \
  checked_result(checked_syscall6((n), (long)(a1), (long)(a2), (long)(a3),   \
                                  (long)(a4), (long)(a5), (long)(a6)))

int sm_checked_openat(int fd, const char *path, int flags, ...) {
  int mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return (int)CHECKED(SYS_openat, fd, path, flags, mode, 0, 0);
}

int sm_checked_fstatat(int fd, const char *path, struct stat *buf, int flag) {
  return (int)CHECKED(SYS_fstatat, fd, path, buf, flag, 0, 0);
}

int sm_checked_mkdirat(int fd, const char *path, mode_t mode) {
  return (int)CHECKED(SYS_mkdirat, fd, path, mode, 0, 0, 0);
}

int sm_checked_unlinkat(int fd, const char *path, int flag) {
  return (int)CHECKED(SYS_unlinkat, fd, path, flag, 0, 0, 0);
}

int sm_checked_renameat(int fromfd, const char *from, int tofd,
                        const char *to) {
  return (int)CHECKED(SYS_renameat, fromfd, from, tofd, to, 0, 0);
}

ssize_t sm_checked_readlinkat(int fd, const char *path, char *buf,
                              size_t size) {
  return (ssize_t)CHECKED(SYS_readlinkat, fd, path, buf, size, 0, 0);
}

int sm_checked_linkat(int fd1, const char *path1, int fd2, const char *path2,
                      int flag) {
  return (int)CHECKED(SYS_linkat, fd1, path1, fd2, path2, flag, 0);
}

int sm_checked_symlinkat(const char *target, int fd, const char *path) {
  return (int)CHECKED(SYS_symlinkat, target, fd, path, 0, 0, 0);
}

int sm_checked_link(const char *path, const char *link_path) {
  return (int)CHECKED(SYS_link, path, link_path, 0, 0, 0, 0);
}

int sm_checked_dup(int fd) {
  return (int)CHECKED(SYS_dup, fd, 0, 0, 0, 0, 0);
}

int sm_checked_futimens(int fd, const struct timespec times[2]) {
  return (int)CHECKED(SYS_futimens, fd, times, 0, 0, 0, 0);
}

int sm_checked_statfs(const char *path, struct statfs *buf) {
  return (int)CHECKED(SYS_statfs, path, buf, 0, 0, 0, 0);
}

int sm_checked_fstatfs(int fd, struct statfs *buf) {
  return (int)CHECKED(SYS_fstatfs, fd, buf, 0, 0, 0, 0);
}

int sm_checked_getfsstat(struct statfs *buf, long size, int mode) {
  return (int)CHECKED(SYS_getfsstat, buf, size, mode, 0, 0, 0);
}
