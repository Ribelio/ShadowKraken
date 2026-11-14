/* Host integration smoke check: production SMP functions, platform calls mocked. */
#include "host_platform.h"
#include "sm_appdb.h"
#include "sm_config_mount.h"
#include "sm_fpkg_cache.h"
#include "sm_filesystem.h"
#include "sm_image_cache.h"
#include "sm_install.h"
#include "sm_l10n.h"
#include "sm_limits.h"
#include "sm_log.h"
#include "sm_path_utils.h"
#include "sm_paths.h"
#include "sm_runtime.h"
#include "sm_shellcore_hooks.h"
#include "sm_title_state.h"
#include <strings.h>

#ifndef MNT_RDONLY
#define MNT_RDONLY 0x00000001
#endif

typedef int (*app_install_title_dir_fn_t)(const char *title_id,
                                          const char *install_path,
                                          void *reserved);
typedef struct {
  const char *label;
} fakelib_layer_t;
typedef struct {
  char title_id[MAX_TITLE_ID];
  char sandbox_app0_path[MAX_PATH];
  char fpkg_source_directory[MAX_PATH];
  fakelib_layer_t layers[8];
  size_t layer_count;
} fakelib_session_t;

static const char *const k_source =
    "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test";
static const char *const k_title = "PPSA00001";
static char events[16][32];
static size_t event_count;
static bool fail_mount_link;
static bool bridge_enabled;
static bool fail_bridge;
static int api_result;
static int api_calls;
static int bridge_calls;
static char api_title[MAX_TITLE_ID];
static char api_install_path[MAX_PATH];
static char sandbox_target[MAX_PATH];
static char sandbox_source[MAX_PATH];
static char sandbox_label[32];
static int sandbox_flags;
static int sandbox_calls;
static fakelib_session_t g_fakelib_mount;
static bool fpkg_feature_enabled;
static sm_fpkg_status_t generation_info_status;
static sm_fpkg_cache_generation_info_t generation_info;
static unsigned generation_info_calls;

static bool copy_sce_sys_to_appmeta(const char *src, const char *dst);
static bool update_trophy_metadata(const char *title_id,
                                   const char *src_sce_sys);
static void update_registered_title_snd0info(const char *title_id);
bool mount_sandbox_directory_at(const char *title_id, const char *app0_path,
                                const char *target_suffix,
                                const char *source_path, const char *label,
                                int flags);

static void record_event(const char *event) {
  assert(event_count < sizeof(events) / sizeof(events[0]));
  (void)strlcpy(events[event_count++], event, sizeof(events[0]));
}

static void reset_events(void) {
  event_count = 0;
  memset(events, 0, sizeof(events));
  api_calls = 0;
  bridge_calls = 0;
}

static int fake_install_title_dir(const char *title_id,
                                  const char *install_path,
                                  void *reserved) {
  assert(reserved == NULL);
  ++api_calls;
  (void)strlcpy(api_title, title_id, sizeof(api_title));
  (void)strlcpy(api_install_path, install_path, sizeof(api_install_path));
  record_event("AppInstallTitleDir");
  return api_result;
}

static app_install_title_dir_fn_t resolve_app_install_title_dir(void) {
  return fake_install_title_dir;
}

static int test_mkdir(const char *path, mode_t mode) {
  (void)path;
  (void)mode;
  return 0;
}

static int test_unlink(const char *path) {
  (void)path;
  errno = ENOENT;
  return -1;
}

#define mkdir test_mkdir
#define unlink test_unlink
#include "install_lifecycle.inc"
#undef mkdir
#undef unlink

