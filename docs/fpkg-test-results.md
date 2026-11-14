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
  focused ASan/UBSan host-test step for the probe, layout parser and index writer,
  but it has not executed here.

## Baseline build and regression suite

| Command | Result | Evidence / reason |
| --- | --- | --- |
| `make -n help` | FAIL (environment) | Make stops while including missing `/opt/ps5-payload-sdk/toolchain/prospero.mk`. No source compilation occurs. |
| `python3 tools/test_runtime_config.py --sanitize` | FAIL (platform) | macOS `struct stat` lacks Linux `st_mtim`/`st_ctim` names used by the existing implementation/test, and macOS headers do not expose `pthread_barrier_t` under this test's flags. |
| `python3 tools/test_shellcore.py --sanitize` | PARTIAL/FAIL (platform) | ptrace unit portion passed; remaining test assembly uses x86-64 GNU syntax and cannot assemble for this arm64 macOS host. |
| `python3 tools/test_scan_permissions.py` | FAIL (platform) | Existing `tests/host_platform.h` redefines macOS `strlcpy` under `-Werror`. |
| `python3 tools/test_shellcore_launch.py --sanitize` | PASS | `Launch ownership: abandoned switch recovery, retry identity, fallback, live owner and mutation guards passed`. |
| `python3 tools/test_sandbox_cleanup.py` | FAIL (platform) | Existing host shim redefines macOS `strlcpy`; macOS headers also lack the expected `fsid_t` declaration with current flags. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_header.c src/sm_fpkg.c src/sm_fpkg_layout.c src/sm_fpkg_image.c src/sm_fpkg_decoder.c src/sm_fpkg_tree.c -o /tmp/sm-fpkg-header-test && /tmp/sm-fpkg-header-test` | PASS (focused safety test) | Synthetic FIH/PFS eligibility, bounded CNT table and payload extents, outer direct/indirect maps, mapped descriptor parse, two-block image reads and inner superblock/inode/tree validation pass under ASan/UBSan. This is not a game or real-package compatibility fixture. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_index.c src/sm_fpkg_index.c -o /tmp/sm-fpkg-index-test && /tmp/sm-fpkg-index-test` | PASS (focused safety test) | Synthetic AMPRIDX3/AMPRPAK4 layout, RAW and codec-2 descriptors, CRCs, duplicate/case-folded paths, traversal, invalid UTF-8 and package-bound checks passed under ASan/UBSan. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_layout.c src/sm_fpkg_layout.c -o /tmp/sm-fpkg-layout-test && /tmp/sm-fpkg-layout-test` | PASS (focused safety test) | Synthetic raw 256 KiB Kraken descriptor, resource bounds, compression/shuffle rejection, sparse rejection and short input passed under ASan/UBSan. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_decoder.c src/sm_fpkg_decoder.c -o /tmp/sm-fpkg-decoder-test && /tmp/sm-fpkg-decoder-test` | PASS (focused safety test) | Synthetic raw block, LZ literal and match commands, delta literals, single-symbol and multi-symbol Huffman arrays passed under ASan/UBSan. |
| `python3 tools/test_fpkg_decoder_reference.py /private/tmp/kraken-mount-reference` | PASS | The pinned Kraken reader at `4b420d579bd381796d773875e36d14c9acaabf39` independently decoded matching synthetic literal, delta, match and Huffman vectors. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_tree.c src/sm_fpkg_tree.c -o /tmp/sm-fpkg-tree-test && /tmp/sm-fpkg-tree-test` | PASS (focused safety test) | Synthetic inner PFS tree bounds, UTF-8/path traversal rejection, inode-count cap and directory-cycle rejection passed under ASan/UBSan. |
| Temporary focused oracle harness: C writer output parsed with pinned `ampr_format::index::Index::open` and `ampr_pkg::open_pkg` from v0.4.0 source revision `920e9172b6bf99bd9210361b545e2953659d1e9b` | PASS | The AMPR reader accepted a synthetic one-file codec-2 manifest and AMPRIDX3 index. This is format parser acceptance, not byte-identical output comparison or decoded package-chunk readback. Harness and output remained under `/private/tmp`; no reference runtime is included in SMP. |
| Temporary focused layout oracle: `ps5upload_fpkg::kraken_image::describe` and the C parser consumed the same synthetic descriptor bytes | PASS | Both reported one raw block at logical/stored offset zero, length 256 KiB. The descriptor and harness remained under `/private/tmp`; this is not an FPKG fixture. |

The existing suite outcomes above are macOS baseline results; the FPKG commands
are focused implementation checks. No test was reported as passing based on
source inspection.

## FPKG matrix status

All T01–T38 are **NOT RUN** because implementation and fixture/PS5 requirements
are not satisfied yet. Specifically, there are no legal debug-FPKG fixtures,
the pinned decoder oracle covers only synthetic blocks rather than full packages,
there is no AMPR codec-2 package readback, no SDK build, and no console. A legal
fixture hash, package reference SHA, SMP SHA, AMPR artifact SHA, or console log
therefore does not exist for these cases.

## Validation gates

| Gate | Status |
| --- | --- |
| G0 repository/source/license audit | Source and license audit recorded. Baseline PS5 build is blocked by missing SDK. PR #200 is open, not merged. |
| G1 legal-fixture differential reader tests | NOT MET; synthetic outer-to-inner reads and inner metadata/tree validation exist, but there is no legal FPKG fixture, CNT metadata fallback, normalized retained file index, or extraction. |
| G2 decoder differential/sanitizer/fuzz gates | NOT MET; native decoder and synthetic differential vectors exist, but the legal golden corpus, malformed-stream corpus, fuzzing and worst-case CPU/memory measurements remain. |
| G3 byte-identical indexes and runtime readback | NOT MET; C writer exists and one synthetic output passed the pinned parser, but no byte-identical writer comparison, multi-case manifest coverage or decoded package-chunk readback has run. |
| G4 safe cache crash/recovery tests | Design contract recorded; implementation and failure injection not run. |
| G5 PS5 launch/mount lifecycle | PS5-only; NOT RUN. |
| G6 Linux CI and release build | NOT RUN; requires a push-capable fork/PR and CI. |

Host-verified functionality includes the bounded FIH/PFS/CNT and outer metadata
probe through descriptor mapping and parsing, the standalone Kraken descriptor
parser against a synthetic descriptor also accepted by ps5-dump-forge, the AMPR
writer against the pinned AMPR parser, and the native Kraken decoder against
synthetic reference vectors, plus inner-tree path validation through the outer
reader. No retained file index, extraction, cache, registration, or launch
functionality is claimed as implemented or verified by this record.
