/* Errno-setting replacements for the PS5 SDK's raw *at and statfs wrappers.
 *
 * The SDK defines many FreeBSD system calls in libc/syscalls.c as a bare
 * "syscall; ret". On failure FreeBSD sets the carry flag and leaves the
 * positive error number in rax, so those functions return, for example, 2
 * for ENOENT instead of -1, and errno is not set. A caller that tests "< 0"
 * then treats the error number as a file descriptor. The SDK fixed this for
 * mount/nmount/unmount in libc/mount.c (7116a65); this does the same for the
 * calls ShadowKraken uses. Enabled only for the console build
 * (SM_CHECKED_SYSCALLS); host tests use the host libc. */
#ifndef SM_SYSCALL_CHECKED_H
#define SM_SYSCALL_CHECKED_H

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

int sm_checked_openat(int fd, const char *path, int flags, ...);
int sm_checked_fstatat(int fd, const char *path, struct stat *buf, int flag);
int sm_checked_mkdirat(int fd, const char *path, mode_t mode);
int sm_checked_unlinkat(int fd, const char *path, int flag);
int sm_checked_renameat(int fromfd, const char *from, int tofd, const char *to);
ssize_t sm_checked_readlinkat(int fd, const char *path, char *buf, size_t size);
int sm_checked_linkat(int fd1, const char *path1, int fd2, const char *path2,
                      int flag);
int sm_checked_symlinkat(const char *target, int fd, const char *path);
int sm_checked_link(const char *path, const char *link_path);
int sm_checked_dup(int fd);
int sm_checked_futimens(int fd, const struct timespec times[2]);
int sm_checked_statfs(const char *path, struct statfs *buf);
int sm_checked_fstatfs(int fd, struct statfs *buf);
int sm_checked_getfsstat(struct statfs *buf, long size, int mode);

#ifndef SM_SYSCALL_CHECKED_IMPL
/* Function-like, so "struct statfs" and other non-call uses are untouched. */
#define openat(...) sm_checked_openat(__VA_ARGS__)
#define fstatat(...) sm_checked_fstatat(__VA_ARGS__)
#define mkdirat(...) sm_checked_mkdirat(__VA_ARGS__)
#define unlinkat(...) sm_checked_unlinkat(__VA_ARGS__)
#define renameat(...) sm_checked_renameat(__VA_ARGS__)
#define readlinkat(...) sm_checked_readlinkat(__VA_ARGS__)
#define linkat(...) sm_checked_linkat(__VA_ARGS__)
#define symlinkat(...) sm_checked_symlinkat(__VA_ARGS__)
#define link(...) sm_checked_link(__VA_ARGS__)
#define dup(...) sm_checked_dup(__VA_ARGS__)
#define futimens(...) sm_checked_futimens(__VA_ARGS__)
#define statfs(...) sm_checked_statfs(__VA_ARGS__)
#define fstatfs(...) sm_checked_fstatfs(__VA_ARGS__)
#define getfsstat(...) sm_checked_getfsstat(__VA_ARGS__)
#endif

#endif
