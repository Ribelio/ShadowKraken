/* Native AMPRIDX3 / AMPRPAK4 writer, based on the GPL-3.0-or-later
 * Kraken Mount and pinned ampr-emu-ng v0.4.0 format implementations. */
#include "sm_fpkg_index.h"

#include <stdlib.h>
#include <string.h>

#define IDX_MAGIC "AMPRIDX3"
#define IDX_HEADER 0x30u
#define IDX_RECORD 24u
#define IDX_SLOT 16u
#define IDX_SLOT_SHARED 1u
#define PAK_MAGIC "AMPRPAK4"
#define PAK_HEADER 128u
#define PAK_FILE_RECORD 48u
#define PAK_CHUNK_RECORD 12u
#define PAK_PACK_RECORD 32u
#define PAK_PAYLOAD_CRC_AT 112u
#define PAK_HEADER_CRC_AT 116u
#define PAK_ENDIAN 0x01020304u
#define PAK_PAGE 65536u
#define PAK_BLOCK (1u << 18)
#define PAK_OFFSET_LIMIT (UINT64_C(1) << 48)
#define PAK_KRAKEN_OFFSET_LIMIT (UINT64_C(1) << 40)
#define PAK_CODEC_RAW 0u
#define PAK_CODEC_KRAKEN 2u
#define PAK_FLAG_FULL 0x10u
#define PAK_FLAG_PAGE_CONTAINED 0x04u
#define PAK_FLAG_PAGE_ALIGNED 0x08u
#define PAK_FILE_PACKED 1u
#define PAK_PACK_IO_PAGE_LAYOUT 2u
#define MAX_INDEX_FILES 1000000u
#define MAX_INDEX_CHUNKS 16000000u
#define MAX_INDEX_PATH_BYTES (256u * 1024u * 1024u)
#define MAX_PATH_BYTES 1024u
#define MAX_COMPONENT_BYTES 255u

typedef struct {
  const sm_fpkg_index_file_t *file;
  char *path;
  size_t path_length;
  uint64_t folded_hash;
} index_row_t;

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(v >> (i * 8));
}

static bool add_size(size_t a, size_t b, size_t *out) {
  if (b > SIZE_MAX - a)
    return false;
  *out = a + b;
  return true;
}

static bool mul_size(size_t a, size_t b, size_t *out) {
  if (a != 0 && b > SIZE_MAX / a)
    return false;
  *out = a * b;
  return true;
}

static bool round_up_size(size_t value, size_t alignment, size_t *out) {
  size_t rem = value % alignment;
  return add_size(value, rem ? alignment - rem : 0, out);
}

static uint8_t fold_byte(uint8_t b) {
  if (b >= 'A' && b <= 'Z')
    return (uint8_t)(b + ('a' - 'A'));
  return b;
}

static bool valid_utf8(const uint8_t *s, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t c = s[i++];
    if (c < 0x80)
      continue;
    unsigned tail;
    uint32_t value;
    if (c >= 0xc2 && c <= 0xdf) {
      tail = 1;
      value = c & 0x1fu;
    } else if (c >= 0xe0 && c <= 0xef) {
      tail = 2;
      value = c & 0x0fu;
    } else if (c >= 0xf0 && c <= 0xf4) {
      tail = 3;
      value = c & 0x07u;
    } else {
      return false;
    }
    if (tail > len - i)
      return false;
    uint8_t first = s[i];
    if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0) ||
        (c == 0xf0 && first < 0x90) || (c == 0xf4 && first >= 0x90))
      return false;
    for (unsigned j = 0; j < tail; ++j) {
      uint8_t b = s[i++];
      if ((b & 0xc0u) != 0x80u)
        return false;
      value = (value << 6) | (b & 0x3fu);
    }
    if (value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
      return false;
  }
  return true;
}

static uint64_t fnv_path(const char *path, size_t len, uint64_t basis) {
  uint64_t hash = basis;
  for (size_t i = 0; i < len; ++i) {
    hash ^= fold_byte((uint8_t)path[i]);
    hash *= UINT64_C(1099511628211);
  }
  return hash ? hash : 1;
}

