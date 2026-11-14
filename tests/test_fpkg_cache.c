/* Synthetic cache transaction fixtures; no package bytes are read. */
#include "host_platform.h"

#include <sys/file.h>
#include <sys/statvfs.h>
#include <dirent.h>

#include "sm_fpkg_cache.h"
#include "sm_fpkg.h"
#include "sm_time.h"

#define TEST_TITLE_ID "PPSA00001"
#define TEST_SOURCE_PATH "/tmp/synthetic.pkg"

uint64_t monotonic_time_us(void) { return 1u; }

bool sm_pkg_source_matches(const sm_pkg_info_t *info, const struct stat *st) {
  (void)info;
  (void)st;
  return false;
}

int sm_fpkg_source_fd(const sm_fpkg_t *fpkg) {
  (void)fpkg;
  return -1;
}

size_t sm_fpkg_file_count(const sm_fpkg_t *fpkg) {
  (void)fpkg;
  return 0;
}

bool sm_fpkg_file_at(const sm_fpkg_t *fpkg, size_t index,
                     sm_fpkg_file_t *file_out) {
  (void)fpkg;
  (void)index;
  (void)file_out;
  return false;
}

sm_fpkg_status_t sm_fpkg_extract_file(sm_fpkg_t *fpkg, size_t index,
                                      const sm_fpkg_extract_sink_t *sink) {
  (void)fpkg;
  (void)index;
  (void)sink;
  return SM_FPKG_UNSUPPORTED;
}

uint64_t sm_fpkg_package_size(const sm_fpkg_t *fpkg) {
  (void)fpkg;
  return 0;
}

sm_fpkg_status_t sm_fpkg_file_block_count(const sm_fpkg_t *fpkg, size_t index,
                                          size_t *count_out) {
  (void)fpkg;
  (void)index;
  if (count_out)
    *count_out = 0;
  return SM_FPKG_UNSUPPORTED;
}

sm_fpkg_status_t sm_fpkg_file_blocks(const sm_fpkg_t *fpkg, size_t index,
                                     sm_fpkg_block_t *blocks,
                                     size_t block_capacity,
                                     size_t *count_out) {
  (void)fpkg;
  (void)index;
  (void)blocks;
  (void)block_capacity;
  if (count_out)
    *count_out = 0;
  return SM_FPKG_UNSUPPORTED;
}

static void write_all_test(int fd, const char *bytes, size_t size) {
  size_t done = 0;
  while (done < size) {
    ssize_t count = write(fd, bytes + done, size - done);
    assert(count > 0);
    done += (size_t)count;
  }
  assert(fsync(fd) == 0);
}

static void write_at(int directory_fd, const char *name, const char *contents,
                     mode_t mode) {
  int fd = openat(directory_fd, name,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                  mode);
  assert(fd >= 0);
  write_all_test(fd, contents, strlen(contents));
  assert(close(fd) == 0);
}

static void write_at_bytes(int directory_fd, const char *name,
                           const void *contents, size_t size, mode_t mode) {
  int fd = openat(directory_fd, name,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                  mode);
  assert(fd >= 0);
  const uint8_t *bytes = contents;
  size_t done = 0;
  while (done < size) {
    ssize_t count = write(fd, bytes + done, size - done);
    assert(count > 0);
    done += (size_t)count;
  }
  assert(fsync(fd) == 0);
  assert(close(fd) == 0);
}

static uint64_t fixture_fingerprint(uint64_t inode) {
  uint64_t hash = UINT64_C(14695981039346656037);
  const uint64_t fields[] = {1u, inode, 3u, 5u, 4u, 0u, 0u};
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    uint64_t value = fields[i];
    for (unsigned byte = 0; byte < 8u; ++byte) {
      hash = (hash ^ (uint8_t)value) * UINT64_C(1099511628211);
      value >>= 8;
    }
  }
  return hash;
}

