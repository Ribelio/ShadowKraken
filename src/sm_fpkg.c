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
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SM_FPKG_FIH_SIZE 0x100u
#define SM_FPKG_CNT_HEADER_SIZE 0x1000u
#define SM_FPKG_OUTER_BLOCK_SIZE 0x10000u
#define SM_FPKG_SUPERBLOCK_SIZE 0x5A0u
#define SM_FPKG_PLAINTEXT_MARKER "PPRPLAIN-NOAUTH!"
#define SM_FPKG_SUPERBLOCK_MAGIC 20130315u
#define SM_FPKG_DINODE_SIZE 0x2C8u
#define SM_FPKG_DINODE_DIRECT_OFFSET 0x64u
#define SM_FPKG_DINODE_DIRECT_COUNT 12u

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

static sm_fpkg_status_t read_outer_block(sm_pkg_read_fn read, void *ctx,
                                         uint64_t pfs_offset,
                                         uint64_t block_count, uint32_t block,
                                         uint8_t *buffer) {
  if ((uint64_t)block >= block_count)
    return SM_FPKG_CORRUPT;
  uint64_t offset = pfs_offset + (uint64_t)block * SM_FPKG_OUTER_BLOCK_SIZE;
  return read(ctx, offset, buffer, SM_FPKG_OUTER_BLOCK_SIZE) == 0
             ? SM_FPKG_OK
             : SM_FPKG_IO;
}

static bool dinode_block(const uint8_t *dinode, unsigned slot,
                         uint32_t *block_out) {
  if (slot >= SM_FPKG_DINODE_DIRECT_COUNT)
    return false;
  size_t at = SM_FPKG_DINODE_DIRECT_OFFSET + (size_t)slot * 36u + 32u;
  *block_out = read_le32(dinode + at);
  return true;
}

static sm_fpkg_status_t outer_child(
    sm_pkg_read_fn read, void *ctx, uint64_t pfs_offset, uint64_t block_count,
    const uint8_t *dinodes, uint64_t dinode_count, uint32_t directory_inode,
    const char *wanted, uint32_t *inode_out, uint8_t *block) {
  if ((uint64_t)directory_inode >= dinode_count)
    return SM_FPKG_CORRUPT;
  const uint8_t *directory = dinodes + (size_t)directory_inode * SM_FPKG_DINODE_SIZE;
  uint64_t directory_size = read_le64(directory + 8);
  uint32_t data_block;
  if (directory_size > SM_FPKG_OUTER_BLOCK_SIZE ||
      !dinode_block(directory, 0, &data_block))
    return SM_FPKG_CORRUPT;
  sm_fpkg_status_t status = read_outer_block(
      read, ctx, pfs_offset, block_count, data_block, block);
  if (status != SM_FPKG_OK)
    return status;

  size_t limit = (size_t)directory_size;
  size_t at = 0;
  size_t wanted_length = strlen(wanted);
  while (at < limit) {
    if (limit - at < 16u)
      return SM_FPKG_CORRUPT;
    uint32_t inode = read_le32(block + at);
    uint32_t name_length = read_le32(block + at + 8u);
    uint32_t entry_size = read_le32(block + at + 12u);
    if (entry_size == 0)
      return SM_FPKG_CORRUPT;
    if (name_length > limit - at - 16u ||
        entry_size < 16u + (uint64_t)name_length ||
        entry_size > limit - at || (uint64_t)inode >= dinode_count)
      return SM_FPKG_CORRUPT;
    if (name_length == wanted_length &&
        memcmp(block + at + 16u, wanted, wanted_length) == 0) {
      *inode_out = inode;
      return SM_FPKG_OK;
    }
    at += entry_size;
  }
  return SM_FPKG_CORRUPT;
}

/*
 * Validate the first outer PFS inode table and the required metadata entries.
 * Full block mapping and the inner PFS tree are handled by later reader phases.
 */
static sm_fpkg_status_t validate_outer_metadata(
    sm_pkg_read_fn read, void *ctx, uint64_t pfs_offset, uint64_t block_count,
    uint32_t superblock_index, const uint8_t *superblock) {
  uint64_t inode_count = read_le64(superblock + 0x30u);
  uint32_t table_block = read_le32(superblock + 0xD8u);
  uint64_t inode_capacity = SM_FPKG_OUTER_BLOCK_SIZE / SM_FPKG_DINODE_SIZE;
  uint32_t uroot_inode, image_inode, layout_inode;
  if (inode_count < 4u || inode_count > inode_capacity ||
      (uint64_t)table_block >= block_count || table_block == superblock_index)
    return SM_FPKG_CORRUPT;
  uint8_t *workspace = malloc(2u * SM_FPKG_OUTER_BLOCK_SIZE);
  if (!workspace)
    return SM_FPKG_NOMEM;
  uint8_t *inode_table = workspace;
  uint8_t *io_block = workspace + SM_FPKG_OUTER_BLOCK_SIZE;
  sm_fpkg_status_t result = SM_FPKG_CORRUPT;
  sm_fpkg_status_t status = read_outer_block(
      read, ctx, pfs_offset, block_count, table_block, inode_table);
  if (status != SM_FPKG_OK)
    goto done;
  status = outer_child(read, ctx, pfs_offset, block_count, inode_table,
                       inode_count, 0, "uroot", &uroot_inode, io_block);
  if (status != SM_FPKG_OK)
    goto done;
  status = outer_child(read, ctx, pfs_offset, block_count, inode_table,
                       inode_count, uroot_inode, "pfs_image.dat", &image_inode,
                       io_block);
  if (status != SM_FPKG_OK)
    goto done;
  status = outer_child(read, ctx, pfs_offset, block_count, inode_table,
                       inode_count, uroot_inode, "naps_pkg_layout.dat",
                       &layout_inode, io_block);
  if (status != SM_FPKG_OK || image_inode == layout_inode)
    goto done;
  const uint8_t *files[] = {
      inode_table + (size_t)image_inode * SM_FPKG_DINODE_SIZE,
      inode_table + (size_t)layout_inode * SM_FPKG_DINODE_SIZE};
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
    uint64_t file_size = read_le64(files[i] + 8u);
    uint32_t first_block;
    if (file_size == 0 || file_size > block_count * SM_FPKG_OUTER_BLOCK_SIZE ||
        !dinode_block(files[i], 0, &first_block))
      goto done;
    status = read_outer_block(read, ctx, pfs_offset, block_count, first_block,
                              io_block);
    if (status != SM_FPKG_OK)
      goto done;
  }
  result = SM_FPKG_OK;
done:
  free(workspace);
  return status != SM_FPKG_OK ? status : result;
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

  uint64_t block_count = pfs_size / SM_FPKG_OUTER_BLOCK_SIZE;
  uint64_t superblock_index = (superblock_offset - pfs_offset) /
                              SM_FPKG_OUTER_BLOCK_SIZE;
  if (block_count > UINT32_MAX || superblock_index > UINT32_MAX)
    return SM_FPKG_CORRUPT;
  sm_fpkg_status_t outer_status = validate_outer_metadata(
      read, ctx, pfs_offset, block_count, (uint32_t)superblock_index,
      superblock);
  if (outer_status != SM_FPKG_OK)
    return outer_status;

  return SM_FPKG_OK;
}
