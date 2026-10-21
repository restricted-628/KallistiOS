#!/usr/bin/env python3
"""Actual addon C with host OS/I/O shims; no target execution/cache evidence."""
import argparse
import hashlib
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

from dco_pack import PROFILE_VM, pack
from test_package import member, section_change
from elf_plan import ELF_HEADER, PROGRAM_HEADER
from test_plan import BASE

STUB = r"""
#ifndef OVL_TEST_OS_H
#define OVL_TEST_OS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define MUTEX_TYPE_NORMAL 0
int mutex_init(mutex_t *, unsigned);
int mutex_destroy(mutex_t *);
int mutex_lock(mutex_t *);
int mutex_unlock(mutex_t *);
int irq_inside_int(void);
void icache_sync_range(uintptr_t, size_t);
extern uint32_t _arch_mem_top;
#define THD_KERNEL_STACK_SIZE (64 * 1024)
typedef int file_t;
#define FILEHND_INVALID (-1)
file_t fs_open(const char *, int);
int fs_fstat(file_t, struct stat *);
ssize_t fs_read(file_t, void *, size_t);
int fs_close(file_t);
#define FS_ISO9660_BACKEND_DIRECT 1
int fs_iso9660_get_backend(void);
#endif
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parent.parent
    addon = root / "addons/libkosoverlay"
    with tempfile.TemporaryDirectory(prefix="kos-overlay-runtime-") as directory:
        temp = Path(directory)
        (temp / "test_os.h").write_text(STUB)
        for name in ("kos/cache.h", "kos/fs.h", "kos/irq.h", "kos/mutex.h",
                     "arch/arch.h", "arch/stack.h", "dc/fs_iso9660.h"):
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "test_os.h"\n')
        (temp / "kos/cdefs.h").write_text("#include <sys/cdefs.h>\n")
        (temp / "kos/vm.h").write_text('#include "test_os.h"\ntypedef struct vm_space vm_space_t;\n')
        (temp / "kos/vm_exec.h").symlink_to(root / "include/kos/vm_exec.h")
        (temp / "kos/overlay.h").symlink_to(root / "addons/include/kos/overlay.h")
        source = (addon / "overlay.c").read_text()
        instruction = '__asm__ volatile("stc sr, %0" : "=r"(sr));'
        assert source.count(instruction) == 1
        (temp / "overlay-under-test.c").write_text(source.replace(instruction, "sr = test_sr;"))
        package, manifest = pack(member(), BASE, 8192)
        fixture = temp / "fixture.dco"
        fixture.write_bytes(package)
        vm_package, vm_manifest = pack(member(0x101ff000), 0x101ff000, 8192, PROFILE_VM)
        vm_fixture = temp / "translated.dco"
        vm_fixture.write_bytes(vm_package)
        # A host-admitted ELF with text spanning a 32 KiB stream boundary and
        # data occurring earlier in the file: no assumption of source ordering.
        large = bytearray(member())
        large.extend(bytes(70000 - len(large)))
        large[0x7ffc:0x8004] = b"TEXTedge"
        large[0x104:0x108] = b"DATA"
        phoff = ELF_HEADER.unpack_from(large)[5]
        first = list(PROGRAM_HEADER.unpack_from(large, phoff))
        first[1], first[4], first[5] = 0x7ffc, 8, 8
        PROGRAM_HEADER.pack_into(large, phoff, *first)
        large = section_change(section_change(large, 1, 4, 0x7ffc), 1, 5, 8)
        large_package, large_manifest = pack(large, BASE, 8192)
        large_fixture = temp / "large.dco"
        large_fixture.write_bytes(large_package)
        for optimization in ("-O0", "-O2"):
            executable = temp / ("check" + optimization)
            command = [args.cc, "-std=gnu17", optimization, "-g", "-Wall", "-Wextra",
                       "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       "-pthread", "-I" + str(temp), "-I" + str(addon),
                       str(here / "runtime_test.c"), str(addon / "package.c"),
                       str(addon / "sha256.c"), "-o", str(executable)]
            if sys.platform == "darwin":
                # Mach-O normally reserves the entire low 4 GiB as PAGEZERO.
                # Keep a null guard but permit a data-only P1-address fixture.
                command += ["-Wl,-pagezero_size,0x10000"]
            print("$", " ".join(command), flush=True)
            subprocess.run(command, check=True)
            subprocess.run([str(executable), str(fixture), manifest["package_sha256"]], check=True)
            subprocess.run([str(executable), "--profile", str(vm_fixture)], check=True)
            subprocess.run([str(executable), "--vm", str(vm_fixture),
                            vm_manifest["package_sha256"]], check=True)
            subprocess.run([str(executable), "--large", str(large_fixture),
                            large_manifest["package_sha256"]], check=True)
            # Independent hashlib differential, padding/block/chunk boundaries,
            # and a million-byte message. Every run uses the real addon SHA code.
            rng = random.Random(0x44436f40)
            messages = [b"", b"abc", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                        b"a" * 1000000]
            messages += [rng.randbytes(n) for n in
                         (1, 55, 56, 63, 64, 65, 119, 120, 127, 128, 129, 4097, 32769)]
            for message in messages:
                vector = temp / "vector.bin"
                vector.write_bytes(message)
                result = subprocess.check_output([str(executable), "--sha", str(vector)], text=True)
                assert result.splitlines() == [hashlib.sha256(message).hexdigest()] * 6
            print(f"PASS {optimization}: {len(messages) * 6} SHA chunk/vector comparisons", flush=True)
    print("Host shims only; SH-4 runtime, disc, cache and hardware gates remain open.")


if __name__ == "__main__":
    main()
