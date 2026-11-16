# illumos: no ESP file system on 4Kn disks (MacBookPro14,3 NVMe)

Found 2026-11-15 for the MacBook Pro 15" 2017 (internal Apple NVMe,
4096-byte sectors; `notes/macbookpro-2017-drivers.md` 5d). Reproduced on the
OI VM without the Mac. Source: illumos-gate c893fd03ad (= osnet 23344, the
INTENSO stick). The three files here are unchanged in illumos-usbroot.

## 1. Cause

1. The text installer runs `zpool create -f -B <pool> <disk>` (slim_source
   `usr/src/lib/libti/ti_zfm.c`). `-B` without `bootsize` gives a 256 MB
   ESP (`SYSTEM256`, cmd/zpool/zpool_main.c). On a 4Kn disk that is
   **65,536 sectors**.
2. libzfs warns about a too-small ESP only below 256 MB for 4096-byte
   sectors (libzfs_pool.c, "33MB is tested with mkfs -F pcfs"). It stays
   silent at 256 MB, but that is not enough:
   - FAT32 needs at least 65,525 data clusters;
   - with 32 reserved sectors and two FATs of 64 sectors, the smallest
     FAT32 at 4K is about 65,690 sectors (≈ 257 MB);
   - 8 × 33 MB = 264 MB is safe.
3. installboot (`cmd/boot/installboot/i386/installboot.c`, install_esp_cb)
   sees no pcfs on the ESP and runs `/usr/sbin/mkfs -F pcfs -o fat=32
   <ESP>`. mkfs refuses:
   `Requested size is too small for FAT32.` (exit 4).
4. installboot's `mkfs_pcfs()` returns `status != -1`, so a mkfs that ran
   and failed counts as success. It goes on to mount the ESP as pcfs; the
   mount fails, and `stagefs_mount()` prints nothing. No `bootx64.efi` is
   written, and the install callbacks are `void`, so installboot, bootadm
   and the installer all report success.

Result: the ESP has no file system. The loader says `open 'disk0p1' failed`,
and Apple's firmware shows no boot entry.

A second mkfs_pcfs bug (any sector size): its FAT32 check compares the
volume's sector count (minus reserved sectors) with 65,524, without
subtracting the FATs. From just above that up to about 65,690 sectors, it
exits 0 and writes an **invalid** FAT32: 65,600 sectors gave 65,440
clusters, and fsck.fat says "less than the required minimum of 65525".

Not a bug (my earlier suspicion): the 4K boot-sector writes read from
`boot_sector_t`, which is `buf[MAXBPS]` = 4096 bytes.

## 2. Reproduction on the OI VM (no 4Kn disk needed)

lofi and zvols always report 512-byte sectors, so `shim4k.c` (here) is an
LD_PRELOAD shim. It rewrites only the answer to `DKIOCGMEDIAINFO(EXT)` to
4096-byte sectors. mkfs, fstyp and the system mkfs are 32-bit, so build it
`-m32` and use `LD_PRELOAD_32`.

Steps:
- `mkfile -n 300m esp.img`, then `lofiadm -a` it;
- run `mkfs -F pcfs -o fat=…,nofdisk,size=<sectors> /dev/rlofi/N` with the
  shim, `size` = sectors as on the MBP;
- copy the image to Linux and check it with `fsck.fat -n -v` and
  `minfo`/`mdir` (mtools).

| run (4096-byte sectors) | stock mkfs | patched mkfs | image |
|---|---|---|---|
| fat=32, 65,536 (the MBP ESP) | exit 4, "too small for FAT32" | exit 4, names sectors and `fat=16` | — |
| fat=32, 65,600 | **exit 0, invalid FAT32 (65,440 clusters)** | exit 4, "65440 clusters … at least 65525" | — |
| fat=32, 65,700 | exit 0 | exit 0 | valid, 65,538 clusters |
| fat=32, 67,584 (264 MB) | exit 0 | exit 0 | valid |
| fat=16, 65,536 (with `b=EFI`) | exit 0 | exit 0 | valid FAT16: 4096 B/sector, 2 sectors/cluster, 32,749 clusters, label EFI |

On the FAT16 image (stock tools):
- `fstyp` → pcfs, FAT16;
- `mount -F pcfs` read-write works;
- `/boot/loader64.efi` copied to `EFI/Boot/bootx64.efi`: identical sha256
  through mtools on Linux, fsck.fat clean.

The loader's dosfs (`boot/libsa/dosfs.c` parsebs, with 16797) accepts 512 to
4096 bytes per sector, and takes FAT12/16 when the root-entry count is not
0. So it reads this FAT16.

Not tested:
- installboot itself on a 4Kn GPT (the shim cannot fake a 4K GPT on lofi).
  Patch 0001 is compiled only; the path it changes is the mkfs call shown
  above.