bool is_under_image_mount_base(const char *path) {
  (void)path;
  return false;
}
size_t resolve_image_source_chain_from_mount_cache(
    const char *path, char chain[MAX_IMAGE_CHAIN_DEPTH][MAX_PATH]) {
  (void)path;
  (void)chain;
  return 0;
}
bool resolve_sce_sys_file(const char *root, const char *relative,
                          char out[MAX_PATH]) {
  int written = snprintf(out, MAX_PATH, "%s/%s", root, relative);
  return written >= 0 && written < MAX_PATH;
}
bool has_appmeta_data(const char *title_id) {
  (void)title_id;
  return true;
}
bool should_stop_requested(void) { return false; }
bool runtime_sleep_mode_active(void) { return false; }
int copy_sce_sys_dir(const char *src, const char *dst) {
  (void)src;
  (void)dst;
  return 0;
}
static bool copy_sce_sys_to_appmeta(const char *src, const char *dst) {
  (void)src;
  (void)dst;
  return true;
}
int copy_file(const char *src, const char *dst) {
  (void)src;
  (void)dst;
  return 0;
}
static bool update_trophy_metadata(const char *title_id,
                                   const char *src_sce_sys) {
  (void)title_id;
  (void)src_sce_sys;
  return true;
}
bool mount_title_nullfs(const char *title_id, const char *src_path) {
  assert(strcmp(title_id, k_title) == 0);
  assert(strcmp(src_path, k_source) == 0);
  record_event("nullfs mount");
  return true;
}
void runtime_mount_state_lock(void) {}
void runtime_mount_state_unlock(void) {}
bool rollback_title_nullfs_mount(const char *title_id, const char *src_path) {
  assert(strcmp(title_id, k_title) == 0);
  assert(strcmp(src_path, k_source) == 0);
  record_event("nullfs rollback");
  return true;
}
bool write_mount_link(const char *title_id, const char *src_path) {
  assert(strcmp(title_id, k_title) == 0);
  assert(strcmp(src_path, k_source) == 0);
  record_event("mount.lnk");
  if (fail_mount_link) {
    errno = EIO;
    return false;
  }
  return true;
}
bool write_mount_image_chain(
    const char *title_id,
    const char paths[MAX_IMAGE_CHAIN_DEPTH][MAX_PATH], size_t count) {
  (void)title_id;
  (void)paths;
  (void)count;
  return true;
}
bool cache_image_source_mapping(const char *path, const char *mount_point) {
  (void)path;
  (void)mount_point;
  return true;
}
int update_snd0info(const char *title_id) {
  (void)title_id;
  return 0;
}
bool path_exists(const char *path) {
  (void)path;
  return false;
}
bool sm_shellcore_install_bridge_enabled(void) { return bridge_enabled; }
bool sm_shellcore_install_title_dir(const char *title_id,
                                    const char *install_dir,
                                    int *result_out) {
  ++bridge_calls;
  (void)strlcpy(api_title, title_id, sizeof(api_title));
  (void)strlcpy(api_install_path, install_dir, sizeof(api_install_path));
  record_event("ShellCore TitleDir bridge");
  if (fail_bridge)
    return false;
  *result_out = api_result;
  return true;
}
bool sm_config_fpkg_direct_mount_enabled(void) {
  return fpkg_feature_enabled;
}
sm_fpkg_status_t sm_fpkg_cache_read_generation_info(
    const char *cache_root, const char *title_id, const char *generation_path,
    sm_fpkg_cache_generation_info_t *info_out) {
  assert(strcmp(cache_root, SM_FPKG_CACHE_DEFAULT_ROOT) == 0);
  assert(strcmp(title_id, k_title) == 0);
  assert(strcmp(generation_path,
                "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test")
         == 0);
  ++generation_info_calls;
  if (generation_info_status == SM_FPKG_OK)
    *info_out = generation_info;
  return generation_info_status;
}
void mark_register_attempted(const char *title_id) { (void)title_id; }
void invalidate_app_db_title_cache(void) {}
void clear_register_attempts(const char *title_id) { (void)title_id; }
void notify_game_installed_rich(const char *title_id) { (void)title_id; }
static void update_registered_title_snd0info(const char *title_id) {
  (void)title_id;
}
void notify_system_l10n(sm_l10n_key_t key, ...) { (void)key; }
void log_debug(const char *format, ...) { (void)format; }
bool mount_sandbox_directory_at(const char *title_id, const char *app0_path,
                                const char *target_suffix,
                                const char *source_path, const char *label,
                                int flags) {
  assert(strcmp(title_id, k_title) == 0);
  assert(strcmp(app0_path, "/mnt/sandbox/PPSA00001_000/app0") == 0);
  ++sandbox_calls;
  (void)strlcpy(sandbox_target, target_suffix, sizeof(sandbox_target));
  (void)strlcpy(sandbox_source, source_path, sizeof(sandbox_source));
  (void)strlcpy(sandbox_label, label, sizeof(sandbox_label));
  sandbox_flags = flags;
  return true;
}