static uint64_t idx_hash(const char *path, size_t len) {
  return fnv_path(path, len, UINT64_C(1469598103934665603));
}

static uint64_t pak_hash(const char *path, size_t len) {
  return fnv_path(path, len, UINT64_C(14695981039346656037));
}

static int row_compare(const void *left, const void *right) {
  const index_row_t *a = left;
  const index_row_t *b = right;
  size_t n = a->path_length < b->path_length ? a->path_length : b->path_length;
  for (size_t i = 0; i < n; ++i) {
    uint8_t ac = fold_byte((uint8_t)a->path[i]);
    uint8_t bc = fold_byte((uint8_t)b->path[i]);
    if (ac != bc)
      return ac < bc ? -1 : 1;
  }
  return (a->path_length > b->path_length) -
         (a->path_length < b->path_length);
}

static bool valid_relative_path(const char *path, size_t len) {
  if (!path || len == 0 || len >= MAX_PATH_BYTES - 6u || path[0] == '/' ||
      !valid_utf8((const uint8_t *)path, len))
    return false;
  size_t component = 0;
  size_t component_start = 0;
  for (size_t i = 0; i <= len; ++i) {
    if (i < len && (path[i] == '\0' || path[i] == '\\'))
      return false;
    if (i == len || path[i] == '/') {
      component = i - component_start;
      if (component == 0 || component > MAX_COMPONENT_BYTES ||
          (component == 1 && path[component_start] == '.') ||
          (component == 2 && path[component_start] == '.' &&
           path[component_start + 1] == '.'))
        return false;
      component_start = i + 1;
    }
  }
  return true;
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t len) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < len; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                           (uint32_t)-(int32_t)(crc & 1u));
  }
  return ~crc;
}