- Apple's firmware reading a FAT16 ESP: **confirmed on the
  MacBookPro14,3 (2026-11-16)**, see section 4.

## 3. Patches (against c893fd03ad; compiled on the OI VM with gcc 10 -m32)

- `0001-installboot-esp-mkfs-status-fat16.patch`:
  - `mkfs_pcfs()` succeeds only when `WIFEXITED && WEXITSTATUS == 0`;
  - install_esp_cb tries `fat=32` and then falls back to `fat=16`, with a
    message;
  - `stagefs_mount()` reports a failed pcfs mount.
  - Still open for upstream: the install callbacks are `void`, so
    installboot exits 0 even when the ESP was not written. That needs a
    return value through `pl_cb.install` (a larger change).
- `0002-mkfs_pcfs-fat32-min-clusters.patch`:
  - refuses FAT32 when data clusters ≤ 65,524, counted after the FATs;
  - both FAT32-size errors name the size and suggest `-o fat=16`.
- `0003-libzfs-esp-4kn-fat32-warning.patch`: the 4Kn warning threshold
  goes from 256 MB to 264 MB.
- Proposed, not written: on 4Kn disks a bigger default ESP. Either:
  - `zpool create -B` uses 512 MB when the disk's `efi_lbasize` is 4096;
  - or the installer (ti_zfm.c) passes `-o bootsize=512M`.

  With 0001 alone, a 256 MB 4Kn ESP already gets a working FAT16.

For our medium respin (oi-apple-installer-eval.md), 0001 would go into
`system/boot/installboot` (or the package that ships installboot), and
0002 into the pcfs package; both are userland. Upstream: an illumos issue
for each. Filing them is the user's decision.

## 4. Recovery on the MacBook Pro

**Result (user, 2026-11-16): it worked.**
- After the FAT16 ESP and the boot block, the firmware sees the NVMe and
  OI boots from it.
- The `boot-ncpus="2"` setting in /boot/conf.d is used.
- The Option boot picker lists the disk; no bless was needed. (The user
  did not say whether the picker shows "EFI Boot".)
- So FAT16 with 4096-byte sectors works with Apple's firmware, the
  loader and pcfs.

Note for the "boot from the stick" step below: `set currdev=zfs:rpool1/…`
alone at the stick's loader booted the stick's system, not the NVMe's.
The recipe still works that way, because mkfs and bootadm only need some
running OI that sees the NVMe and rpool1.

Boot OI from the NVMe as now: stick loader, `set
currdev=zfs:rpool1/ROOT/openindiana:`, `boot -B boot-ncpus=2`. Log in, then
as root (pfexec or sudo, typed by the user):

1. Find the disk: `zpool status rpool1`. The pool is on `<disk>s1`, so
   the ESP is `<disk>s0`.
2. Make sure it really is empty: `fstyp /dev/rdsk/<disk>s0`. Expected:
   `unknown_fstyp (no matches)`. If it says pcfs, stop and send the output.
3. FAT16 with label EFI (asks `(y/n)?`, answer y):
   ```
   mkfs -F pcfs -o fat=16,b=EFI /dev/rdsk/<disk>s0
   ```
4. Check: `fstyp -v /dev/rdsk/<disk>s0 | head -3` should show FAT16 and
   Bytes Per Sector 4096.
5. Write the loader:
   ```
   bootadm install-bootloader -v -f -P rpool1
   ```
   It should print `bootblock written to /tmp/iboot…/EFI/Boot/bootx64.efi`.
6. Verify:
   ```
   mount -F pcfs -o ro /dev/dsk/<disk>s0 /mnt && ls -lR /mnt/EFI; umount /mnt
   ```
7. Keep 2 CPUs until the AP-start hang is fixed, because the NVMe's own
   loader now reads rpool1's /boot:
   ```
   echo 'boot-ncpus="2"' > /boot/conf.d/boot-ncpus
   ```
   Remove the file once the CPU hang is fixed.
8. Reboot without the stick and hold Option. "EFI Boot" should appear. If
   it does not, in macOS Recovery (Terminal):
   ```
   diskutil mount disk0s1
   bless --mount /Volumes/EFI --setBoot --file /Volumes/EFI/EFI/Boot/bootx64.efi
   ```

Fallback without OI, all in macOS Recovery (untested): the FAT32 fallback
in my earlier report does not work, because newfs_msdos refuses FAT32 on
256 MB at 4K as well. Use FAT16 instead:
1. `newfs_msdos -F 16 -S 4096 -c 2 -v EFI /dev/rdisk0s1`;
2. copy `EFI/Boot/bootx64.efi` from the INTENSO stick's own ESP (FAT) to
   `/Volumes/EFI/EFI/Boot/` (`diskutil mount` both);
3. bless as above.
