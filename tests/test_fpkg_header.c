/* Synthetic envelope checks only; this is not a game FPKG compatibility fixture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sm_fpkg.h"

#define OUTER_BLOCK 0x10000u
#define CNT_OFFSET 0x20000u
#define FILE_SIZE (CNT_OFFSET + 0x1000u)

typedef struct {
  uint8_t bytes[FILE_SIZE];
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

static void valid_envelope(memory_pkg_t *pkg) {
  memset(pkg, 0, sizeof(*pkg));
  uint8_t *fih = pkg->bytes;
  memcpy(fih, "\x7f" "FIH", 4);
  fih[5] = 0;
  fih[6] = 3;
  put_le32(fih + 0x60, OUTER_BLOCK);
  put_le64(fih + 0x10, OUTER_BLOCK);
  put_le64(fih + 0x18, OUTER_BLOCK);
  put_le64(fih + 0x20, OUTER_BLOCK);
  put_le64(fih + 0x58, CNT_OFFSET);
  set_be32(pkg->bytes + CNT_OFFSET, 0x7F434E54u);
  put_le64(pkg->bytes + OUTER_BLOCK, 2);
  put_le64(pkg->bytes + OUTER_BLOCK + 8, 20130315u);
  memcpy(pkg->bytes + OUTER_BLOCK + 0x370, "PPRPLAIN-NOAUTH!", 16);
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

  puts("synthetic FIH/PFS eligibility boundaries passed");
  return 0;
}
