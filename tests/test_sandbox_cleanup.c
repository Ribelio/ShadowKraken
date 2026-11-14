#include "host_platform.h"
#include "sm_limits.h"
#ifdef __APPLE__
#include "sandbox_layer.inc"
#define MNT_BYFSID 0x08000000
#else
#include "sandbox_layer.inc"
#define MNT_BYFSID 0x08000000
struct statfs {
  char f_mntonname[88], f_mntfromname[88], f_fstypename[16];
  fsid_t f_fsid;
};
#endif
#define SANDBOX_USB_ROOT_COUNT 8u
#define SANDBOX_EXT_ROOT_COUNT 2u
static struct statfs mounts[2];
static int mount_count, lookup_error, snapshot_error, unmount_error;
static unsigned unmount_calls, removed_dirs;
static struct {
  fakelib_layer_t layers[8u + SANDBOX_USB_ROOT_COUNT + SANDBOX_EXT_ROOT_COUNT];
  size_t layer_count;
  uint64_t cleanup_deadline_us;
} g_fakelib_mount;

static void log_debug(const char *format, ...) { (void)format; }
static void sm_mount_diag_log_busy(const char *path) { (void)path; }
static bool path_matches_root_or_child(const char *path, const char *root) {
  size_t length = strlen(root);
  return strncmp(path, root, length) == 0 &&
         (path[length] == '\0' || path[length] == '/');
}
static int sm_mount_table_snapshot(struct statfs **out) {
  if (snapshot_error) { errno = snapshot_error; return -1; }
  *out = malloc(sizeof(mounts));
  assert(*out);
  memcpy(*out, mounts, sizeof(mounts));
  return mount_count;
}
int statfs(const char *path, struct statfs *out) {
  (void)path; (void)out; errno = lookup_error; return -1;
}
int unmount(const char *path, int flags) {
  assert(flags == MNT_BYFSID && !strcmp(path, "FSID:11:22"));
  ++unmount_calls;
  if (unmount_error) { errno = unmount_error; return -1; }
  return 0;
}
static int test_rmdir(const char *path) {
  (void)path; ++removed_dirs; return 0;
}
#define rmdir test_rmdir
#include "sandbox_cleanup.inc"
#undef rmdir

static fakelib_layer_t reset_layer(const char *source, bool overlay) {
  memset(mounts, 0, sizeof(mounts));
  mount_count = 1;
  lookup_error = EPIPE;
  snapshot_error = unmount_error = 0;
  unmount_calls = removed_dirs = 0;
  fakelib_layer_t layer = {.owned_mount = true, .mount_identity_valid = true,
                           .created_mount_path = true, .overlay = overlay};
  layer.label = overlay ? "game" : "directory";
  strlcpy(layer.source_path, source, sizeof(layer.source_path));
  strlcpy(layer.mount_path, "/mnt/sandbox/PPSA12345_000/data", sizeof(layer.mount_path));
  int32_t ids[2] = {11, 22};
  memcpy(&layer.mount_fsid, ids, sizeof(ids));
  mounts[0].f_fsid = layer.mount_fsid;
  strlcpy(mounts[0].f_mntonname, layer.mount_path, sizeof(mounts[0].f_mntonname));
  strlcpy(mounts[0].f_mntfromname, source, sizeof(mounts[0].f_mntfromname));
  strlcpy(mounts[0].f_fstypename, overlay ? "unionfs" : "nullfs", sizeof(mounts[0].f_fstypename));
  return layer;
}

int main(void) {
  const char *sources[] = {"/data", "/mnt/usb0", "/preinst/common/font", "/data/game/fakelib"};
  for (size_t i = 0; i < 4; ++i) {
    for (unsigned error = 0; error < 2; ++error) {
      fakelib_layer_t layer = reset_layer(sources[i], i == 3);
      lookup_error = error ? EAGAIN : EPIPE;
      assert(unmount_sandbox_layer(&layer));
      assert(unmount_calls == 1 && !layer.owned_mount && !layer.source_path[0]);
      layer = reset_layer(sources[i], i == 3);
      mount_count = 0;
      assert(unmount_sandbox_layer(&layer));
      assert(!unmount_calls && !removed_dirs && !layer.source_path[0]);
    }
  }
  fakelib_layer_t layer = reset_layer("/data", false);
  strlcpy(mounts[0].f_mntfromname, "/user/data", sizeof(mounts[0].f_mntfromname));
  assert(unmount_sandbox_layer(&layer) && unmount_calls == 1);

  layer = reset_layer("/mnt/usb0/games", false);
  layer.label = "fpkg";
  assert(unmount_sandbox_layer(&layer) && unmount_calls == 1);

  layer = reset_layer("/mnt/usb0/games", false);
  layer.label = "fpkg";
  g_fakelib_mount.layers[0] = layer;
  g_fakelib_mount.layer_count = 1;
  assert(remove_fpkg_sandbox_layers_for_root("/mnt/usb0", false));
  assert(g_fakelib_mount.layer_count == 0 && unmount_calls == 1);

  layer = reset_layer("/mnt/usb0/games", false);
  layer.label = "fpkg";
  layer.owned_mount = false;
  g_fakelib_mount.layers[0] = layer;
  g_fakelib_mount.layer_count = 1;
  errno = 0;
  assert(!remove_fpkg_sandbox_layers_for_root("/mnt/usb0", false));
  assert(errno == EBUSY && g_fakelib_mount.layer_count == 1 &&
         unmount_calls == 0);

  layer = reset_layer("/data", false);
  memset(&mounts[0].f_fsid, 0, sizeof(mounts[0].f_fsid));
  assert(unmount_sandbox_layer(&layer) && !unmount_calls && !removed_dirs);

  for (unsigned conflict = 0; conflict < 4; ++conflict) {
    layer = reset_layer("/data", false);
    if (conflict == 0) { mounts[1] = mounts[0]; memset(&mounts[1].f_fsid, 0, sizeof(fsid_t)); mount_count = 2; }
    if (conflict == 1) strlcpy(mounts[0].f_mntfromname, "/other", sizeof(mounts[0].f_mntfromname));
    if (conflict == 2) strlcpy(mounts[0].f_mntonname, "/other", sizeof(mounts[0].f_mntonname));
    if (conflict == 3) strlcpy(mounts[0].f_fstypename, "otherfs", sizeof(mounts[0].f_fstypename));
    assert(!unmount_sandbox_layer(&layer) && errno == EBUSY);
    assert(!unmount_calls && layer.owned_mount && layer.source_path[0]);
  }
  layer = reset_layer("/data", false);
  snapshot_error = EIO;
  assert(!unmount_sandbox_layer(&layer) && errno == EIO && !unmount_calls);
  layer = reset_layer("/data", false);
  unmount_error = EBUSY;
  assert(!unmount_sandbox_layer(&layer) && errno == EBUSY && layer.owned_mount);
  layer = reset_layer("/data", false);
  layer.mount_identity_valid = false;
  assert(!unmount_sandbox_layer(&layer) && errno == EPIPE && !unmount_calls);
  layer = reset_layer("/data", false);
  layer.owned_mount = false;
  assert(unmount_sandbox_layer(&layer) && !unmount_calls);
  puts("Sandbox cleanup: stale identities recover; FPKG nullfs cleanup is ownership-aware, and borrowed layers, overmounts and real failures remain protected");
  return 0;
}