static int make_generation(int title_fd, const char *name, uint64_t inode) {
  assert(mkdirat(title_fd, name, 0700) == 0);
  int generation_fd = openat(title_fd, name,
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(generation_fd >= 0);
  assert(mkdirat(generation_fd, "sce_sys", 0700) == 0);
  assert(mkdirat(generation_fd, "kraken", 0700) == 0);

  write_at(generation_fd, "eboot.bin", "boot", 0644);
  int system_fd = openat(generation_fd, "sce_sys",
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(system_fd >= 0);
  write_at(system_fd, "param.json", "meta", 0644);
  assert(close(system_fd) == 0);
  sm_fpkg_index_file_t index_file = {.path = "eboot.bin", .size = 4};
  sm_fpkg_index_blob_t index = {0}, manifest = {0};
  assert(sm_fpkg_index_build(&index_file, 1, 3, 4, &index, &manifest) ==
         SM_FPKG_OK);
  write_at_bytes(generation_fd, "ampr_emu.index", index.data, index.size,
                 0644);
  write_at_bytes(generation_fd, "ampr_assets.index", manifest.data,
                 manifest.size, 0644);
  const char link_contents[] = "kraken/synthetic.pkg\n";
  write_at(generation_fd, "kraken.image", link_contents, 0644);

  char built[2048];
  char fingerprint[17];
  (void)snprintf(fingerprint, sizeof(fingerprint), "%016llx",
                 (unsigned long long)fixture_fingerprint(inode));
  int built_length = snprintf(
      built, sizeof(built),
      "SM-FPKG-BUILT-1\n"
      "title=%s\nsource=" TEST_SOURCE_PATH "\n"
      "fingerprint=%s\ndevice=1\ninode=%llu\nsize=3\n"
      "mtime=4.000000000\nctime=5.000000000\n"
      "file=4:eboot.bin\nfile=4:sce_sys/param.json\n"
      "file=%zu:ampr_emu.index\nfile=%zu:ampr_assets.index\n"
      "file=%zu:kraken.image\n",
      TEST_TITLE_ID, fingerprint, (unsigned long long)inode,
      index.size, manifest.size,
      sizeof(link_contents) - 1u);
  assert(built_length > 0 && (size_t)built_length < sizeof(built));
  write_at(generation_fd, ".built", built, 0600);

  char done[256];
  char name_fingerprint[17];
  memcpy(name_fingerprint, name + 4, 16u);
  name_fingerprint[16] = '\0';
  int done_length = snprintf(done, sizeof(done),
                             "SM-FPKG-DONE-1\ntitle=%s\nfingerprint=%s\n",
                             TEST_TITLE_ID, name_fingerprint);
  assert(done_length > 0 && (size_t)done_length < sizeof(done));
  write_at(generation_fd, ".done", done, 0600);
  write_at(generation_fd, ".journal",
           "SM-FPKG-JOURNAL-1\nstate=published\n", 0600);
  sm_fpkg_index_blob_free(&index);
  sm_fpkg_index_blob_free(&manifest);
  assert(fsync(generation_fd) == 0);
  return generation_fd;
}

static void write_active(int title_fd, const char *generation_name) {
  char active[256];
  int length = snprintf(active, sizeof(active),
                        "SM-FPKG-ACTIVE-1\ngeneration=%s\n",
                        generation_name);
  assert(length > 0 && (size_t)length < sizeof(active));
  write_at(title_fd, "active", active, 0600);
  assert(fsync(title_fd) == 0);
}

static bool cleanup_allowed = true;
static unsigned cleanup_begin_count;
static unsigned cleanup_end_count;

static bool begin_cleanup(const char *title_id, const char *generation_path,
                          void *ctx) {
  (void)ctx;
  assert(strcmp(title_id, TEST_TITLE_ID) == 0);
  assert(strstr(generation_path, "/gen-") != NULL);
  ++cleanup_begin_count;
  return cleanup_allowed;
}

static void end_cleanup(void *ctx) {
  (void)ctx;
  ++cleanup_end_count;
}

static void remove_tree(const char *path) {
  DIR *directory = opendir(path);
  if (!directory) {
    assert(unlink(path) == 0);
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char child[MAX_PATH];
    int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    assert(length > 0 && (size_t)length < sizeof(child));
    struct stat st;
    assert(lstat(child, &st) == 0);
    if (S_ISDIR(st.st_mode))
      remove_tree(child);
    else
      assert(unlink(child) == 0);
  }
  assert(closedir(directory) == 0);
  assert(rmdir(path) == 0);
}

int main(void) {
  char template[] = "/private/tmp/sm-fpkg-cache-test.XXXXXX";
  if (access("/private/tmp", W_OK) != 0)
    (void)strlcpy(template, "/tmp/sm-fpkg-cache-test.XXXXXX",
                  sizeof(template));
  char *cache_root = mkdtemp(template);
  assert(cache_root != NULL);
  assert(chmod(cache_root, 0700) == 0);
  int root_fd = open(cache_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(root_fd >= 0);
  assert(mkdirat(root_fd, TEST_TITLE_ID, 0700) == 0);
  int title_fd = openat(root_fd, TEST_TITLE_ID,
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(title_fd >= 0);
  int lock_fd = openat(title_fd, ".lock",
                       O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                       0600);
  assert(lock_fd >= 0);
  assert(close(lock_fd) == 0);

  char active_name[112];
  char orphan_name[112];
  (void)snprintf(active_name, sizeof(active_name), "gen-%016llx-1-2",
                 (unsigned long long)fixture_fingerprint(2u));
  (void)snprintf(orphan_name, sizeof(orphan_name), "gen-%016llx-3-4",
                 (unsigned long long)fixture_fingerprint(9u));
  int active_fd = make_generation(title_fd, active_name, 2u);
  assert(close(active_fd) == 0);
  write_active(title_fd, active_name);

  sm_pkg_info_t active_source = {0};
  (void)strlcpy(active_source.path, TEST_SOURCE_PATH,
                sizeof(active_source.path));
  (void)strlcpy(active_source.title_id, TEST_TITLE_ID,
                sizeof(active_source.title_id));
  active_source.source_device = 1u;
  active_source.source_inode = 2u;
  active_source.source_size = 3u;
  active_source.source_ctime = 5u;
  active_source.source_mtime = 4;
  active_source.pkg_kind = PKG_KIND_BASE;
  active_source.pkg_classification_confident = true;
  char current_generation[MAX_PATH];
  assert(sm_fpkg_cache_find_current_generation(cache_root, &active_source,
                                               current_generation) ==
         SM_FPKG_OK);
  char expected_generation[MAX_PATH];
  int generation_length = snprintf(expected_generation,
                                   sizeof(expected_generation), "%s/%s/%s",
                                   cache_root, TEST_TITLE_ID, active_name);
  assert(generation_length > 0 &&
         (size_t)generation_length < sizeof(expected_generation));
  assert(strcmp(current_generation, expected_generation) == 0);
  sm_fpkg_cache_generation_info_t generation_info;
  assert(sm_fpkg_cache_read_generation_info(
             cache_root, TEST_TITLE_ID, current_generation,
             &generation_info) == SM_FPKG_OK);
  assert(strcmp(generation_info.source_path, TEST_SOURCE_PATH) == 0);
  assert(strcmp(generation_info.generation_path, current_generation) == 0);
  assert(!generation_info.source_matches_snapshot);

  int damaged_generation_fd = openat(
      title_fd, active_name,
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(damaged_generation_fd >= 0);
  int damaged_index_fd = openat(damaged_generation_fd, "ampr_emu.index",
                                O_RDWR | O_NOFOLLOW | O_CLOEXEC);
  assert(damaged_index_fd >= 0);
  uint8_t original_size = 0;
  assert(pread(damaged_index_fd, &original_size, 1, 56) == 1);
  uint8_t damaged_size = (uint8_t)(original_size + 1u);
  assert(pwrite(damaged_index_fd, &damaged_size, 1, 56) == 1);
  assert(fsync(damaged_index_fd) == 0);
  assert(sm_fpkg_cache_read_generation_info(
             cache_root, TEST_TITLE_ID, current_generation,
             &generation_info) != SM_FPKG_OK);
  assert(pwrite(damaged_index_fd, &original_size, 1, 56) == 1);
  assert(fsync(damaged_index_fd) == 0);
  assert(close(damaged_index_fd) == 0);
  assert(close(damaged_generation_fd) == 0);
  assert(sm_fpkg_cache_read_generation_info(
             cache_root, TEST_TITLE_ID, current_generation,
             &generation_info) == SM_FPKG_OK);

  int orphan_fd = make_generation(title_fd, orphan_name, 9u);
  write_at(orphan_fd, "unexpected.save", "user-data", 0600);
  assert(close(orphan_fd) == 0);
  assert(mkdirat(title_fd, ".stage-interrupted", 0700) == 0);
  assert(mkdirat(title_fd, ".quarantine-interrupted", 0700) == 0);
  assert(fsync(title_fd) == 0);

  sm_fpkg_cache_recovery_report_t report;
  sm_fpkg_status_t initial_recovery =
      sm_fpkg_cache_recover_title(cache_root, TEST_TITLE_ID, &report);
  if (initial_recovery != SM_FPKG_OK)
    fprintf(stderr, "initial cache recovery returned %d (errno %d)\n",
            initial_recovery, errno);
  assert(initial_recovery == SM_FPKG_OK);
  struct stat st;
  assert(fstatat(title_fd, orphan_name, &st, AT_SYMLINK_NOFOLLOW) == 0);
  assert(unlinkat(title_fd, "active", 0) == 0);
  assert(symlinkat("missing-generation", title_fd, "active") == 0);
  assert(sm_fpkg_cache_recover_title(cache_root, TEST_TITLE_ID, &report) !=
         SM_FPKG_OK);
  assert(fstatat(title_fd, "active", &st, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISLNK(st.st_mode));
  assert(unlinkat(title_fd, "active", 0) == 0);
  write_active(title_fd, active_name);

  sm_fpkg_status_t recovery_status =
      sm_fpkg_cache_recover_title(cache_root, TEST_TITLE_ID, &report);
  if (recovery_status != SM_FPKG_OK)
    fprintf(stderr, "cache recovery returned %d (errno %d)\n",
            recovery_status, errno);
  assert(recovery_status == SM_FPKG_OK);
  char expected_active[MAX_PATH];
  int active_length = snprintf(expected_active, sizeof(expected_active),
                               "%s/%s/%s", cache_root, TEST_TITLE_ID,
                               active_name);
  assert(active_length > 0 && (size_t)active_length < sizeof(expected_active));
  assert(strcmp(report.active_generation, expected_active) == 0);
  assert(report.staging_generations == 1);
  assert(report.orphaned_generations == 1);
  assert(report.quarantined_generations == 1);

  cleanup_begin_count = cleanup_end_count = 0;
  assert(sm_fpkg_cache_remove_generation(
             cache_root, TEST_TITLE_ID, active_name, begin_cleanup, end_cleanup,
             NULL) == SM_FPKG_CANCELLED);
  assert(cleanup_begin_count == 1 && cleanup_end_count == 1);

  assert(sm_fpkg_cache_remove_generation(
             cache_root, TEST_TITLE_ID, orphan_name, begin_cleanup, end_cleanup,
             NULL) == SM_FPKG_CORRUPT);
  assert(fstatat(title_fd, orphan_name, &st, AT_SYMLINK_NOFOLLOW) == 0);

  int bad_generation_fd = openat(
      title_fd, orphan_name,
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  assert(bad_generation_fd >= 0);
  assert(unlinkat(bad_generation_fd, "unexpected.save", 0) == 0);
  assert(close(bad_generation_fd) == 0);
  cleanup_allowed = false;
  assert(sm_fpkg_cache_remove_generation(
             cache_root, TEST_TITLE_ID, orphan_name, begin_cleanup, end_cleanup,
             NULL) == SM_FPKG_CANCELLED);
  assert(fstatat(title_fd, orphan_name, &st, AT_SYMLINK_NOFOLLOW) == 0);
  cleanup_allowed = true;
  sm_fpkg_status_t cleanup_status = sm_fpkg_cache_remove_generation(
      cache_root, TEST_TITLE_ID, orphan_name, begin_cleanup, end_cleanup, NULL);
  if (cleanup_status != SM_FPKG_OK)
    fprintf(stderr, "cache cleanup returned %d (errno %d)\n", cleanup_status,
            errno);
  assert(cleanup_status == SM_FPKG_OK);
  assert(fstatat(title_fd, orphan_name, &st, AT_SYMLINK_NOFOLLOW) != 0 &&
         errno == ENOENT);
  assert(cleanup_begin_count == 4 && cleanup_end_count == 3);

  assert(close(title_fd) == 0);
  assert(close(root_fd) == 0);
  remove_tree(cache_root);
  puts("synthetic cache recovery, quarantine, and ownership cleanup checks passed");
  return 0;
}
