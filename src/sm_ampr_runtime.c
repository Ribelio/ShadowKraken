#include "sm_ampr_runtime.h"

#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sm_paths.h"

#define AMPR_IDENTITY_CHUNK (64u * 1024u)

static bool contains_marker(const unsigned char *bytes, size_t size,
                            const char *marker, size_t marker_size) {
  if (size < marker_size)
    return false;
  for (size_t i = 0; i <= size - marker_size; ++i) {
    if (memcmp(bytes + i, marker, marker_size) == 0)
      return true;
  }
  return false;
}

int sm_file_is_ampr_ng(const char *path) {
  static const char marker[] = "ampr-emu-ng ";
  enum { MARKER_SIZE = sizeof(marker) - 1u };
  unsigned char buffer[AMPR_IDENTITY_CHUNK + MARKER_SIZE - 1u];
  size_t kept = 0;
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  int result = 0;
  int saved_errno = 0;
  for (;;) {
    ssize_t received = read(fd, buffer + kept, AMPR_IDENTITY_CHUNK);
    if (received < 0 && errno == EINTR)
      continue;
    if (received < 0) {
      result = -1;
      saved_errno = errno;
      break;
    }
    if (received == 0)
      break;

    size_t length = kept + (size_t)received;
    if (contains_marker(buffer, length, marker, MARKER_SIZE)) {
      result = 1;
      break;
    }
    kept = length < MARKER_SIZE - 1u ? length : MARKER_SIZE - 1u;
    memmove(buffer, buffer + length - kept, kept);
  }

  if (close(fd) != 0 && result >= 0) {
    result = -1;
    saved_errno = errno;
  }
  if (result < 0)
    errno = saved_errno;
  return result;
}

int sm_ampr_resolve_ng_runtime(const char *emulators_path,
                               char runtime_path[MAX_PATH]) {
  if (!emulators_path || !emulators_path[0] || !runtime_path) {
    errno = EINVAL;
    return -1;
  }
  int written = snprintf(runtime_path, MAX_PATH, "%s/%s/%s", emulators_path,
                         AMPR_NG_EMULATOR_DIR, AMPR_EMULATOR_FILE_NAME);
  if (written <= 0 || (size_t)written >= MAX_PATH) {
    runtime_path[0] = '\0';
    errno = ENAMETOOLONG;
    return -1;
  }

  struct stat st;
  if (stat(runtime_path, &st) != 0)
    return -1;
  if (!S_ISREG(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
  int is_ng = sm_file_is_ampr_ng(runtime_path);
  if (is_ng < 0)
    return -1;
  if (is_ng == 0) {
    errno = ENOEXEC;
    return -1;
  }
  return 0;
}
