# Native FPKG test and validation record

Date: 2026-11-14. This is an execution record, not a claim of FPKG compatibility.

## Environment

- Host: macOS 27.2, Apple Silicon (`arm64`), Apple Clang 21.0.0.
- Python: 3.14.3.
- PS5 SDK: unavailable; `/opt/ps5-payload-sdk` does not exist.
- Docker: unavailable.
- Test FPKG: the user supplied a local 2,430,510,116-byte plaintext debug
  package for `PPSA12544`. It was opened read-only from Downloads; no package
  data was copied into the repository or extracted to disk.
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
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_header.c src/sm_fpkg.c src/sm_fpkg_layout.c src/sm_fpkg_image.c src/sm_fpkg_decoder.c src/sm_fpkg_tree.c src/sm_fpkg_index.c -o /tmp/sm-fpkg-header-test && /tmp/sm-fpkg-header-test` | PASS (focused safety test) | Synthetic FIH/PFS eligibility, bounded CNT table and payload extents, outer direct/indirect maps, mapped descriptor parse, three-block image reads, retained file enumeration and `eboot.bin` range read, package block mapping for aligned inner and CNT files, unaligned-block rejection, package-to-AMPR index construction for packed and caller-selected extracted files, streaming extraction success and cancellation rollback, CNT `icon0.png` fallback enumeration/read, and inner tree validation pass under ASan/UBSan. This is not a game or real-package compatibility fixture. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_index.c src/sm_fpkg_index.c src/sm_fpkg.c src/sm_fpkg_layout.c src/sm_fpkg_image.c src/sm_fpkg_decoder.c src/sm_fpkg_tree.c -o /tmp/sm-fpkg-index-test && /tmp/sm-fpkg-index-test` | PASS (focused safety test) | Synthetic AMPRIDX3/AMPRPAK4 layout, RAW and codec-2 descriptors, CRCs, duplicate/case-folded paths, traversal, invalid UTF-8 and package-bound checks passed under ASan/UBSan. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_layout.c src/sm_fpkg_layout.c -o /tmp/sm-fpkg-layout-test && /tmp/sm-fpkg-layout-test` | PASS (focused safety test) | Synthetic raw 256 KiB Kraken descriptor, resource bounds, compression/shuffle rejection, sparse rejection and short input passed under ASan/UBSan. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_decoder.c src/sm_fpkg_decoder.c -o /tmp/sm-fpkg-decoder-test && /tmp/sm-fpkg-decoder-test` | PASS (focused safety test) | Synthetic raw block, LZ literal and match commands, delta literals, single-symbol and multi-symbol Huffman arrays passed under ASan/UBSan. |
| `python3 tools/test_fpkg_decoder_reference.py /private/tmp/kraken-mount-reference` | PASS | The pinned Kraken reader at `4b420d579bd381796d773875e36d14c9acaabf39` independently decoded matching synthetic literal, delta, match and Huffman vectors. |
| `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_fpkg_tree.c src/sm_fpkg_tree.c -o /tmp/sm-fpkg-tree-test && /tmp/sm-fpkg-tree-test` | PASS (focused safety test) | Synthetic inner PFS tree bounds, UTF-8/path traversal rejection, inode-count cap and directory-cycle rejection passed under ASan/UBSan. |
| `mkdir -p /tmp/sm-fpkg-ampr-oracle && clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude tests/fpkg_ampr_oracle/generate.c src/sm_fpkg_index.c -o /tmp/fpkg-ampr-generate && /tmp/fpkg-ampr-generate /tmp/sm-fpkg-ampr-oracle && cargo run --offline --locked --manifest-path tests/fpkg_ampr_oracle/Cargo.toml -- /tmp/sm-fpkg-ampr-oracle/ampr_emu.index /tmp/sm-fpkg-ampr-oracle/ampr_assets.index /tmp/sm-fpkg-ampr-oracle/kraken.pkg` | PASS | The permanent test-only harness generated indexes with both C and pinned Kraken writers using a fixed timestamp, compared both byte-for-byte, opened the C outputs with pinned AMPR v0.4.0 readers, fetched the codec-2 chunk from the synthetic package, decoded it with AMPR's Kraken decoder, and byte-compared all 256 KiB. The AMPR source is test-only and is not included in SMP. |
| Read-only fixture probe: temporary C harness linked `sm_pkg.c` and all FPKG modules, parsed package metadata, then called `sm_fpkg_open_stream` | EXPECTED UNSUPPORTED | SMP metadata classification accepted `PPSA12544` as a base package. The native reader returned `SM_FPKG_UNSUPPORTED` because the layout boundary table contains a sparse extent at mount offset `0x6b52db7b`. No game files were read or extracted. The temporary harness and executable are in `/private/tmp`, not the repository. |
| Pinned reference cross-check: `CARGO_TARGET_DIR=/private/tmp/kraken-cargo cargo run --offline --locked --manifest-path /private/tmp/kraken-mount-reference/crates/kraken-mount-pkg/Cargo.toml --example ls -- '<supplied package path>'` | EXPECTED UNSUPPORTED | Kraken revision `4b420d579bd381796d773875e36d14c9acaabf39` returned `unsupported package: the layout has a sparse extent at mount 0x6b52db7b; repack with --kraken stored`. The native reader and pinned reference agree on this unsupported boundary. This is not a full-tree differential test or compatibility pass. |
| Temporary focused layout oracle: `ps5upload_fpkg::kraken_image::describe` and the C parser consumed the same synthetic descriptor bytes | PASS | Both reported one raw block at logical/stored offset zero, length 256 KiB. The descriptor and harness remained under `/private/tmp`; this is not an FPKG fixture. |

