#!/usr/bin/env python3
"""Read or set the Samsung SBL reboot reason on espresso (OMAP4).

The stock kernel (CM cm-10.2 arch/arm/mach-omap2/sec_common.c) writes the
reason to the SAR scratchpad before a warm reset; SBL reads it on the next
boot. Mainline has no such driver, so set it from userspace, then reboot.
Verified: "recovery" boots the RECOVERY partition (docs/bringup.md B8).

Usage: samsung-reboot-mode.py show
       samsung-reboot-mode.py recovery|normal
Run as root, then `systemctl reboot`.
"""
import mmap
import os
import struct
import sys

PAGE = 0x4A326000
MODE = 0x4A326FF8  # SEC_REBOOT_MODE_ADDR (OMAP_SW_BOOT_CFG_ADDR)
FLAG = 0x4A326FF4  # SEC_REBOOT_FLAG_ADDR: "RESE" of "RESET"
CMD = 0x4A326FE8  # SEC_REBOOT_CMD_ADDR
FLAG_RESET = struct.unpack("<I", b"RESE")[0]
REBOOT_MODE_PREFIX = 0x12345670
MODES = {
    # name: (REBOOTMODE_* bit for MODE, REBOOT_MODE_* for CMD)
    "normal": (1 << 0, 0),
    "recovery": (1 << 1, REBOOT_MODE_PREFIX | 2),
}


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("show", *MODES):
        print(__doc__, file=sys.stderr)
        return 2
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    mem = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED,
                    mmap.PROT_READ | mmap.PROT_WRITE, offset=PAGE)

    def rd(addr: int) -> int:
        return struct.unpack("<I", mem[addr - PAGE:addr - PAGE + 4])[0]

    def wr(addr: int, val: int) -> None:
        mem[addr - PAGE:addr - PAGE + 4] = struct.pack("<I", val)
        got = rd(addr)
        if got != val:
            raise SystemExit(f"ERROR: {addr:#x}: wrote {val:#x}, read {got:#x}")

    if sys.argv[1] != "show":
        mode, cmd = MODES[sys.argv[1]]
        wr(MODE, mode)
        wr(FLAG, FLAG_RESET)
        wr(CMD, cmd)
    for name, addr in (("mode", MODE), ("flag", FLAG), ("cmd", CMD)):
        print(f"{name} {addr:#x} = {rd(addr):#010x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
