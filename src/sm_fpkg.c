/*
 * Native debug-FPKG eligibility reader for ShadowMountPlus.
 *
 * Format behavior follows the GPL-3.0-or-later Kraken Mount reader and its
 * ps5-dump-forge format references. This initial reader validates only the
 * FIH/CNT envelope and plaintext outer-PFS marker; it does not parse the inner
 * tree or decode Kraken blocks.
 */
#include "sm_fpkg.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

#define SM_FPKG_FIH_SIZE 0x100u
#define SM_FPKG_CNT_HEADER_SIZE 0x1000u
#define SM_FPKG_OUTER_BLOCK_SIZE 0x10000u
#define SM_FPKG_SUPERBLOCK_SIZE 0x5A0u
#define SM_FPKG_PLAINTEXT_MARKER "PPRPLAIN-NOAUTH!"
#define SM_FPKG_SUPERBLOCK_MAGIC 20130315u

static uint32_t read_be32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint32_t read_le32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (int i = 7; i >= 0; --i)
    value = (value << 8) | bytes[i];
  return value;
}

static bool range_fits(uint64_t offset, uint64_t length, uint64_t total) {
  return offset <= total && length <= total - offset;
}

static bool pkg_extension(const char *name) {
  if (!name)
    return false;
  const char *dot = strrchr(name, '.');
  return dot && strcasecmp(dot, ".pkg") == 0;
}

sm_fpkg_status_t sm_fpkg_probe_stream(const char *name, uint64_t size,
                                      sm_pkg_read_fn read, void *ctx,
                                      const sm_pkg_info_t *metadata) {
  if (!name || !read || !metadata || size < SM_FPKG_FIH_SIZE ||
      !pkg_extension(name))
    return SM_FPKG_CORRUPT;
  if (metadata->title_id[0] == '\0' ||
      metadata->pkg_kind != PKG_KIND_BASE ||
      !metadata->pkg_classification_confident)
    return SM_FPKG_UNSUPPORTED;

  uint8_t fih[SM_FPKG_FIH_SIZE];
  int result = read(ctx, 0, fih, sizeof(fih));
  if (result != 0)
    return SM_FPKG_IO;
  if (read_be32(fih) == 0x7F434E54u)
    return SM_FPKG_NOT_PS5;
  if (read_be32(fih) == 0x7F4C4948u)
    return SM_FPKG_UNSUPPORTED;
  if (read_be32(fih) != 0x7F464948u)
    return SM_FPKG_NOT_PS5;
  if (fih[5] != 0)
    return SM_FPKG_UNSUPPORTED;
  if (fih[6] != 3 || fih[7] != 0)
    return SM_FPKG_UNSUPPORTED;
  if (read_le32(fih + 0x60) != SM_FPKG_OUTER_BLOCK_SIZE)
    return SM_FPKG_CORRUPT;

  uint64_t pfs_offset = read_le64(fih + 0x10);
  uint64_t pfs_size = read_le64(fih + 0x18);
  uint64_t superblock_offset = read_le64(fih + 0x20);
  uint64_t cnt_offset = read_le64(fih + 0x58);
  if (pfs_offset < SM_FPKG_FIH_SIZE ||
      (pfs_offset % SM_FPKG_OUTER_BLOCK_SIZE) != 0 || pfs_size == 0 ||
      (pfs_size % SM_FPKG_OUTER_BLOCK_SIZE) != 0 ||
      !range_fits(pfs_offset, pfs_size, size) ||
      !range_fits(cnt_offset, SM_FPKG_CNT_HEADER_SIZE, size))
    return SM_FPKG_CORRUPT;

  if (superblock_offset < pfs_offset ||
      ((superblock_offset - pfs_offset) % SM_FPKG_OUTER_BLOCK_SIZE) != 0 ||
      !range_fits(superblock_offset, SM_FPKG_SUPERBLOCK_SIZE,
                  pfs_offset + pfs_size))
    return SM_FPKG_CORRUPT;

  uint8_t cnt[4];
  result = read(ctx, cnt_offset, cnt, sizeof(cnt));
  if (result != 0)
    return SM_FPKG_IO;
  if (read_be32(cnt) != 0x7F434E54u)
    return SM_FPKG_CORRUPT;

  uint8_t superblock[SM_FPKG_SUPERBLOCK_SIZE];
  result = read(ctx, superblock_offset, superblock, sizeof(superblock));
  if (result != 0)
    return SM_FPKG_IO;
  if (read_le64(superblock) != 2 ||
      read_le64(superblock + 8) != SM_FPKG_SUPERBLOCK_MAGIC)
    return SM_FPKG_CORRUPT;
  if (memcmp(superblock + 0x370, SM_FPKG_PLAINTEXT_MARKER, 16) != 0)
    return SM_FPKG_UNSUPPORTED;

  return SM_FPKG_OK;
}