static void test_title_dir_registration_routes(void) {
  int result = 123;
  bridge_enabled = false;
  api_result = (int)0x80990002u;
  reset_events();
  assert(sm_install_register_title_dir(k_title, APP_BASE "/", &result));
  assert(result == api_result && api_calls == 1 && bridge_calls == 0);
  assert(strcmp(api_title, k_title) == 0);
  assert(strcmp(api_install_path, APP_BASE "/") == 0);

  bridge_enabled = true;
  api_result = -42;
  result = 0;
  reset_events();
  assert(sm_install_register_title_dir(k_title, APP_BASE "/", &result));
  assert(result == -42 && bridge_calls == 1 && api_calls == 0);
  assert(strcmp(events[0], "ShellCore TitleDir bridge") == 0);

  fail_bridge = true;
  assert(!sm_install_register_title_dir(k_title, APP_BASE "/", &result));
  assert(sm_install_register_title_dir(NULL, APP_BASE "/", &result) == false);
  fail_bridge = false;
}

static void test_install_publication_and_registration_order(void) {
  reset_events();
  bridge_enabled = false;
  api_result = 0;
  fail_mount_link = false;
  assert(mount_and_install(k_source, k_title, "Synthetic FPKG", false, true,
                           false, NULL));
  assert(event_count == 3);
  assert(strcmp(events[0], "nullfs mount") == 0);
  assert(strcmp(events[1], "mount.lnk") == 0);
  assert(strcmp(events[2], "AppInstallTitleDir") == 0);
  assert(api_calls == 1 && bridge_calls == 0);
  assert(strcmp(api_title, k_title) == 0);
  assert(strcmp(api_install_path, APP_BASE "/") == 0);

  reset_events();
  fail_mount_link = true;
  assert(!mount_and_install(k_source, k_title, "Synthetic FPKG", false, true,
                            false, NULL));
  assert(event_count == 3);
  assert(strcmp(events[0], "nullfs mount") == 0);
  assert(strcmp(events[1], "mount.lnk") == 0);
  assert(strcmp(events[2], "nullfs rollback") == 0);
  assert(api_calls == 0 && bridge_calls == 0);
  fail_mount_link = false;

  reset_events();
  assert(mount_and_install(k_source, k_title, "Synthetic FPKG", false, true,
                           true, NULL));
  assert(event_count == 2 && api_calls == 0 && bridge_calls == 0);
  assert(strcmp(events[0], "nullfs mount") == 0);
  assert(strcmp(events[1], "mount.lnk") == 0);

  reset_events();
  assert(mount_and_install(k_source, k_title, "Synthetic FPKG", true, false,
                           false, NULL));
  assert(event_count == 2 && api_calls == 0 && bridge_calls == 0);

  reset_events();
  api_result = (int)0x80990002u;
  assert(mount_and_install(k_source, k_title, "Synthetic FPKG", true, true,
                           false, NULL));
  assert(event_count == 3 && api_calls == 1);
  assert(strcmp(events[2], "AppInstallTitleDir") == 0);

  reset_events();
  api_result = -1;
  assert(!mount_and_install(k_source, k_title, "Synthetic FPKG", false, true,
                            false, NULL));
  assert(event_count == 3 && api_calls == 1);
  assert(strcmp(events[2], "AppInstallTitleDir") == 0);

  bool batch_snd0 = true;
  reset_events();
  api_result = 0;
  assert(mount_and_install(k_source, k_title, "Synthetic FPKG", false, true,
                           true, &batch_snd0));
  assert(!batch_snd0);
}