The existing suite outcomes above are macOS baseline results; the FPKG commands
are focused implementation checks. No test was reported as passing based on
source inspection.

## FPKG matrix status

All T01–T38 remain **NOT MET**. The supplied package is a real debug FPKG, but
both the native reader and pinned Kraken reject it because of a sparse extent
that the pinned reference MVP does not support. Therefore no supported real-
package tree, decoder output, or AMPR readback was produced. The pinned decoder
oracle still covers synthetic blocks only; there is no SDK build or console test.
The package remains in Downloads and was not copied into this repository.

## Validation gates

| Gate | Status |
| --- | --- |
| G0 repository/source/license audit | Source and license audit recorded. Baseline PS5 build is blocked by missing SDK. PR #200 is open, not merged. |
| G1 legal-fixture differential reader tests | NOT MET; synthetic outer-to-inner reads, retained file paths, bounded file reads, CNT fallback entries, package-to-AMPR index construction and streaming extraction exist. The supplied real package is rejected by both the C reader and pinned Kraken for the same unsupported sparse extent, so it yields no full-tree differential result. |
| G2 decoder differential/sanitizer/fuzz gates | NOT MET; native decoder and synthetic differential vectors exist, but the legal golden corpus, malformed-stream corpus, fuzzing and worst-case CPU/memory measurements remain. |
| G3 byte-identical indexes and runtime readback | PARTIAL; the C and pinned Kraken writers now produce byte-identical AMPRIDX3 and AMPRPAK4 output for one deterministic synthetic codec-2 file, and the pinned AMPR readers decode that package chunk. Broader manifest coverage and real-package vectors remain. |
| G4 safe cache crash/recovery tests | Design contract recorded; implementation and failure injection not run. |
| G5 PS5 launch/mount lifecycle | PS5-only; NOT RUN. |
| G6 Linux CI and release build | NOT RUN; requires a push-capable fork/PR and CI. |

Host-verified functionality includes the bounded FIH/PFS/CNT and outer metadata
reader through descriptor mapping and parsing, retained inner-tree paths and
bounded file reads (including the three reference CNT fallback paths), package
block mapping, package-to-AMPR index construction and a callback-based streaming
extraction path against synthetic fixtures, the standalone Kraken descriptor
parser against a synthetic descriptor also accepted by ps5-dump-forge, byte
comparison of writer output against pinned Kraken and AMPR codec-2 readback, and
the native Kraken decoder against synthetic reference vectors. No filesystem
cache sink, transaction/recovery, registration, scanner, or launch integration
is claimed as implemented or verified by this record.
