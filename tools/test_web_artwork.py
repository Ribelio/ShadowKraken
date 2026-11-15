#!/usr/bin/env python3
"""Exercise the production artwork resolver using synthetic directory titles."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/sm_api_service.c').read_text()
functions = []
for name in ('copy_regular_file_path', 'copy_source_artwork_path', 'resolve_game_artwork_path'):
    match = re.search(r'^static bool ' + name + r'\([^;{]*\)\s*\{.*?^\}', source, re.M | re.S)
    if not match:
        raise RuntimeError('missing ' + name)
    functions.append(match.group())
with tempfile.TemporaryDirectory(prefix='sk-artwork-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text('''#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#define MAX_PATH 1024
#define APPMETA_BASE "/nonexistent-shadowkraken-appmeta"
#define APP_BASE "/nonexistent-shadowkraken-app"
''' + '\n'.join(functions) + '''
int main(int argc, char **argv) {
  (void)argc;
  char dir[MAX_PATH], file[MAX_PATH], out[MAX_PATH];
  assert(snprintf(dir, sizeof(dir), "%s/sce_sys", argv[1]) > 0);
  assert(mkdir(dir, 0700) == 0);
  assert(snprintf(file, sizeof(file), "%s/pic1.png", dir) > 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  FILE *f = fopen(file, "wb"); assert(f); assert(fclose(f) == 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  f = fopen(file, "wb"); assert(f); assert(fwrite("png", 1, 3, f) == 3); assert(fclose(f) == 0);
  assert(resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  assert(strcmp(out, file) == 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "icon0.png", out));
  assert(unlink(file) == 0);
  char outside[MAX_PATH], outside_file[MAX_PATH];
  assert(snprintf(outside, sizeof(outside), "%s/outside", argv[1]) > 0);
  assert(mkdir(outside, 0700) == 0);
  assert(snprintf(outside_file, sizeof(outside_file), "%s/pic1.png", outside) > 0);
  f = fopen(outside_file, "wb"); assert(f); assert(fwrite("png", 1, 3, f) == 3); assert(fclose(f) == 0);
  assert(rmdir(dir) == 0);
  assert(symlink(outside, dir) == 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  assert(unlink(dir) == 0);
  assert(mkdir(dir, 0700) == 0);
  assert(snprintf(file, sizeof(file), "%s/pic1.png", dir) > 0);
  assert(symlink(outside_file, file) == 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  assert(unlink(file) == 0);
  assert(unlink(outside_file) == 0);
  assert(rmdir(outside) == 0);
  assert(!resolve_game_artwork_path("PPSA00001", argv[1], "pic1.png", out));
  puts("artwork resolver: ok");
}
''')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test'), str(tmp)], check=True)
