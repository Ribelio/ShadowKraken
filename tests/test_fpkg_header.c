/* Synthetic envelope checks only; this is not a game FPKG compatibility fixture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sm_fpkg.h"

#define OUTER_BLOCK 0x10000u
#define OUTER_BLOCK_COUNT 6u
#define PFS_SIZE (OUTER_BLOCK * OUTER_BLOCK_COUNT)
#define PFS_END (OUTER_BLOCK + PFS_SIZE)
#define CNT_OFFSET PFS_END
#define FILE_SIZE (CNT_OFFSET + 0x1000u)
#define DINODE_SIZE 0x2C8u

typedef struct {
  uint8_t bytes[FILE_SIZE];
  uint64_t fail_offset;
  int fail_read;
} memory_pkg_t;

static void put_le32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(v >> (i * 8));
}

static void put_le64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(v >> (i * 8));
}

static int memory_read(void *ctx, uint64_t offset, void *out, size_t size) {
  memory_pkg_t *pkg = ctx;
  if (pkg->fail_read && offset == pkg->fail_offset)
    return 5;
  if (offset > sizeof(pkg->bytes) || size > sizeof(pkg->bytes) - offset)
    return 5;
  memcpy(out, pkg->bytes + offset, size);
  return 0;
}

static void set_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void make_dirent(uint8_t *out, uint32_t inode, const char *name) {
  size_t name_length = strlen(name);
  put_le32(out, inode);
  put_le32(out + 8, (uint32_t)name_length);
  put_le32(out + 12, (uint32_t)(16u + name_length));
  memcpy(out + 16, name, name_length);
}

static void set_dinode(uint8_t *out, uint64_t size, uint32_t first_block) {
  put_le64(out + 8, size);
  put_le32(out + 0x64 + 32, first_block);
}

static void valid_envelope(memory_pkg_t *pkg) {
  memset(pkg, 0, sizeof(*pkg));
  uint8_t *fih = pkg->bytes;
  memcpy(fih, "\x7f" "FIH", 4);
  fih[5] = 0;
  fih[6] = 3;
  put_le32(fih + 0x60, OUTER_BLOCK);
  put_le64(fih + 0x10, OUTER_BLOCK);
  put_le64(fih + 0x18, PFS_SIZE);
  put_le64(fih + 0x20, OUTER_BLOCK);
  put_le64(fih + 0x58, CNT_OFFSET);
  set_be32(pkg->bytes + CNT_OFFSET, 0x7F434E54u);
  put_le64(pkg->bytes + OUTER_BLOCK, 2);
  put_le64(pkg->bytes + OUTER_BLOCK + 8, 20130315u);
  memcpy(pkg->bytes + OUTER_BLOCK + 0x370, "PPRPLAIN-NOAUTH!", 16);
  put_le64(pkg->bytes + OUTER_BLOCK + 0x30, 4);
  put_le32(pkg->bytes + OUTER_BLOCK + 0xD8, 1);

  uint8_t *inodes = pkg->bytes + 2u * OUTER_BLOCK;
  set_dinode(inodes + 0u * DINODE_SIZE, 21, 2);
  set_dinode(inodes + 1u * DINODE_SIZE, 64, 3);
  set_dinode(inodes + 2u * DINODE_SIZE, 1, 4);
  set_dinode(inodes + 3u * DINODE_SIZE, 1, 5);

  make_dirent(pkg->bytes + 3u * OUTER_BLOCK, 1, "uroot");
  uint8_t *uroot = pkg->bytes + 4u * OUTER_BLOCK;
  make_dirent(uroot, 2, "pfs_image.dat");
  make_dirent(uroot + 29, 3, "naps_pkg_layout.dat");
}

static sm_pkg_info_t metadata(void) {
  sm_pkg_info_t info;
  memset(&info, 0, sizeof(info));
  strcpy(info.title_id, "PPSA00001");
  info.pkg_kind = PKG_KIND_BASE;
  info.pkg_classification_confident = true;
  return info;
}

int main(void) {
  memory_pkg_t pkg;
  sm_pkg_info_t info = metadata();
  valid_envelope(&pkg);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_OK);

  pkg.bytes[5] = 1;
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_UNSUPPORTED);
  valid_envelope(&pkg);
  memcpy(pkg.bytes + OUTER_BLOCK + 0x370, "NOT-PLAINTEXT!!", 16);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_UNSUPPORTED);
  valid_envelope(&pkg);
  memcpy(pkg.bytes, "\x7f" "LIH", 4);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_UNSUPPORTED);
  valid_envelope(&pkg);
  put_le64(pkg.bytes + 0x18, UINT64_MAX);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  put_le64(pkg.bytes + OUTER_BLOCK + 0x30, UINT64_MAX);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  pkg.bytes[3u * OUTER_BLOCK + 16u] = 'x';
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  put_le32(pkg.bytes + 3u * OUTER_BLOCK + 12u, 15);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  put_le32(pkg.bytes + 3u * OUTER_BLOCK, 4);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  pkg.fail_read = 1;
  pkg.fail_offset = OUTER_BLOCK;
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_IO);

  puts("synthetic FIH/PFS eligibility and malformed-input boundaries passed");
  return 0;
}
