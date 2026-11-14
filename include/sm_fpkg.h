#ifndef SM_FPKG_H
#define SM_FPKG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "sm_pkg.h"

typedef enum {
  SM_FPKG_OK = 0,
  SM_FPKG_NOT_PS5,
  SM_FPKG_UNSUPPORTED,
  SM_FPKG_CORRUPT,
  SM_FPKG_IO,
  SM_FPKG_NOMEM,
  SM_FPKG_CANCELLED
} sm_fpkg_status_t;

typedef struct sm_fpkg sm_fpkg_t;

typedef struct {
  const char *path;
  uint64_t size;
  bool is_directory;
} sm_fpkg_file_t;

/*
 * Open a validated FPKG reader. The read callback and its context must stay
 * alive until sm_fpkg_close; the handle retains only bounded metadata and one
 * decoded Kraken block, not the package bytes.
 */
sm_fpkg_status_t sm_fpkg_open_stream(const char *name, uint64_t size,
                                     sm_pkg_read_fn read, void *ctx,
                                     const sm_pkg_info_t *metadata,
                                     sm_fpkg_t **fpkg_out);
void sm_fpkg_close(sm_fpkg_t *fpkg);
size_t sm_fpkg_file_count(const sm_fpkg_t *fpkg);
bool sm_fpkg_file_at(const sm_fpkg_t *fpkg, size_t index,
                     sm_fpkg_file_t *file_out);
sm_fpkg_status_t sm_fpkg_read_file(sm_fpkg_t *fpkg, size_t index,
                                   uint64_t offset, void *output,
                                   size_t size);

/*
 * Probe a package already classified by sm_pkg_parse_stream/file and release
 * its validated reader state. The probe is useful for routing decisions; use
 * sm_fpkg_open_stream when callers need to enumerate or read files.
 */
sm_fpkg_status_t sm_fpkg_probe_stream(const char *name, uint64_t size,
                                      sm_pkg_read_fn read, void *ctx,
                                      const sm_pkg_info_t *metadata);

#endif