static uint32_t crc32_parts(const uint8_t *a, size_t a_len,
                            const uint8_t *b, size_t b_len,
                            const uint8_t *c, size_t c_len) {
  uint32_t crc = UINT32_MAX;
  const uint8_t *parts[] = {a, b, c};
  size_t lengths[] = {a_len, b_len, c_len};
  for (size_t p = 0; p < 3; ++p) {
    for (size_t i = 0; i < lengths[p]; ++i) {
      crc ^= parts[p][i];
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                             (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static sm_fpkg_status_t prepare_rows(const sm_fpkg_index_file_t *files,
                                     size_t count, index_row_t **rows_out,
                                     size_t *path_bytes_out) {
  if (!files || count == 0 || count > MAX_INDEX_FILES)
    return SM_FPKG_UNSUPPORTED;
  index_row_t *rows = calloc(count, sizeof(*rows));
  if (!rows)
    return SM_FPKG_NOMEM;
  size_t path_bytes = 0;
  sm_fpkg_status_t status = SM_FPKG_OK;
  for (size_t i = 0; i < count; ++i) {
    if (!files[i].path) {
      status = SM_FPKG_CORRUPT;
      break;
    }
    size_t rel_len = strlen(files[i].path);
    if (!valid_relative_path(files[i].path, rel_len)) {
      status = SM_FPKG_CORRUPT;
      break;
    }
    size_t full_len;
    if (!add_size(6u, rel_len, &full_len) ||
        !add_size(path_bytes, full_len + 1u, &path_bytes) ||
        path_bytes > MAX_INDEX_PATH_BYTES || full_len > UINT32_MAX) {
      status = SM_FPKG_UNSUPPORTED;
      break;
    }
    rows[i].path = malloc(full_len + 1u);
    if (!rows[i].path) {
      status = SM_FPKG_NOMEM;
      break;
    }
    memcpy(rows[i].path, "/app0/", 6u);
    memcpy(rows[i].path + 6u, files[i].path, rel_len);
    rows[i].path[full_len] = '\0';
    rows[i].path_length = full_len;
    rows[i].file = &files[i];
    rows[i].folded_hash = idx_hash(rows[i].path, full_len);
  }
  if (status == SM_FPKG_OK) {
    qsort(rows, count, sizeof(*rows), row_compare);
    for (size_t i = 1; i < count; ++i) {
      if (row_compare(&rows[i - 1], &rows[i]) == 0) {
        status = SM_FPKG_CORRUPT;
        break;
      }
    }
  }
  if (status != SM_FPKG_OK) {
    for (size_t i = 0; i < count; ++i)
      free(rows[i].path);
    free(rows);
    return status;
  }
  *rows_out = rows;
  *path_bytes_out = path_bytes;
  return SM_FPKG_OK;
}

static void free_rows(index_row_t *rows, size_t count) {
  if (!rows)
    return;
  for (size_t i = 0; i < count; ++i)
    free(rows[i].path);
  free(rows);
}

static sm_fpkg_status_t build_index(const index_row_t *rows, size_t count,
                                    size_t path_bytes, int64_t mtime,
                                    sm_fpkg_index_blob_t *out) {
  size_t records_size, path_start, slots_offset, slot_count = 2;
  if (!mul_size(count, IDX_RECORD, &records_size) ||
      !add_size(IDX_HEADER, records_size, &path_start) ||
      !add_size(path_start, path_bytes, &slots_offset) ||
      !round_up_size(slots_offset, IDX_SLOT, &slots_offset))
    return SM_FPKG_UNSUPPORTED;
  while (slot_count < count * 2u) {
    if (slot_count > SIZE_MAX / 2u)
      return SM_FPKG_UNSUPPORTED;
    slot_count *= 2u;
  }
  size_t slots_size, total;
  if (!mul_size(slot_count, IDX_SLOT, &slots_size) ||
      !add_size(slots_offset, slots_size, &total) ||
      slot_count > UINT32_MAX || slots_offset > UINT64_MAX)
    return SM_FPKG_UNSUPPORTED;
  uint8_t *buf = calloc(1, total);
  if (!buf)
    return SM_FPKG_NOMEM;
  memcpy(buf, IDX_MAGIC, 8);
  put_le32(buf + 8, 3);
  put_le32(buf + 12, IDX_RECORD);
  put_le64(buf + 16, count);
  put_le64(buf + 24, path_bytes);
  put_le64(buf + 32, slots_offset);
  put_le32(buf + 40, IDX_SLOT);
  put_le32(buf + 44, (uint32_t)slot_count);

  size_t path_at = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t *record = buf + IDX_HEADER + i * IDX_RECORD;
    put_le32(record, (uint32_t)path_at);
    put_le32(record + 4, (uint32_t)rows[i].path_length);
    put_le64(record + 8, rows[i].file->size);
    put_le64(record + 16, (uint64_t)mtime);
    memcpy(buf + path_start + path_at, rows[i].path, rows[i].path_length);
    path_at += rows[i].path_length;
    buf[path_start + path_at++] = 0;
  }
  for (size_t i = 0; i < count; ++i) {
    uint64_t hash = rows[i].folded_hash;
    size_t at = (size_t)hash & (slot_count - 1u);
    bool shared = false;
    while (read_le32(buf + slots_offset + at * IDX_SLOT + 8u) != 0) {
      uint8_t *slot = buf + slots_offset + at * IDX_SLOT;
      uint64_t old_hash = (uint64_t)slot[0] | ((uint64_t)slot[1] << 8) |
                          ((uint64_t)slot[2] << 16) |
                          ((uint64_t)slot[3] << 24) |
                          ((uint64_t)slot[4] << 32) |
                          ((uint64_t)slot[5] << 40) |
                          ((uint64_t)slot[6] << 48) |
                          ((uint64_t)slot[7] << 56);
      if (old_hash == hash) {
        put_le32(slot + 12, read_le32(slot + 12) | IDX_SLOT_SHARED);
        shared = true;
      }
      at = (at + 1u) & (slot_count - 1u);
    }
    uint8_t *slot = buf + slots_offset + at * IDX_SLOT;
    put_le64(slot, hash);
    put_le32(slot + 8, (uint32_t)i + 1u);
    put_le32(slot + 12, shared ? IDX_SLOT_SHARED : 0);
  }
  out->data = buf;
  out->size = total;
  return SM_FPKG_OK;
}

static sm_fpkg_status_t chunk_descriptor(const sm_fpkg_index_chunk_t *chunk,
                                         uint64_t wanted, uint64_t package_size,
                                         uint64_t volume_size,
                                         uint64_t *location_out,
                                         uint32_t *descriptor_out) {
  uint64_t stored;
  uint32_t codec;
  uint8_t flags = 0;
  uint64_t location = chunk->package_offset;
  if (chunk->raw_length < wanted || chunk->raw_length > PAK_BLOCK)
    return SM_FPKG_CORRUPT;
  if (!chunk->kraken_encoded) {
    if (chunk->stored_length != chunk->raw_length)
      return SM_FPKG_CORRUPT;
    stored = wanted;
    codec = PAK_CODEC_RAW;
  } else {
    stored = chunk->stored_length;
    if (stored == 0 || stored > PAK_BLOCK || chunk->even_length > stored ||
        chunk->mode >= 32 ||
        (chunk->raw_length > wanted && chunk->raw_length != PAK_BLOCK))
      return SM_FPKG_CORRUPT;
    codec = PAK_CODEC_KRAKEN;
    if (chunk->raw_length > wanted)
      flags |= PAK_FLAG_FULL;
    location |= (uint64_t)chunk->even_length << 40;
    location |= (uint64_t)chunk->mode << 59;
  }
  uint64_t limit = codec == PAK_CODEC_KRAKEN ? PAK_KRAKEN_OFFSET_LIMIT
                                               : PAK_OFFSET_LIMIT;
  if (chunk->package_offset < PAK_PAGE ||
      chunk->package_offset >= limit || stored > volume_size ||
      stored > package_size ||
      chunk->package_offset > package_size - stored ||
      chunk->package_offset > volume_size - stored ||
      chunk->package_offset + stored >= limit)
    return SM_FPKG_CORRUPT;
  bool contained = stored <= PAK_PAGE &&
                   chunk->package_offset / PAK_PAGE ==
                       (chunk->package_offset + stored - 1u) / PAK_PAGE;
  bool aligned = chunk->package_offset % PAK_PAGE == 0 &&
                 (stored > PAK_PAGE || contained);
  if (contained)
    flags |= PAK_FLAG_PAGE_CONTAINED;
  if (aligned)
    flags |= PAK_FLAG_PAGE_ALIGNED;
  *location_out = location;
  *descriptor_out = (uint32_t)(stored - 1u) | (codec << 20) |
                    ((uint32_t)flags << 22);
  return SM_FPKG_OK;
}

static sm_fpkg_status_t build_manifest(const index_row_t *rows, size_t count,
                                       size_t path_bytes, size_t chunk_count,
                                       uint64_t pkg_size, int64_t mtime,
                                       sm_fpkg_index_blob_t *out) {
  if (pkg_size > UINT64_MAX - (PAK_PAGE - 1u))
    return SM_FPKG_UNSUPPORTED;
  uint64_t volume_size = (pkg_size + PAK_PAGE - 1u) / PAK_PAGE * PAK_PAGE;
  if (volume_size < 2u * PAK_PAGE)
    volume_size = 2u * PAK_PAGE;
  size_t files_size, chunks_size, strings_size, payload_size, total;
  if (!mul_size(count, PAK_FILE_RECORD, &files_size) ||
      !mul_size(chunk_count, PAK_CHUNK_RECORD, &chunks_size) ||
      !add_size(path_bytes, sizeof("kraken.pkg"), &strings_size) ||
      !add_size(files_size, chunks_size, &payload_size) ||
      !add_size(payload_size, PAK_PACK_RECORD, &payload_size) ||
      !add_size(payload_size, strings_size, &payload_size) ||
      !add_size(PAK_HEADER, payload_size, &total) ||
      chunk_count > UINT32_MAX)
    return SM_FPKG_UNSUPPORTED;
  uint8_t *buf = calloc(1, total);
  if (!buf)
    return SM_FPKG_NOMEM;
  uint8_t *file_records = buf + PAK_HEADER;
  uint8_t *chunk_records = file_records + files_size;
  uint8_t *pack_record = chunk_records + chunks_size;
  uint8_t *strings = pack_record + PAK_PACK_RECORD;
  size_t string_at = 0, chunk_at = 0;

  for (size_t i = 0; i < count; ++i) {
    const sm_fpkg_index_file_t *file = rows[i].file;
    uint8_t *rec = file_records + i * PAK_FILE_RECORD;
    put_le64(rec, pak_hash(rows[i].path, rows[i].path_length));
    put_le64(rec + 8, file->size);
    put_le64(rec + 16, (uint64_t)mtime);
    if (string_at > UINT32_MAX || rows[i].path_length > UINT32_MAX) {
      free(buf);
      return SM_FPKG_UNSUPPORTED;
    }
    put_le32(rec + 32, (uint32_t)string_at);
    put_le32(rec + 36, (uint32_t)rows[i].path_length);
    memcpy(strings + string_at, rows[i].path, rows[i].path_length);
    string_at += rows[i].path_length;
    strings[string_at++] = 0;
    if (!file->chunks)
      continue;
    put_le32(rec + 24, (uint32_t)chunk_at);
    put_le32(rec + 28, (uint32_t)file->chunk_count);
    put_le32(rec + 40, PAK_FILE_PACKED);
    rec[44] = 18;
    for (size_t j = 0; j < file->chunk_count; ++j) {
      uint64_t logical_offset;
      if (j > UINT64_MAX / PAK_BLOCK) {
        free(buf);
        return SM_FPKG_UNSUPPORTED;
      }
      logical_offset = (uint64_t)j * PAK_BLOCK;
      if (logical_offset >= file->size) {
        free(buf);
        return SM_FPKG_CORRUPT;
      }
      uint64_t wanted = file->size - logical_offset;
      if (wanted > PAK_BLOCK)
        wanted = PAK_BLOCK;
      uint64_t location;
      uint32_t descriptor;
      sm_fpkg_status_t status = chunk_descriptor(
          &file->chunks[j], wanted, pkg_size, volume_size, &location,
          &descriptor);
      if (status != SM_FPKG_OK) {
        free(buf);
        return status;
      }
      uint8_t *chunk = chunk_records + chunk_at * PAK_CHUNK_RECORD;
      put_le64(chunk, location);
      put_le32(chunk + 8, descriptor);
      ++chunk_at;
    }
  }
  if (string_at != path_bytes) {
    free(buf);
    return SM_FPKG_CORRUPT;
  }
  put_le64(pack_record, volume_size - PAK_PAGE);
  put_le64(pack_record + 8, volume_size);
  put_le32(pack_record + 16, (uint32_t)string_at);
  put_le32(pack_record + 20, (uint32_t)(sizeof("kraken.pkg") - 1u));
  put_le32(pack_record + 24, PAK_PACK_IO_PAGE_LAYOUT);
  put_le32(pack_record + 28, PAK_PAGE);
  memcpy(strings + string_at, "kraken.pkg", sizeof("kraken.pkg"));

  memcpy(buf, PAK_MAGIC, 8);
  put_le32(buf + 8, 4);
  put_le32(buf + 12, PAK_HEADER);
  put_le32(buf + 20, PAK_ENDIAN);
  uint32_t seed = crc32_parts(file_records, files_size, chunk_records,
                              chunks_size, strings, strings_size);
  for (unsigned i = 0; i < 4; ++i) {
    uint8_t seed_bytes[4], size_bytes[8], index_byte = (uint8_t)i;
    put_le32(seed_bytes, seed);
    put_le64(size_bytes, pkg_size);
    uint32_t id = crc32_parts(seed_bytes, sizeof(seed_bytes), size_bytes,
                              sizeof(size_bytes), &index_byte, 1);
    put_le32(buf + 24 + i * 4u, id);
  }
  put_le64(buf + 40, count);
  put_le64(buf + 48, chunk_count);
  put_le32(buf + 56, 1);
  put_le32(buf + 60, PAK_FILE_RECORD);
  put_le32(buf + 64, PAK_CHUNK_RECORD);
  put_le32(buf + 68, PAK_PACK_RECORD);
  uint64_t offset = PAK_HEADER;
  put_le64(buf + 72, offset);
  offset += files_size;
  put_le64(buf + 80, offset);
  offset += chunks_size;
  put_le64(buf + 88, offset);
  offset += PAK_PACK_RECORD;
  put_le64(buf + 96, offset);
  put_le64(buf + 104, strings_size);
  uint32_t payload_crc = crc32_bytes(buf + PAK_HEADER, total - PAK_HEADER);
  put_le32(buf + PAK_PAYLOAD_CRC_AT, payload_crc);
  const uint8_t zero_crc[4] = {0};
  uint32_t header_crc = crc32_parts(buf, PAK_HEADER_CRC_AT, zero_crc,
                                    sizeof(zero_crc),
                                    buf + PAK_HEADER_CRC_AT + 4u,
                                    PAK_HEADER - PAK_HEADER_CRC_AT - 4u);
  put_le32(buf + PAK_HEADER_CRC_AT, header_crc);
  out->data = buf;
  out->size = total;
  return SM_FPKG_OK;
}

sm_fpkg_status_t sm_fpkg_index_build(
    const sm_fpkg_index_file_t *files, size_t file_count,
    uint64_t package_size, int64_t package_mtime,
    sm_fpkg_index_blob_t *index_out, sm_fpkg_index_blob_t *manifest_out) {
  if (!index_out || !manifest_out || index_out == manifest_out)
    return SM_FPKG_CORRUPT;
  index_out->data = NULL;
  index_out->size = 0;
  manifest_out->data = NULL;
  manifest_out->size = 0;
  if (package_size == 0)
    return SM_FPKG_CORRUPT;
  index_row_t *rows = NULL;
  size_t path_bytes = 0;
  sm_fpkg_status_t status =
      prepare_rows(files, file_count, &rows, &path_bytes);
  if (status != SM_FPKG_OK)
    return status;
  size_t total_chunks = 0;
  for (size_t i = 0; i < file_count; ++i) {
    const sm_fpkg_index_file_t *file = rows[i].file;
    uint64_t expected = file->size / PAK_BLOCK +
                        (file->size % PAK_BLOCK != 0);
    if (file->chunks) {
      if (file->chunk_count != expected ||
          !add_size(total_chunks, file->chunk_count, &total_chunks) ||
          total_chunks > MAX_INDEX_CHUNKS) {
        status = SM_FPKG_CORRUPT;
        goto done;
      }
    } else if (file->chunk_count != 0) {
      status = SM_FPKG_CORRUPT;
      goto done;
    }
  }
  status = build_index(rows, file_count, path_bytes, package_mtime, index_out);
  if (status != SM_FPKG_OK)
    goto done;
  status = build_manifest(rows, file_count, path_bytes, total_chunks,
                          package_size, package_mtime, manifest_out);
  if (status != SM_FPKG_OK) {
    free(index_out->data);
    index_out->data = NULL;
    index_out->size = 0;
  }
done:
  free_rows(rows, file_count);
  return status;
}

sm_fpkg_status_t sm_fpkg_index_build_package(
    const sm_fpkg_t *fpkg, sm_fpkg_index_extract_fn should_extract_fn,
    void *extract_ctx, int64_t package_mtime,
    sm_fpkg_index_blob_t *index_out, sm_fpkg_index_blob_t *manifest_out) {
  if (!index_out || !manifest_out || index_out == manifest_out)
    return SM_FPKG_CORRUPT;
  index_out->data = NULL;
  index_out->size = 0;
  manifest_out->data = NULL;
  manifest_out->size = 0;
  if (!fpkg)
    return SM_FPKG_CORRUPT;
  size_t entry_count = sm_fpkg_file_count(fpkg);
  if (entry_count == 0 || entry_count > MAX_INDEX_FILES ||
      sm_fpkg_package_size(fpkg) == 0)
    return SM_FPKG_UNSUPPORTED;

  sm_fpkg_index_file_t *files = calloc(entry_count, sizeof(*files));
  sm_fpkg_index_chunk_t **owned_chunks =
      calloc(entry_count, sizeof(*owned_chunks));
  sm_fpkg_block_t *blocks = NULL;
  if (!files || !owned_chunks) {
    free(files);
    free(owned_chunks);
    return SM_FPKG_NOMEM;
  }
  static const sm_fpkg_index_chunk_t empty_chunks[1] = {{0}};
  size_t file_count = 0, total_chunks = 0;
  sm_fpkg_status_t status = SM_FPKG_OK;
  for (size_t i = 0; i < entry_count; ++i) {
    sm_fpkg_file_t entry;
    if (!sm_fpkg_file_at(fpkg, i, &entry)) {
      status = SM_FPKG_CORRUPT;
      break;
    }
    if (entry.is_directory)
      continue;
    sm_fpkg_index_file_t *file = &files[file_count];
    file->path = entry.path;
    file->size = entry.size;
    if (should_extract_fn && should_extract_fn(entry.path, extract_ctx))
      goto next_file;

    size_t block_count = 0;
    status = sm_fpkg_file_block_count(fpkg, i, &block_count);
    if (status != SM_FPKG_OK)
      break;
    if (block_count > MAX_INDEX_CHUNKS - total_chunks) {
      status = SM_FPKG_UNSUPPORTED;
      break;
    }
    if (block_count == 0) {
      file->chunks = empty_chunks;
      goto next_file;
    }
    if (block_count > SIZE_MAX / sizeof(*blocks) ||
        block_count > SIZE_MAX / sizeof(*owned_chunks[file_count])) {
      status = SM_FPKG_UNSUPPORTED;
      break;
    }
    blocks = malloc(block_count * sizeof(*blocks));
    owned_chunks[file_count] =
        malloc(block_count * sizeof(*owned_chunks[file_count]));
    if (!blocks || !owned_chunks[file_count]) {
      status = SM_FPKG_NOMEM;
      break;
    }
    size_t actual_count = 0;
    status = sm_fpkg_file_blocks(fpkg, i, blocks, block_count,
                                 &actual_count);
    if (status != SM_FPKG_OK)
      break;
    if (actual_count != block_count) {
      status = SM_FPKG_CORRUPT;
      break;
    }
    for (size_t j = 0; j < block_count; ++j) {
      owned_chunks[file_count][j] = (sm_fpkg_index_chunk_t){
          .package_offset = blocks[j].package_offset,
          .stored_length = blocks[j].stored_size,
          .raw_length = blocks[j].raw_size,
          .even_length = blocks[j].even_size,
          .mode = blocks[j].mode,
          .kraken_encoded = blocks[j].kraken_encoded,
      };
    }
    file->chunks = owned_chunks[file_count];
    file->chunk_count = block_count;
    total_chunks += block_count;
    free(blocks);
    blocks = NULL;
next_file:
    ++file_count;
  }
  if (status == SM_FPKG_OK)
    status = sm_fpkg_index_build(files, file_count,
                                 sm_fpkg_package_size(fpkg), package_mtime,
                                 index_out, manifest_out);
  free(blocks);
  for (size_t i = 0; i < entry_count; ++i)
    free(owned_chunks[i]);
  free(owned_chunks);
  free(files);
  if (status != SM_FPKG_OK) {
    sm_fpkg_index_blob_free(index_out);
    sm_fpkg_index_blob_free(manifest_out);
  }
  return status;
}

void sm_fpkg_index_blob_free(sm_fpkg_index_blob_t *blob) {
  if (!blob)
    return;
  free(blob->data);
  blob->data = NULL;
  blob->size = 0;
}
