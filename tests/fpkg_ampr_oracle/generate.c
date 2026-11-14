/* Writes a disposable synthetic AMPR index/manifest and pack payload. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utime.h>

#include "sm_fpkg_index.h"

#define PACKAGE_SIZE 0x60000u
#define CHUNK_OFFSET 0x20000u
#define CHUNK_SIZE 0x40000u

static int write_file(const char *path, const void *bytes, size_t size) {
  FILE *file = fopen(path, "wb");
  if (!file)
    return -1;
  int result = fwrite(bytes, 1, size, file) == size ? 0 : -1;
  if (fclose(file) != 0)
    result = -1;
  return result;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
    return 2;
  }
  char index_path[4096], manifest_path[4096], package_path[4096];
  int index_len = snprintf(index_path, sizeof(index_path), "%s/ampr_emu.index", argv[1]);
  int manifest_len = snprintf(manifest_path, sizeof(manifest_path), "%s/ampr_assets.index", argv[1]);
  int package_len = snprintf(package_path, sizeof(package_path), "%s/kraken.pkg", argv[1]);
  if (index_len < 0 || (size_t)index_len >= sizeof(index_path) ||
      manifest_len < 0 || (size_t)manifest_len >= sizeof(manifest_path) ||
      package_len < 0 || (size_t)package_len >= sizeof(package_path)) {
    fprintf(stderr, "output path formatting failed\n");
    return 2;
  }

  sm_fpkg_index_chunk_t chunk = {
      .package_offset = CHUNK_OFFSET,
      .stored_length = CHUNK_SIZE,
      .raw_length = CHUNK_SIZE,
      .even_length = 0x20000u,
      .mode = 0,
      .kraken_encoded = true,
  };
  sm_fpkg_index_file_t file = {
      .path = "eboot.bin",
      .size = CHUNK_SIZE,
      .chunks = &chunk,
      .chunk_count = 1,
  };
  sm_fpkg_index_blob_t index = {0}, manifest = {0};
  sm_fpkg_status_t status = sm_fpkg_index_build(
      &file, 1, PACKAGE_SIZE, 123, &index, &manifest);
  if (status != SM_FPKG_OK) {
    fprintf(stderr, "AMPR index generation failed: %d\n", status);
    return 1;
  }
  uint8_t *package = calloc(PACKAGE_SIZE, 1);
  if (!package) {
    sm_fpkg_index_blob_free(&index);
    sm_fpkg_index_blob_free(&manifest);
    return 1;
  }
  for (size_t i = 0; i < CHUNK_SIZE; ++i)
    package[CHUNK_OFFSET + i] = (uint8_t)((i * 29u + i / 113u) & 0xffu);
  int result = write_file(index_path, index.data, index.size) |
               write_file(manifest_path, manifest.data, manifest.size) |
               write_file(package_path, package, PACKAGE_SIZE);
  if (result == 0) {
    struct utimbuf fixed_time = {.actime = 123, .modtime = 123};
    if (utime(package_path, &fixed_time) != 0)
      result = -1;
  }
  if (result != 0)
    fprintf(stderr, "failed writing oracle inputs: %s\n", strerror(errno));
  free(package);
  sm_fpkg_index_blob_free(&index);
  sm_fpkg_index_blob_free(&manifest);
  return result == 0 ? 0 : 1;
}
