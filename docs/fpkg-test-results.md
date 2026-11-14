# Native FPKG test and validation record

Date: 2026-11-14. This is an execution record, not a claim of FPKG compatibility.

## Environment

- Host: macOS 27.2, Apple Silicon (`arm64`), Apple Clang 21.0.0.
- Python: 3.14.3.
- PS5 SDK: unavailable; `/opt/ps5-payload-sdk` does not exist.
- Docker: unavailable.
- Test FPKG: not supplied. No game-derived fixture was created or copied.
- PS5 console: unavailable in this task environment.
- Linux CI: not run. Upstream GitHub permission is read-only; local commits cannot
  trigger the repository's workflow. The existing Ubuntu PS5 workflow now has a
  focused ASan/UBSan host-test step for this probe, but it has not executed here.

## Baseline build and regression suite

| Command | Result | Evidence / reason |
| --- | --- | --- |
| `make -n help` | FAIL (environment) | Make stops while including missing `/opt/ps5-payload-sdk/toolchain/prospero.mk`. No source compilation occurs. |
| `python3 tools/test_runtime_config.py --sanitize` | FAIL (platform) | macOS `struct stat` lacks Linux `st_mtim`/`st_ctim` names used by the existing implementation/test, and macOS headers do not expose `pthread_barrier_t` under this test's flags. |
| `python3 tools/test_shellcore.py --sanitize` | PARTIAL/FAIL (platform) | ptrace unit portion passed; remaining test assembly uses x86-64 GNU syntax and cannot assemble for this arm64 macOS host. |
| `python3 tools/test_scan_permissions.py` | FAIL (platform) | Existing `tests/host_platform.h` redefines macOS `strlcpy` under `-Werror`. |
| `python3 tools/test_shellcore_launch.py --sanitize` | PASS | `Launch ownership: abandoned switch recovery, retry identity, fallback, live owner and mutation guards passed`. |
| `python3 tools/test_sandbox_cleanup.py` | FAIL (platform) | Existing host shim redefines macOS `strlcpy`; macOS headers also lack the expected `fsid_t` declaration with current flags. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_header.c src/sm_fpkg.c -o /tmp/sm-fpkg-header-test && /tmp/sm-fpkg-header-test` | PASS (focused safety test) | Synthetic FIH/PFS eligibility, bounded CNT table and payload extents, superblock, bounded inode table, required outer metadata entries, direct/indirect map bounds, alias rejection, malformed dirent rejection and read-error propagation passed under ASan/UBSan. This validates only the outer metadata probe; it is not a game or real-package compatibility fixture. |

These are baseline suite outcomes on this host, not regressions caused by FPKG
changes. No test was skipped or reported as passing based on source inspection.

## FPKG matrix status

All T01–T38 are **NOT RUN** because implementation and fixture/PS5 requirements
are not satisfied yet. Specifically, there are no legal debug-FPKG fixtures,
no pinned Kraken host oracle execution, no AMPR codec-2 readback, no SDK build,
and no console. A test fixture hash, reference SHA, SMP SHA, AMPR artifact SHA,
command output or console log therefore does not exist for these cases.

## Validation gates

| Gate | Status |
| --- | --- |
| G0 repository/source/license audit | Source and license audit recorded. Baseline PS5 build is blocked by missing SDK. PR #200 is open, not merged. |
| G1 legal-fixture differential reader tests | NOT MET; no package fixture or C reader implementation. |
| G2 decoder differential/sanitizer/fuzz gates | NOT MET; no C decoder implementation. |
| G3 byte-identical indexes and runtime readback | NOT MET; no C index writer or pinned AMPR oracle run. |
| G4 safe cache crash/recovery tests | Design contract recorded; implementation and failure injection not run. |
| G5 PS5 launch/mount lifecycle | PS5-only; NOT RUN. |
| G6 Linux CI and release build | NOT RUN; requires a push-capable fork/PR and CI. |

Host-verified functionality includes the new bounded FIH/PFS and outer metadata
probe, including synthetic direct/indirect outer file maps and malformed/eligible
envelope bytes, and the pre-existing
SMP launch ownership regression test. No direct FPKG inner file-tree, decoder,
index, cache, registration, or launch functionality is claimed as implemented
or verified by this record.
