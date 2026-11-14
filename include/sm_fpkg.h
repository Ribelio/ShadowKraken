#ifndef SM_FPKG_H
#define SM_FPKG_H

#include <stddef.h>
#include <stdint.h>

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

/*
 * Probe only the outer eligibility envelope of a package already parsed by
 * sm_pkg_parse_stream/file. This deliberately does not expose on-disk structs
 * or claim that the package tree or Kraken blocks are valid.
 */
sm_fpkg_status_t sm_fpkg_probe_stream(const char *name, uint64_t size,
                                      sm_pkg_read_fn read, void *ctx,
                                      const sm_pkg_info_t *metadata);

#endif
