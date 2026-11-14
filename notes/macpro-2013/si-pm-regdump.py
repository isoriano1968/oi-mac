#!/usr/bin/python3
#
# si-pm-regdump.py: read-only dump of the SI (Tahiti) power-management
# register block through the PCI MMIO BAR, under Linux (root), to compare
# Linux radeon's dpm setup with ours on the Mac Pro 2013 (FirePro D500).
#
#   sudo python3 -I si-pm-regdump.py 0000:02:00.0 [samples] > out.txt
#
# Reads only the registers smu_6_0_d.h defines in the CG block (clock,
# power-management and thermal) plus GRBM_STATUS (0x2004) from
# /sys/bus/pci/devices/<dev>/resource2 (BAR 2, the register aperture).
# Never the undefined offsets in between: on the Mac Pro (dpm6, 2026-11-14)
# reading every dword of 0x180-0x27f made the whole CG block read 0 from
# about 0x194 on, until reboot. No SMC_IND_INDEX/DATA access, no writes.
# Prints the registers once, then for [samples] (default 20, 100 ms
# apart) only the registers that changed, so a run under load shows what
# moves (current level, activity).
import mmap, os, struct, sys, time

dev = sys.argv[1]
samples = int(sys.argv[2]) if len(sys.argv) > 2 else 20
path = "/sys/bus/pci/devices/%s/resource2" % dev
fd = os.open(path, os.O_RDONLY | os.O_SYNC)
mm = mmap.mmap(fd, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ)
REGS = [0x180, 0x181, 0x182, 0x183, 0x185, 0x186, 0x188, 0x189, 0x18b,
        0x197, 0x198, 0x199, 0x19b, 0x19c, 0x1c0, 0x1c1, 0x1c2, 0x1c4,
        0x1c5, 0x1d5, 0x1d6, 0x1d7, 0x1dc, 0x1dd, 0x1e0, 0x1e1, 0x1e2,
        0x1e6, 0x1ef, 0x1f0, 0x1ff, 0x200, 0x201, 0x203, 0x20a, 0x21e,
        0x21f, 0x221, 0x22e, 0x2004]

def rd(r):
    return struct.unpack_from("<I", mm, r * 4)[0]

first = {r: rd(r) for r in REGS}
print("# %s %s" % (dev, time.strftime("%Y-%m-%d %H:%M:%S")))
for r in REGS:
    print("0x%04x 0x%08x" % (r, first[r]))
prev = first
for i in range(samples):
    time.sleep(0.1)
    cur = {r: rd(r) for r in REGS}
    ch = ["0x%04x=0x%08x" % (r, cur[r]) for r in REGS if cur[r] != prev[r]]
    print("# sample %d: %s" % (i, " ".join(ch) if ch else "no change"))
    prev = cur
