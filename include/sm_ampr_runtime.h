#ifndef SM_AMPR_RUNTIME_H
#define SM_AMPR_RUNTIME_H

#include "sm_limits.h"

// Resolve emulators_path/ampr-emu-ng/libSceAmpr.sprx and verify its embedded
// ampr-emu-ng identity marker. Returns 0 on success, -1 with errno on failure.
int sm_ampr_resolve_ng_runtime(const char *emulators_path,
                               char runtime_path[MAX_PATH]);

// Return 1 for an ampr-emu-ng binary, 0 for another file, and -1 on I/O error.
int sm_file_is_ampr_ng(const char *path);

#endif
