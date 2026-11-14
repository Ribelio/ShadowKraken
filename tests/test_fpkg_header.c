/* Synthetic envelope checks only; this is not a game FPKG compatibility fixture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sm_fpkg.h"

#define OUTER_BLOCK 0x10000u
#define OUTER_BLOCK_COUNT 24u
#define PFS_SIZE (OUTER_BLOCK * OUTER_BLOCK_COUNT)
#define PFS_END (OUTER_BLOCK + PFS_SIZE)
#define CNT_OFFSET PFS_END
#define FILE_SIZE (CNT_OFFSET + 0x2000u)
#define DINODE_SIZE 0x2C8u
#define DESCRIPTOR_SIZE 95u
#define DESCRIPTOR_RECORD_AT 59u

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

static void put_bits(uint8_t *record, unsigned low, unsigned width,
                     uint64_t value) {
  for (unsigned i = 0; i < width; ++i) {
    if ((value >> i) & 1u)
      record[(low + i) / 8u] |= (uint8_t)(1u << ((low + i) % 8u));
  }
}

static void put_boundary(uint8_t *p, uint64_t value, uint8_t kind) {
  for (unsigned i = 0; i < 5; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
  p[5] = kind;
}

static void make_layout_descriptor(uint8_t *bytes) {
  memset(bytes, 0, DESCRIPTOR_SIZE);
  put_le64(bytes, (UINT64_C(2) << 24) | 3u | (UINT64_C(1) << 32));
  put_le64(bytes + 8, UINT64_C(2) << 24);
  put_boundary(bytes + 16, 0, 0);
  put_boundary(bytes + 22, 0x40000u, 0);
  put_boundary(bytes + 28, 0x40000u, 0);
  put_boundary(bytes + 34, 0x40000u, 0x40);
  bytes[DESCRIPTOR_RECORD_AT - 1u] = 4;
  uint8_t *block = bytes + DESCRIPTOR_RECORD_AT + 9u;
  put_bits(block, 14, 17, (1u << 17) - 1u);
  put_bits(block, 66, 1, 1);
  put_bits(block, 68, 4, 4);
  put_bits(bytes + DESCRIPTOR_RECORD_AT + 18u, 26, 22, 1);
  put_bits(bytes + DESCRIPTOR_RECORD_AT + 18u, 67, 1, 1);
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
  set_dinode(inodes + 2u * DINODE_SIZE, 4u * OUTER_BLOCK, 4);
  put_le32(inodes + 2u * DINODE_SIZE + 0x64u + 36u + 32u, 6);
  put_le32(inodes + 2u * DINODE_SIZE + 0x64u + 72u + 32u, 7);
  put_le32(inodes + 2u * DINODE_SIZE + 0x64u + 108u + 32u, 8);
  set_dinode(inodes + 3u * DINODE_SIZE, DESCRIPTOR_SIZE, 5);

  make_dirent(pkg->bytes + 3u * OUTER_BLOCK, 1, "uroot");
  uint8_t *uroot = pkg->bytes + 4u * OUTER_BLOCK;
  make_dirent(uroot, 2, "pfs_image.dat");
  make_dirent(uroot + 29, 3, "naps_pkg_layout.dat");
  make_layout_descriptor(pkg->bytes + 6u * OUTER_BLOCK);
}

static void set_indirect_image_file(memory_pkg_t *pkg, uint32_t indirect_data) {
  uint8_t *dinode = pkg->bytes + 2u * OUTER_BLOCK + DINODE_SIZE * 2u;
  put_le64(dinode + 8, 13u * OUTER_BLOCK);
  for (unsigned i = 0; i < 12; ++i)
    put_le32(dinode + 0x64u + i * 36u + 32u, 8u + i);
  put_le32(dinode + 0x64u + 12u * 36u + 32u, 20);
  put_le32(pkg->bytes + 21u * OUTER_BLOCK + 32u, indirect_data);
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
  valid_envelope(&pkg);
  set_be32(pkg.bytes + CNT_OFFSET + 0x10u, 4097);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  set_be32(pkg.bytes + CNT_OFFSET + 0x10u, 1);
  set_be32(pkg.bytes + CNT_OFFSET + 0x18u, 0x1000);
  set_be32(pkg.bytes + CNT_OFFSET + 0x1000u + 16u, 0x2000);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_OK);
  set_be32(pkg.bytes + CNT_OFFSET + 0x1000u + 20u, 1);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);
  valid_envelope(&pkg);
  set_indirect_image_file(&pkg, 21);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_OK);
  valid_envelope(&pkg);
  set_indirect_image_file(&pkg, 8);
  assert(sm_fpkg_probe_stream("game.pkg", sizeof(pkg.bytes), memory_read,
                              &pkg, &info) == SM_FPKG_CORRUPT);

  puts("synthetic FIH/PFS descriptor mapping and malformed-input checks passed");
  return 0;
}
