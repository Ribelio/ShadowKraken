#!/usr/bin/env python3
"""Exercise SMP's production title install and FPKG mount routing on host."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

from test_storage_paths import functions


def main():
    root = Path(__file__).resolve().parents[1]
    install_source = (root / "src/sm_install.c").read_text(encoding="utf-8")
    cache_cancel_type = re.search(
        r"typedef struct \{\s*const char \*title_id;\s*\} "
        r"fpkg_cache_cancel_context_t;", install_source)
    if not cache_cancel_type:
        raise RuntimeError("Cannot find FPKG cache cancellation context")
    install = functions(root, "src/sm_install.c", [
        "fpkg_cache_build_cancelled", "prepare_fpkg_candidate",
        "sm_install_register_title_dir", "mount_and_install"])
    fakelib = functions(root, "src/sm_fakelib.c", [
        "resolve_fpkg_source_directory", "mount_fpkg_source_directory",
        "has_fpkg_sandbox_layer", "mount_fpkg_source_if_needed"])
    with tempfile.TemporaryDirectory(prefix="sm-fpkg-install-lifecycle-") as directory:
        build = Path(directory)
        (build / "install_lifecycle.inc").write_text(
            cache_cancel_type.group(0) + "\n\n" + install + "\n\n" +
            fakelib, encoding="utf-8")
        binary = build / "install_lifecycle"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I" + str(root / "include"),
            "-I" + str(build), "-I" + str(root / "tests"),
            "-include", str(root / "tests/host_platform.h"),
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            str(root / "tests/test_fpkg_install_lifecycle.c"),
            "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
