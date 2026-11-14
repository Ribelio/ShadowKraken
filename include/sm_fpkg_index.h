#ifndef SM_FPKG_INDEX_H
#define SM_FPKG_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sm_fpkg.h"

typedef struct {
  uint64_t package_offset;
  uint32_t stored_length;
  uint32_t raw_length;
  uint32_t even_length;
  uint8_t mode;
  bool kraken_encoded;
} sm_fpkg_index_chunk_t;

typedef struct {
  /* Relative app0 path, for example "sce_sys/param.json". */
  const char *path;
  uint64_t size;
  /* NULL for loose files; otherwise one record per 256 KiB logical chunk. */
  const sm_fpkg_index_chunk_t *chunks;
  size_t chunk_count;
} sm_fpkg_index_file_t;

typedef struct {
  uint8_t *data;
  size_t size;
} sm_fpkg_index_blob_t;

/* Build deterministic AMPRIDX3 and AMPRPAK4 byte images. */
sm_fpkg_status_t sm_fpkg_index_build(
    const sm_fpkg_index_file_t *files, size_t file_count,
    uint64_t package_size, int64_t package_mtime,
    sm_fpkg_index_blob_t *index_out, sm_fpkg_index_blob_t *manifest_out);
void sm_fpkg_index_blob_free(sm_fpkg_index_blob_t *blob);

#endif