static void test_fpkg_sandbox_mount_route(void) {
  sandbox_calls = 0;
  memset(&g_fakelib_mount, 0, sizeof(g_fakelib_mount));
  assert(mount_fpkg_source_if_needed());
  assert(sandbox_calls == 0);

  (void)strlcpy(g_fakelib_mount.title_id, k_title,
                sizeof(g_fakelib_mount.title_id));
  (void)strlcpy(g_fakelib_mount.sandbox_app0_path,
                "/mnt/sandbox/PPSA00001_000/app0",
                sizeof(g_fakelib_mount.sandbox_app0_path));
  (void)strlcpy(g_fakelib_mount.fpkg_source_directory,
                "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test",
                sizeof(g_fakelib_mount.fpkg_source_directory));
  assert(mount_fpkg_source_if_needed());
  assert(sandbox_calls == 1);

  g_fakelib_mount.layers[0].label = "fpkg";
  g_fakelib_mount.layer_count = 1;
  assert(mount_fpkg_source_if_needed());
  assert(sandbox_calls == 1);

  assert(mount_fpkg_source_directory(
      k_title, "/mnt/sandbox/PPSA00001_000/app0",
      "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test"));
  assert(sandbox_calls == 2);
  assert(strcmp(sandbox_target, "/app0/kraken") == 0);
  assert(strcmp(sandbox_source,
                "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test")
         == 0);
  assert(strcmp(sandbox_label, "fpkg") == 0);
  assert(sandbox_flags == MNT_RDONLY);
}

static void test_fpkg_source_parent_resolution(void) {
  char temporary[] = "/private/tmp/sm-fpkg-source-parent.XXXXXX";
  if (access("/private/tmp", W_OK) != 0)
    (void)strlcpy(temporary, "/tmp/sm-fpkg-source-parent.XXXXXX",
                  sizeof(temporary));
  char *parent = mkdtemp(temporary);
  assert(parent != NULL);
  char source_path[MAX_PATH];
  int length = snprintf(source_path, sizeof(source_path), "%s/source.PKG",
                        parent);
  assert(length > 0 && (size_t)length < sizeof(source_path));

  const char *generation =
      "/data/shadowmount/fpkg-cache/PPSA00001/generation-host-test";
  char directory[MAX_PATH];
  fpkg_feature_enabled = false;
  generation_info_calls = 0;
  errno = 0;
  assert(!resolve_fpkg_source_directory(k_title, generation, directory));
  assert(errno == ENOTSUP && directory[0] == '\0' &&
         generation_info_calls == 0);

  fpkg_feature_enabled = true;
  generation_info_status = SM_FPKG_OK;
  generation_info = (sm_fpkg_cache_generation_info_t){0};
  (void)strlcpy(generation_info.source_path, source_path,
                sizeof(generation_info.source_path));
  generation_info.source_matches_snapshot = false;
  errno = 0;
  assert(!resolve_fpkg_source_directory(k_title, generation, directory));
  assert(errno == ESTALE && directory[0] == '\0' &&
         generation_info_calls == 1);

  generation_info.source_matches_snapshot = true;
  assert(resolve_fpkg_source_directory(k_title, generation, directory));
  assert(strcmp(directory, parent) == 0);

  length = snprintf(generation_info.source_path,
                    sizeof(generation_info.source_path),
                    "%s/missing/source.pkg", parent);
  assert(length > 0 && (size_t)length < sizeof(generation_info.source_path));
  errno = 0;
  assert(!resolve_fpkg_source_directory(k_title, generation, directory));
  assert(errno == ENOENT && directory[0] == '\0');
  assert(rmdir(parent) == 0);
}

int main(void) {
  test_title_dir_registration_routes();
  test_install_publication_and_registration_order();
  test_fpkg_sandbox_mount_route();
  test_fpkg_source_parent_resolution();
  puts("FPKG lifecycle routing: opt-in source resolution, registration, rollback, layer reuse, and read-only sandbox target passed");
  return 0;
}
