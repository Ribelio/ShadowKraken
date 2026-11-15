#!/usr/bin/env python3
"""Check FPKG storage source routing and the unpack guard."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/sm_api_service.c").read_text()
match = re.search(r'^static bool game_uses_fpkg_generation\([^;{]*\)\s*\{.*?^\}',
                  source, re.M | re.S)
assert match, "missing FPKG generation check"
source_resolver = re.search(
    r'^static bool resolve_fpkg_storage_source_path\([^;{]*\)\s*\{.*?^\}',
    source, re.M | re.S)
assert source_resolver, "missing FPKG backing-source resolver"
source_info_type = re.search(
    r'^typedef struct \{[^{}]*\} game_source_info_t;', source, re.M | re.S)
assert source_info_type, "missing storage source descriptor"
source_matcher = re.search(
    r'^static bool game_source_matches_storage_path\([^;{]*\)\s*\{.*?^\}',
    source, re.M | re.S)
assert source_matcher, "missing runtime/physical storage source matching"
assert source.count("operation == GAME_STORAGE_UNPACK && game_uses_fpkg_generation(title_id)") == 1
assert re.search(r'operation == GAME_STORAGE_UNPACK &&\s*game_uses_fpkg_generation\(title_id\)\)\s*status = ENOTSUP;', source)
assert 'source_info->runtime_path, SM_FPKG_CACHE_DEFAULT_ROOT' in source
assert 'source_info->source_type = "pkg";' in source
assert 'runtime_release_source = runtime_source;' in source
assert 'runtime_to_release = source.runtime_path;' in source
assert '!game_source_matches_storage_path(&info, physical_path)' in source
assert '!generation.source_matches_snapshot' in source_resolver.group()
assert 'sm_fpkg_cache_source_matches_remounted_image(&generation)' in source_resolver.group()
assert 'fpkg_runtime_source && (!S_ISREG(st.st_mode) || st.st_size <= 0)' in source
assert 'S_ISREG(source_stat.st_mode)' in source
assert 'operation != GAME_STORAGE_COPY' in source and 'return EROFS;' in source
assert 'paths_overlap(package_path, path)' in source
assert 'game.image_backed && game.can_unpack !== false' in (root / "web/index.html").read_text()
with tempfile.TemporaryDirectory(prefix="fpkg-unpack-") as directory:
    test = Path(directory) / "test.c"
    test.write_text('''#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define MAX_PATH 1024
#define SM_FPKG_CACHE_DEFAULT_ROOT "/data/fpkg-cache"
#define SM_FPKG_OK 0
typedef int sm_fpkg_status_t;
typedef struct {
  char source_path[MAX_PATH];
  bool source_matches_snapshot;
} sm_fpkg_cache_generation_info_t;
static const char *tracked;
static const char *generation_source;
static int generation_status = SM_FPKG_OK;
static bool generation_matches_snapshot = true;
static bool remounted_image_matches;
static bool read_mount_link(const char *id, char *out, size_t size) {
  (void)id;
  if (!tracked) return false;
  return snprintf(out, size, "%s", tracked) > 0;
}
static bool path_matches_root_or_child(const char *path, const char *root) {
  size_t n = strlen(root);
  return strncmp(path, root, n) == 0 && (path[n] == '\\0' || path[n] == '/');
}
static bool is_under_image_mount_base(const char *path) {
  return path && strncmp(path, "/mnt/image", 10) == 0;
}
static bool sm_fpkg_cache_source_matches_remounted_image(
    const sm_fpkg_cache_generation_info_t *info) {
  (void)info;
  return remounted_image_matches;
}
static sm_fpkg_status_t sm_fpkg_cache_read_generation_info(
    const char *root, const char *title_id, const char *generation_path,
    sm_fpkg_cache_generation_info_t *info) {
  (void)title_id;
  (void)generation_path;
  if (strcmp(root, SM_FPKG_CACHE_DEFAULT_ROOT) != 0 ||
      generation_status != SM_FPKG_OK)
    return generation_status ? generation_status : 1;
  if (snprintf(info->source_path, sizeof(info->source_path), "%s",
               generation_source ? generation_source : "") <= 0)
    return 1;
  info->source_matches_snapshot = generation_matches_snapshot;
  return SM_FPKG_OK;
}
''' + match.group() + '''
''' + source_resolver.group() + '''
''' + source_info_type.group() + '''
''' + source_matcher.group() + '''
int main(void) {
  assert(!game_uses_fpkg_generation("PPSA00001"));
  tracked = "/data/fpkg-cache/PPSA00001/gen-1";
  assert(game_uses_fpkg_generation("PPSA00001"));
  tracked = "/data/fpkg-cache-other/game";
  assert(!game_uses_fpkg_generation("PPSA00001"));
  tracked = "/mnt/images/game";
  assert(!game_uses_fpkg_generation("PPSA00001"));
  char source[MAX_PATH];
  generation_source = "/mnt/usb0/homebrew/PPSA00001.pkg";
  assert(resolve_fpkg_storage_source_path("PPSA00001",
      "/data/fpkg-cache/PPSA00001/gen-1", source));
  assert(strcmp(source, generation_source) == 0);
  generation_matches_snapshot = false;
  assert(!resolve_fpkg_storage_source_path("PPSA00001",
      "/data/fpkg-cache/PPSA00001/gen-1", source));
  generation_source = "/mnt/image0/PPSA00001.pkg";
  remounted_image_matches = true;
  assert(resolve_fpkg_storage_source_path("PPSA00001",
      "/data/fpkg-cache/PPSA00001/gen-1", source));
  remounted_image_matches = false;
  assert(!resolve_fpkg_storage_source_path("PPSA00001",
      "/data/fpkg-cache/PPSA00001/gen-1", source));
  generation_matches_snapshot = true;
  generation_source = "/mnt/usb0/homebrew/PPSA00001.pkg";
  generation_status = 2;
  errno = 0;
  assert(!resolve_fpkg_storage_source_path("PPSA00001",
      "/data/fpkg-cache/PPSA00001/gen-1", source));
  assert(errno == ESTALE);
  game_source_info_t source_info = {0};
  snprintf(source_info.physical_path, sizeof(source_info.physical_path),
           "%s", "/mnt/images/games.img");
  snprintf(source_info.runtime_path, sizeof(source_info.runtime_path),
           "%s", "/data/fpkg-cache/PPSA00001/gen-1");
  assert(game_source_matches_storage_path(&source_info,
      "/data/fpkg-cache/PPSA00001/gen-1"));
  assert(game_source_matches_storage_path(&source_info,
      "/mnt/images/games.img"));
  assert(!game_source_matches_storage_path(&source_info,
      "/data/fpkg-cache/PPSA00001/gen-2"));
  puts("FPKG storage source routing and unpack guard: ok");
}
''')
    binary = Path(directory) / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
