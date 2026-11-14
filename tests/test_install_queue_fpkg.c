/* Async registration must finish before an old FPKG cache is reclaimed. */
#include "host_platform.h"

#include "sm_appdb.h"
#include "sm_config_mount.h"
#include "sm_game_cache.h"
#include "sm_install.h"
#include "sm_install_queue.h"
#include "sm_l10n.h"
#include "sm_log.h"
#include "sm_manual.h"
#include "sm_runtime.h"
#include "sm_time.h"
#include "sm_title_state.h"

static runtime_config_t test_config = {.app_install_all_enabled = true};
static bool app_db_has_title;
static bool cleanup_allowed = true;
static unsigned cleanup_calls;
static uint64_t fake_clock = 100;
static char cached_path[MAX_PATH];

runtime_config_t runtime_config(void) { return test_config; }
uint64_t monotonic_time_us(void) { return ++fake_clock; }
bool runtime_sleep_mode_active(void) { return false; }

bool get_app_db_title_list_cached(struct AppDbTitleList *list_out) {
  (void)list_out;
  return true;
}
bool app_db_title_list_contains(const struct AppDbTitleList *list,
                                const char *title_id) {
  (void)list;
  return app_db_has_title && strcmp(title_id, "PPSA00001") == 0;
}
void free_app_db_title_list(struct AppDbTitleList *list) { (void)list; }
void invalidate_app_db_title_cache(void) {}

void log_debug(const char *format, ...) { (void)format; }
void notify_system_info(const char *format, ...) { (void)format; }
void notify_system_l10n(sm_l10n_key_t id, ...) { (void)id; }
const char *sm_l10n_get(sm_l10n_key_t id) {
  (void)id;
  return "queued %d";
}
void notify_game_installed_rich(const char *title_id) {
  assert(strcmp(title_id, "PPSA00001") == 0);
}
int sceAppInstUtilAppInstallAll(void *reserved) {
  (void)reserved;
  return 0;
}

void cache_game_entry(const char *path, const char *title_id,
                      const char *title_name) {
  assert(strcmp(title_id, "PPSA00001") == 0);
  assert(strcmp(title_name, "Synthetic Game") == 0);
  (void)strlcpy(cached_path, path, sizeof(cached_path));
}
bool release_scan_runtime_mounts(void) { return true; }
void clear_register_attempts(const char *title_id) { (void)title_id; }
void clear_failed_mount_attempts(const char *title_id) { (void)title_id; }
uint8_t bump_failed_mount_attempts(const char *title_id) {
  (void)title_id;
  return 1;
}
uint8_t get_failed_mount_attempts(const char *title_id) {
  (void)title_id;
  return 0;
}
int update_snd0info(const char *title_id) {
  (void)title_id;
  return 0;
}
void sm_manual_note_installed(const char *source_path, const char *title_id,
                              const char *title_name) {
  (void)source_path;
  (void)title_id;
  (void)title_name;
}

bool sm_install_release_replaced_fpkg_generation(
    const char *title_id, const char *previous_path, const char *current_path) {
  assert(strcmp(title_id, "PPSA00001") == 0);
  assert(strcmp(previous_path, "gen-old") == 0);
  assert(strcmp(current_path,
                "/cache/PPSA00001/gen-new") == 0);
  ++cleanup_calls;
  return cleanup_allowed;
}

int main(void) {
  scan_candidate_t candidate = {0};
  (void)strlcpy(candidate.path, "/cache/PPSA00001/gen-new",
                sizeof(candidate.path));
  (void)strlcpy(candidate.title_id, "PPSA00001",
                sizeof(candidate.title_id));
  (void)strlcpy(candidate.title_name, "Synthetic Game",
                sizeof(candidate.title_name));

  assert(sm_install_queue_candidate(
      &candidate, false, "gen-old"));
  assert(sm_install_queue_candidate(
      &candidate, false, "gen-current"));
  assert(cleanup_calls == 0);
  assert(sm_install_has_pending_work());
  assert(sm_install_next_wake_us(fake_clock) != 0);
  assert(sm_install_submit_queued());
  assert(cleanup_calls == 0);
  assert(sm_install_has_pending_work());

  app_db_has_title = true;
  cleanup_allowed = false;
  sm_install_poll_pending();
  assert(cleanup_calls == 1);
  assert(strcmp(cached_path, "/cache/PPSA00001/gen-new") == 0);
  assert(!sm_install_has_pending_work());
  assert(sm_install_next_wake_us(fake_clock) != 0);
  cleanup_allowed = true;
  sm_install_poll_pending();
  assert(cleanup_calls == 2);
  assert(!sm_install_has_pending_work());

  scan_candidate_t failed_candidate = {0};
  (void)strlcpy(failed_candidate.path, "/cache/PPSA00002/gen-new",
                sizeof(failed_candidate.path));
  (void)strlcpy(failed_candidate.title_id, "PPSA00002",
                sizeof(failed_candidate.title_id));
  (void)strlcpy(failed_candidate.title_name, "Failed Synthetic Game",
                sizeof(failed_candidate.title_name));
  assert(sm_install_queue_candidate(
      &failed_candidate, false, "gen-old-failed"));
  assert(sm_install_submit_queued());
  fake_clock += UINT64_C(301000000);
  sm_install_poll_pending();
  assert(cleanup_calls == 2);
  assert(!sm_install_has_pending_work());
  puts("async FPKG cache cleanup waits for confirmed app.db registration");
  return 0;
}
