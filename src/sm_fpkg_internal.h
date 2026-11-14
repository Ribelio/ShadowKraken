#ifndef SM_FPKG_INTERNAL_H
#define SM_FPKG_INTERNAL_H

#include "sm_fpkg.h"

typedef struct {
  uint64_t logical_offset;
  uint64_t stored_offset;
  uint32_t logical_length;
  uint32_t stored_length;
  uint32_t even_length;
  uint8_t mode;
  bool raw;
} sm_fpkg_layout_block_t;

typedef struct {
  sm_fpkg_layout_block_t *blocks;
  size_t block_count;
  uint64_t mount_size;
} sm_fpkg_layout_t;

/* Parse a bounded naps_pkg_layout.dat image for pfs_image.dat of image_size. */
sm_fpkg_status_t sm_fpkg_layout_parse(const uint8_t *descriptor,
                                      size_t descriptor_size,
                                      uint64_t image_size,
                                      sm_fpkg_layout_t *layout_out);
void sm_fpkg_layout_free(sm_fpkg_layout_t *layout);

#endif
