# An OpenIndiana installer for the Intel Macs, and the OI-Apple Updater: evaluation (2026-11-14)

The question: can the OI text installer on the INTENSO USB stick be changed to
carry our Apple fixes, so that OI installs on every Intel Mac, the installed
system gets online, and an "OI-Apple Updater" then brings in the Apple-specific
drivers, tools and applications?

This is analysis only. No medium and no system was changed: not the INTENSO
stick, the Mac Pro SSD or any Mac. The OI VM was only read with `pkg` queries,
because an LLVM-size build was running there (load ~60).

Sources:
- illumos-gate at af490f98ee (sparse checkout ~/Sources/illumos-gate-src, read
  with `git show`);
- notes/macpro-2013/README.md and illumos-usb-root/UPSTREAM-DRAFT.md;
- notes/imac2019-network.md, notes/imac2013-imac21.md;
- notes/macpro-2013/open-topics-2026-11-13.md;
- notes/mac-unified-stack-plan-2026-11-14.md.

**The stick was read on 2026-11-14 evening (section 1.5)**, read-only. It
was in the Mac Pro  as `c4t0d0`, and HAL had
mounted it read-only at `/media/OpenIndiana_Text_X86`. Files were copied off
that mount to the Linux workstation's scratchpad. The boot archive (UFS1) and `solaris.zlib`
(lofi lzma) were opened there with two small read-only Python readers. Nothing
was mounted, written or changed on the Mac Pro or the stick. Facts still
marked **(check)** remain open after that look.

## Summary

- The stock text installer is proven on two Macs: the iMac13,2 and the
  iMac14,1 were both installed from it (2026-11-09/10), each to its internal
  disk. Both need nothing on the installer. Their fixes (audiohd, bge
  hardening, graphics) all came after the install.
- **On the Mac Pro the stock installer cannot work.** Its OI system disk is
  the SanDisk SSD on USB, and the stock kernel does not find or keep a root
  pool on USB (hubd bugs A and B). That is why OI came onto it through a
  QEMU-made stick, the hubd fix and `zfs send`, never through the installer.
  On top of that, its screen freezes without the console fix (section 2).
- Only one of our fixes is needed **on the installer** to install at all: the
  **console-on-the-right-GPU fix** (UPSTREAM-DRAFT issue C). Without it the
  Mac Pro's screen freezes after the kernel starts, and so does the installer
  UI. The MacBook Pro 15" (14,3) does not: its installer screen appears, but
  its keyboard is dead (SPI; a keyboard driver is needed on the medium,
  notes/macbookpro-2017-drivers.md).
- The **hubd root-on-USB fix** is not needed to *run* the installer. The live
  media's root is the RAM disk, so the bug cannot occur there. It is needed
  in the *installed* system when its root pool is on USB: the test stick, or
  an install to an external disk. A stock kernel there panics or suspends
  rpool, before any updater can run.
- **Installing to USB is a goal** (user, 2026-11-14: "remember we needed the
  USB installer to be able to install on the external USB SSD etc"). So the
  hubd fix, and the scsa2usb fix for USB 3 disks once it exists, must be **on
  the medium**. The installer copies the medium's packages to the target, so
  the installed system then has both fixes from its first boot. An updater
  run comes too late, because a USB-root system with the stock kernel does
  not boot that far.
- **bge** works stock on every Mac we have (attach on the iMac13,2/14,1, link
  on the Mac Pro). Our three patches only harden it, so they go into the
  updater. What can stop "getting online" is **NWAM**, not bge (the iMac14,1
  never came up with NWAM). Also, the MacBook Pro 2017 has no wired port and
  no Wi-Fi driver.
- **Recommendation: (a), a respin from packages, as the main line from the
  start.** The stock stick plus updater (c) is not enough on its own, because
  it cannot do USB-root installs without a manual step.
  - Phase 1: the first "OI-Apple" text image (r1), made with distro_const
    from hipster plus oi-local. It carries `driver/usb` with the hubd fix, the
    console fix (if it can be packaged without replacing unix, **U 3**), and
    the updater with its `p5p` repository. The updater is built in the same
    phase, because the medium carries it.
  - Phase 2: r2, with the scsa2usb `usb_pipe_xopen()` fix in `driver/usb`
    (and the console fix, if it had to wait for a unix build). If the external
    USB 3 SSD drops off in the r1 tests, the scsa2usb fix becomes a blocker
    for USB-root installs and moves into r1.
  - Until r1 exists, the stock stick stays the fallback. It covers installs
    to internal disks on the Macs it already serves. A USB-root install from
    it is possible only through the manual Shell step (section 3c).
  - Option (b), swapping modules inside the existing image, is the fastest
    but is ruled out. It is hand-copying (project rule), and the installed
    system inherits files that `pkg verify` reports as wrong and that
    `pkg fix` or `pkg update` silently reverts.

## 1. How the OI text-installer media is built

### 1.1 The build: distro_const

OI hipster images (`OI-hipster-text-<date>.iso` / `.usb`) are made by the
**Distribution Constructor**: `pkg:/install/distribution-constructor`,
command `distro_const build <manifest.xml>`. Its source is OpenIndiana's
`slim_source` (github.com/OpenIndiana/slim_source), which also holds the text
installer (`pkg:/system/install/text-install`) and the media manifests. The
build is a chain of checkpoints, each restartable with `-r`/`-p`. The
stick's boot archive was built by DC: its UFS records "last mounted on
`/rpool/distro_const/dc-text/build_data/tmp/ba_lofimnt`". The checkpoint
names below are Solaris 11 DC's **(check them against OI's slim_source)**:

1. **transfer-ips**: `pkg image-create` + `pkg install` into
   `<build_area>/build_data/pkg_image`, from the publishers in the manifest.
   This is the single point where package content enters the medium. Any
   publisher can be listed (several origins, e.g. openindiana.org and a file
   repository of oi-local).
2. **set-mediarepo / pre-pkg-img-mod**: sets the publisher the *installed*
   system will have afterwards. Also SMF profiles, the live user, and media
   services such as `live-fs-root` and `text-mode-menu`.
3. **ba-init / ba-config / ba-arch**: builds the **boot archive**
   (miniroot). The manifest's `<boot_archive_contents>` lists the files and
   directories taken from pkg_image: all of `/kernel`, `/platform`, `/etc`,
   `/lib`, `/sbin`, `/dev`, `/devices`, `/var`, `/root`, minus `/usr`. They
   are written into a UFS image, which is then compressed.
4. **pkg-img-mod**: packs `/usr` into `solaris.zlib` and `/mnt/misc`
   (`/opt` and the large `/etc` pieces) into `solarismisc.zlib`. These are
   compressed, lofi-mountable images.
5. **create-iso / create-usb**: the ISO (hybrid, bootable from USB through
   the illumos loader, BIOS and UEFI), and the `.usb` image that users write
   to a stick with `dd`.

### 1.2 What is on the stick

| Path on the medium | What it is |
|---|---|
| `boot/` + the EFI system partition (`EFI/BOOT/BOOTX64.EFI` = loader.efi) | the illumos loader (Forth menu, `boot/defaults/loader.conf`) |
| `platform/i86pc/amd64/boot_archive` | the miniroot: compressed UFS image, the whole root except /usr. **All kernel modules the installer runs with live here**: `kernel/drv/amd64/*` (bge, scsa2usb, hubd, xhci, nvme, ahci, audiohd...), `kernel/misc/amd64/usba` (hubdi.c), `platform/i86pc/kernel/amd64/unix`, `platform/i86pc/kernel/dacf/amd64/consconfig_dacf`, `platform/i86pc/kernel/misc/amd64/gfx_private` |
| `platform/i86pc/amd64/boot_archive.hash` | the **SHA-1 of the uncompressed archive** (bca65680…ad607, checked). The loader loads and checks it (`boot_archive.hash_load="YES"` in boot/defaults/loader.conf). It must be redone after any change |
| `solaris.zlib` | /usr (installer, pkg, ddu, /usr/kernel modules) |
| `solarismisc.zlib` | /mnt/misc (/opt etc.) |
| `.volsetid`, `.cdrom` | how `live-fs-root` finds its own medium after boot (it scans the disks for the matching volsetid and mounts the zlib images) |

The loader reads `unix` and the boot archive through UEFI. The kernel then
mounts the RAM disk as root (so `modrootloaded` is set at once). After that,
USB and the other devices attach in the usual way, and `live-fs-root` finds
the stick and mounts `/usr`.

### 1.3 What the installer puts on the disk

The text installer (`/usr/bin/text-install`, slim_source) partitions the
target and creates rpool and the BE. It then **copies the running live image
to the target (CPIO transfer of / and /usr), not a fresh `pkg install`**, and
removes the media-only packages (`pkg uninstall` of the live/installer bits).
This is confirmed in the stick's code (`osol_install`, Python 3.9):
- ti_install.py: `TM_PERFORM_CPIO`;
- ict.py: `pkg -R <basedir> uninstall`, `bootadm update-archive -R`,
  `bootadm update-menu`, and the `/save` files copied in.

Then it runs `installboot` on the target and sets the publishers for the
installed image.

Consequence: whatever kernel modules the medium carries become the installed
system's modules. If they came in as packages (option a), the installed
system is clean. If they were swapped by hand (option b), every swapped file
is a `pkg verify` error in the installed system.

### 1.4 Installer menu and drivers

The text-mode menu offers, besides "Install OpenIndiana": **"Install
Additional Drivers"** (`/usr/bin/ddu-text`) and "Shell", confirmed in
`/usr/sbin/text-mode-menu` on the stick, plus terminal type and reboot. DDU's
`3rd_drv_ins.sh` takes a root path as an argument. DDU installs IPS or
SVR4 driver packages into the *running* live system, from a repository or a
file. It cannot change modules that are already loaded (usba with the stick
mounted, consconfig_dacf, unix). It also cannot run before the menu is on the
screen. The "Shell" entry gives a root shell during install, and also after
the install, before the reboot. From there, `pkg -R /a ...` can install into
the new target (used in sections 3c and 4.5).

### 1.5 The INTENSO stick as read (2026-11-14)

| Item | Value |
|---|---|
| Device | Intenso Speed Line 3.00, 14.8 GB, `c4t0d0` on the Mac Pro's Fresco xHCI (storage@7) |
| Layout | hybrid ISO: hsfs on p0 ("OpenIndiana_Text_X86", System id Solaris); MBR part 1 type 0xBE (Solaris boot), part 2 type 0xEF = EFI system partition, FAT16 (`boot/efiboot.img`, 4.5 MB). The rest of the 14.8 GB is unused (`.image_info`: IMAGE_SIZE 2864805 KB) |
| Release | `/etc/release`: OpenIndiana Hipster 2026.10, assembled 30 October 2026 |
| Kernel | `unix`: **illumos-c893fd03ad** October 2026 |
| osnet | **osnet-incorporation 0.5.11-2026.0.0.23344** (20261030T181133Z); `system/kernel`, `system/kernel/platform`, `driver/usb`, `driver/network/bge`, `driver/storage/nvme` all at 23344. 623 packages in the image |
| Installer | `system/install/text-install` 2026.0.0.1134 (osol_install, Python 3.9); `package/pkg` 2026.0.0.5638 |
| Boot archive | `platform/i86pc/amd64/boot_archive`: 64.4 MB gzip, holding 243.5 MB of UFS1, 13,744 entries; `boot_archive.hash` = SHA-1 of the uncompressed UFS |
| /usr | `solaris.zlib` 358.7 MB: lofi, lzma, 128 KiB segments, each with a 1-byte compressed flag; holds a 1.45 GB hsfs. `solarismisc.zlib` 115 MB |
| Modules in the archive | `kernel/misc/amd64/usba`, `kernel/drv/amd64/{hubd,scsa2usb,xhci,bge,nvme,ahci,usbecm,pcieb,vgatext}`, `platform/i86pc/kernel/{dacf/amd64/consconfig_dacf,misc/amd64/gfx_private,misc/amd64/pcie,amd64/unix}`. **No audiohd, no i915/drm**: no GPU or audio driver on the installer, as section 2 wants |
| Aliases | bge `pci(ex)14e4,1682` and `,1686` (57762, 57766); nvme `pciclass,010802` only; usbecm `usb,class2.6.0` |
| Finding the medium | `/lib/svc/method/media-fs-root`: after the RAM-disk root, `listusb` + fstyp hsfs/ufs, matching `.volsetid`, then lofi of `solaris.zlib` to /usr. So the live system never has a USB *root pool*, which confirms that the hubd bugs do not hit the installer itself |

The gate commit matters most. **The medium's kernel is c893fd03ad, osnet
23344: the same commit as the OI VM, `~/src/illumos-usbroot` (usb-root-scan,
console-fb) and `ports/bge-57766`.** The modules built there match this
medium exactly. A respin of this osnet build needs no rebase and no new
nightly for hubd/console. The Mac Pro itself runs a newer build
(illumos-586678ac1b, osnet 23395), so a respin on a newer hipster build needs
the rebase from step 4.

## 2. Which fixes must be on the installer

Per fix: in which module and package it lives, whether it blocks the install,
and whether it blocks getting online.

| Fix | Module (package) | Blocks the installer? | Blocks the installed system? | Where |
|---|---|---|---|---|
| **Console on the GPU that owns the UEFI framebuffer** (issue C: `consplat.c`, `gfxp_fb.c`, `plat_dev_owns_boot_fb()` in `ddi_impl.c`) | `platform/i86pc/kernel/dacf/amd64/consconfig_dacf`, `.../misc/amd64/gfx_private` and `unix` (all `system/kernel/platform`) | **yes on the Mac Pro**: "cannot find driver for screen device", the screen freezes after boot, so the installer UI is never seen. **Not** the MacBook Pro 15" (14,3, iGPU + dGPU): the stock installer screen appears there (user, 2026-11-15); only the keyboard is dead (SPI, see notes/macbookpro-2017-drivers.md). No: iMac13,2, iMac14,1, Polaris iMacs (one visible GPU; the 13,2/14,1 installs prove it) | same | **installer** |
| **hubd root on USB**, 2 bugs (`hubdi.c`: BUS_CONFIG_ALL before root, attach-time pass) | `kernel/misc/amd64/usba` (`driver/usb`). The patches are `notes/macpro-2013/illumos-usb-root/0001-…-not-found-after-it-moves-to-anot.patch` and `0002-…-attach-time-hub-pass-resets-the-USB-root-disk-.patch` (there is no file named `0001-hubd-usb-root-relocation.patch` any more); tree `~/src/illumos-usbroot`, branch usb-root-scan | **no**: the installer's root is the RAM disk. Both bugs need a USB *root pool* | **yes, if rpool is on USB**: panic "cannot mount root" on another port/Mac (bug A), rpool suspended right after boot (bug B, also with the right port). No, for an install to an internal disk | **installer, required** (USB-root installs are a goal): the medium carries it, and the installer copies it to the target |
| **ESP on 4Kn disks** (installboot ignores mkfs's exit status; mkfs refuses FAT32 on the 256 MB ESP = 65,536 sectors of 4096 bytes) | `usr/sbin/installboot` (installboot fallback to FAT16: `notes/illumos-esp-4kn/0001-…`), `mkfs` pcfs (0002), libzfs warning (0003); userland, not the boot archive | **yes**: the installer runs the live image's installboot, which leaves the target ESP without a FAT, while the install reports success | **yes**: the target does not boot, and the Option menu does not show it. Seen on the MacBookPro14,3 NVMe (2026-11-15); every Mac with a 4Kn SSD | **installer, required** (r1): installboot in the live image's /usr (solaris.zlib); the updater then keeps it current. Until then: manual recipe, illumos-esp-4kn README 4 |
| **scsa2usb `usb_pipe_xopen()`** (USB 3 disk drops after attach) | `kernel/drv/amd64/scsa2usb` (`driver/usb`) | low risk: the installer runs from a USB 3 stick, and a drop mid-install would end the install. Seen once, on the Seagate HDD; never on sticks. **No fix written yet** | **yes, for root on a USB 3 disk** (the external USB SSD): a drop suspends rpool. illumos has no UAS, so USB 3 SSD enclosures run through scsa2usb's bulk-only path | **installer**, in r2; moves into r1 if the r1 tests show drops |
| **pciehp stuck power fault** (issue D) | `kernel/misc/amd64/pcie` (`system/kernel`) | no: the second D500 stays offline, which is harmless for the install | no | updater |
| **bge BCM57766/57762** (`ports/bge-57766`, patches 0001-0003) | `kernel/drv/amd64/bge` (`driver/network/bge`) | no | **no**: stock bge already has 57762 (14e4:1682, Mac Pro) and 57766 (14e4:1686, iMacs) in `bge_hw.h` and the aliases in `driver-network-bge.p5m`. It attaches on the iMac13,2 and 14,1 and runs bge1 on the Mac Pro. 0001 (DMA areas below 2 GB) guards against an intermittent reset on machines with a lot of RAM; 0003 (placeholder MAC) matters only on T2 Macs, which we do not have | updater |
| NWAM does not bring up bge (iMac14,1: "no link-up event before plumbing"; Mac Pro: DHCP NAK, NoNet) | configuration, nwamd | no | **can block "online"** | installer choice, or the updater (offline repository), see 4.6 |
| audiohd Apple (CS4206 GPIO, HDMI/DP) | `driver/audio/audiohd` | no | no | updater |
| applesmc + smcstat, Mac Sensors | `driver/sensor/applesmc`, `desktop/system-monitor/mac-sensors` | no | no | updater |
| illumos-drm (amdgpu SI, i915n), firmware, Mesa 26, Xorg 21.1 | drm p5p, x11/*, mesa | **must not be on the installer**: the installer runs on the firmware framebuffer, and that works on every Mac. A DRM driver on the medium is risk with no benefit. Stock i915 on the iMac14,1 even breaks the console ("VBT signature missing") | no | updater |
| HAL (NTFS/GPT/eject), vt-hotkeys, PulseAudio, apple-media-keys | service/hal, system/vt-hotkeys... | no | no | updater |

Not ours yet, but it decides whether a Mac can be installed at all:
- **MacBook Pro 2017**:
  - Internal disk: Apple NVMe. illumos nvme binds by class 010802 only. Whether
    Apple's controller reports that class and works without Linux's Apple
    quirks is unknown **(check: `prtconf -v` from the installer's shell)**.
  - Keyboard/trackpad: SPI, no driver, so a USB keyboard is needed for the
    install. **Confirmed by the user on 2026-11-15**: the stock installer
    boots from the stick, but the keyboard is dead. The drivers needed (LPSS
    SPI + Apple topcase, modelled on FreeBSD's BSD-2 intelspi/atopcase) are
    in notes/macbookpro-2017-drivers.md. Once they exist they belong in the
    respun medium's boot archive, because the keyboard is needed before any
    menu.
  - Network: no wired port, and the BCM43602 has no driver. Options: a USB
    Ethernet adapter with a CDC-ECM configuration (usbecm), or Thunderbolt
    Ethernet (BCM57762 = bge, if the Thunderbolt link is up from power-on).
    Both are untested. Without either, the updater must come from the stick
    (section 4.4).
- **iMac19,1 / Polaris iMac**: same NVMe question **(check)**. Network = bge
  57766.
- **Mac Pro 2013**: its OI system disk is **the SanDisk Portable SSD 1 TB on
  USB** (`c3t0d0`, storage@1 on the Fresco xHCI). The internal SSD keeps
  macOS. So on the Mac Pro, **a USB-root install is the normal case**, not an
  option.
  - OI never got onto it through the installer. On 2026-11-11 the Verbatim
    stick had been made in QEMU, and the stock kernel on the Mac Pro panicked
    with "cannot mount root". Bug A: the label path was the VM's, and before
    root only that port is enumerated. Guessed `bootpath`s for storage@1, 2
    and 6 failed too.
  - It only booted with the hubd fix, which also stops bug B (rpool
    suspended right after boot). The SanDisk system was then made with
    `zfs send` of that BE (README, "SSD system", 2026-11-12).
  - **A stock installer would have failed here twice:**
    - console fix missing: the screen freezes, so the installer cannot be
      used;
    - hubd fix missing: even if the install to the SanDisk had finished, the
      stock kernel would not have found or kept its rpool on USB. Bug B hits
      even on the same port; bug A hits on any other port or Mac.

So the installer needs three fixes:
- the console fix, so it works on all our Macs;
- hubd, so that an install *to a USB disk* (the external USB SSD, a stick)
  can boot;
- scsa2usb, so such a disk stays attached on USB 3.

Everything else waits for the updater.

Two more points to check for USB targets:
- The text installer must list the USB disk as a target. The stick's code
  suggests it does. libtd skips only the drive it booted from ("Drive %s is
  install media. Skipping."). It records `removable` per disk, but nothing
  in `osol_install` filters on it. **(check in QEMU)**
- `installboot` must put `EFI/BOOT/BOOTX64.EFI` on the USB disk's ESP, so
  the Mac's Option-key menu shows it as "EFI Boot". The Verbatim stick was
  set up that way, but in QEMU, not by the installer on a Mac.

## 3. Options

### (a) Respin the medium from packages that carry the fixes

How:
1. Our kernel fixes become packages with the same name as OI's and a higher
   version (the audiohd-apple pattern, 0.5.11-2026.0.0.<build>.1, with
   `incorporate` on the matching osnet-incorporation):
   - `system/kernel/platform` with the console fix (unix, consconfig_dacf,
     gfx_private);
   - `driver/usb` with the hubd fix (and later scsa2usb).

   Both are built from our illumos-gate tree at **exactly the gate commit of
   the osnet build the medium uses**. That is a full nightly of that commit
   (unix cannot be built on its own), as ~/src/illumos-usbroot already does.
   The whole package is republished, built from that tree, never a single
   file. Published to oi-local.
2. A distro_const manifest = OI's text manifest (from slim_source, unchanged
   otherwise) plus oi-local as a second publisher in `transfer-ips`. The
   replaced packages are pinned to oi-local's versions. Also a
   `set-mediarepo` that leaves oi-local configured in the installed system,
   and a package or file that puts the updater's p5p on the medium.
3. Build on OI: the OI VM, or better a separate BE or zone. distro_const
   needs root and some 10-20 GB.
4. Write the image to a *new* stick, not the INTENSO, so the stock one
   stays as the fallback. Test on every Mac.

Effort:
- About 1 day for the two kernel packages (the code exists, tested on the
  Mac Pro). Without the nightly itself: one full nightly is about 1-2 h at
  -j14.
- About 1 day for the DC manifest and the first image.
- Then one test round per Mac.
- Every new OI snapshot needs a rebuild (new gate commit), until the fixes
  are in illumos-gate. After that, plain OI media will do.

Risk: low for the machines, because everything is a package and `pkg
verify` stays clean. The real risks:
- (i) The gate-commit match: an osnet build that differs from our modules'
  base gives a kernel that does not boot. The fix is the exact commit
  (`pkg contents -m system/kernel | grep git-rev`) and a test in QEMU first.
  For osnet 23344, the build on the INTENSO stick, it already matches: it is
  c893fd03ad, our trees' base (section 1.5).
- (ii) Replacing `unix` is the most delicate part. It can be avoided by moving
  `plat_dev_owns_boot_fb()` out of ddi_impl.c into consconfig_dacf or
  gfx_private (it only reads `fb_info` and the device's
  `assigned-addresses`). Then only two modules change, both in
  `system/kernel/platform` (**U 3**).
- (iii) DC on hipster: OI's own image builds are done by their Jenkins.
  Building locally may need slim_source's manifests from the same date.

### (b) Swap the modules inside the existing image

How: copy the .usb image off the stick (or download it again), gunzip the
boot_archive, `lofiadm -a` + `mount -F ufs`, replace usba, consconfig_dacf,
gfx_private and unix, unmount, compress, redo `boot_archive.hash`, write the
image back.

Effort: a few hours.

Risk: medium to high.
- The modules must match the kernel of the medium. Here that is easy: the
  medium is c893fd03ad, the same as our builds (section 1.5). So (b) would be
  technically quick, but the next two points still rule it out.
- The medium no longer matches its own packages, and the installer copies
  that into every installed system: `pkg verify` errors on usba/unix, and
  `pkg fix` or the next `pkg update` puts the stock modules back. A USB-root
  system then stops booting.
- It is a hand copy, against the project's packaging rule.
- It needs writing the INTENSO stick.

Not recommended. At most a throwaway test, once, of whether the console fix
lets the Mac Pro install: on a scratch stick, and only with the user's OK.

### (c) Medium unchanged, fixes as an add-on

How: the stock stick stays as it is. The Apple fixes come in two ways:
- **during the install**, through "Install Additional Drivers" (DDU) or the
  "Shell" entry: `pkg -R /a install` from oi-local into the *target* before
  the first reboot;
- **after the install**, through the updater (section 4).

Effort: small. It is the updater work, which is needed anyway.

Risk: low. It cannot help with anything that must work before the installer
UI is on the screen: **the Mac Pro (console) and the MBP 15" (SPI keyboard) still cannot be
installed this way**. A USB-root install works if the shell step installs
the hubd package into `/a` before the first boot. That step is manual and
easy to forget.

Good enough today for: iMac13,2, iMac14,1, the Polaris iMacs, iMac19,1, and
internal-disk installs. **Not enough for the USB-root goal**: it depends on
a manual step at the right moment, and a missed step leaves a system that
does not boot.

### Recommendation

**(a) from the start**, phase 1 = r1 with hubd (+ console), phase 2 = r2 with
scsa2usb.
- USB-root installs are a goal, and only a medium that carries the fixed
  `driver/usb` gives them to the installed system from its first boot,
  without a manual step.
- (a) is also the only clean way to the Mac Pro (console) and the MBP 15"
  (SPI keyboard driver in the boot archive).
- It reuses the same packages, so the installer and the updater carry the
  same bits.
- (c) stays as the fallback until r1 exists.
- (b) is out (section 3b).

## 4. The OI-Apple Updater

### 4.1 Packages

All on the one publisher **oi-local**. The repos and p5p files the notes
still name (oi-gnome, oi-games, amix, the `illumos-drm` publisher in the drm
p5p) must be republished into oi-local first: that is the one-publisher rule
(step 2 in section 5).

Top level:
- `metapackages/oi-apple` (same style as `metapackages/amix-cross-dev`):
  `require` on the common set and on `system/apple/oi-apple-updater`. Nothing
  model-specific in it.
- `system/apple/oi-apple-updater`: the updater itself (section 4.3), the
  model table, and the publisher setup.
- Group packages per area, each a plain package of `depend` actions, so they
  can be added and removed one at a time:

| Group package | Contains (FMRI: source) | For |
|---|---|---|
| `group/apple/base` | `driver/usb` hubd(+scsa2usb) fix: illumos-usbroot, already on the medium from r1 (the updater keeps it current); `system/kernel/platform` console fix (r1 or r2, U 3); `driver/network/bge` 57766/57762 hardening: ports/bge-57766, **to be packaged like audiohd-apple**; pciehp fix (`system/kernel`) once packaged; `system/vt-hotkeys`: packages/system/vt-hotkeys | every Mac |
| `group/apple/smc` | `driver/sensor/applesmc` (incl. smcstat): ports/applesmc | every Mac (the SMC is on all of them) |
| `group/apple/audio` | `driver/audio/audiohd` (Apple CS4206/CS4208 GPIO, HDMI/DP fixes): ports/audiohd-apple; `library/audio/pulseaudio` r7: packages/desktop/pulseaudio | every Mac |
| `group/apple/storage` | `service/hal` .2 (NTFS, GPT, EFI-ignore, eject): ports/hal-ntfs (+hal-gpt-volumes, hal-fuse-mounts); `system/file-system/fstyp-ntfs`, `system/file-system/ntfsprogs`, `system/hal/probe-storage-gpt`, `system/hal/removable-media-policy`, `system/hal/storage-fuse`: packages/sysutils/* | every Mac |
| `group/apple/desktop` | `desktop/system-monitor/mac-sensors`: ports/mac-sensors; `desktop/gnome/apple-media-keys`, `service/gnome/desktop-cache`: packages/desktop/*; GNOME 45 set (optional, its own group `group/apple/gnome`) | Macs with a desktop |
| `group/apple/gfx-stack` | libdrm 2.4.134 (gfx-drm), Mesa 26.2.6 r2, Xorg 21.1.25 r2 + input drivers (~/src/oi-userland-mesa26, -xorg21). Published and installed as **one set** (graphics-stack rule) | Macs with one of our DRM drivers or llvmpipe; **not** the iMac13,2 (NVIDIA 470 stays on OI's Xorg) |
| `group/apple/drm-amdgpu-si` | illumos-drm unified (drm-core, linuxkpi, drm-helpers, drm-kms, amdgpu, drmadm), `system/firmware/amdgpu-si` (plan 4, step C) | Mac Pro 2013 |
| `group/apple/drm-i915n` | illumos-drm unified (i915n), `system/firmware/i915` (kbl_dmc, later) | iMac14,1; MBP 13"/15" iGPU later |
| `group/apple/drm-amdgpu-polaris` | amdgpu VI + `system/firmware/amdgpu-polaris` | Polaris iMacs, MBP 15" (after plan step D) |

Packages that replace OI packages (audiohd, hal, pulseaudio, bge, driver/usb,
system/kernel/platform, desktop-cache) keep doing what audiohd-apple does:
- same FMRI, version = OI's + `.1`;
- `incorporate` on the osnet build they were built for, so a newer osnet
  build waits for a rebuild instead of putting the stock module back;
- installing them needs `facet.version-lock.<pkg>=false`, which the updater
  sets.

### 4.2 Each Mac gets only what it needs

IPS has no "install if this hardware is present" dependency: `conditional`
only looks at other packages, and variants are per image, not per device.
So the choice happens in the updater, from a data file in its package
(`/usr/share/oi-apple/models.conf`):

```
# smbios product   groups
MacPro6,1          base smc audio storage desktop gfx-stack drm-amdgpu-si
iMac14,1           base smc audio storage desktop gfx-stack drm-i915n
iMac13,2           base smc audio storage desktop            # NVIDIA: OI's Xorg + nvidia-470
iMac18,2 iMac19,1  base smc audio storage desktop gfx-stack  # framebuffer until drm-amdgpu-polaris
MacBookPro14,*     base smc audio storage desktop gfx-stack
*                  base smc audio storage                    # unknown Mac: safe set only
```

The model comes from `smbios -t SMB_TYPE_SYSTEM` (Product). A second check on
PCI IDs (`prtconf -v`: 1002:679e, 8086:0d22, Polaris IDs, 10de:*) refines it,
so an unknown model with known hardware still gets the right driver group,
and a known model with an unexpected GPU gets none. The kernel driver
packages bind only to their own aliases anyway, so a wrong guess costs disk
space, not a broken boot. Only `drmadm select` and the X stack really depend
on the model. A new Mac = a new line and a new updater version, no code
change.

### 4.3 What the updater does

`oi-apple-update` (shell, root through pfexec/sudo; a GUI later, e.g. in
Mac Sensors' style):
1. Checks that it runs on a Mac (smbios vendor "Apple Inc."). Reads the model
   and the PCI IDs, and prints the plan (groups, packages, the OS packages it
   replaces).
2. Sets up oi-local (4.4) and checks that its osnet build matches the
   installed one. If not, it stops with "rebuild needed" (the incorporate
   rule).
3. Unlocks the version-lock facets of the replaced OS packages.
4. `pkg install --be-name oi-apple-<date> metapackages/oi-apple
   group/apple/<…>`: always into a **new BE**, never the running one (most of
   it is reboot-needed kernel content).
5. In the new BE (`beadm mount`): `drmadm -R <mnt> select …` where the model
   has a DRM group, then `bootadm update-archive -R`.
6. Says: reboot, the old BE stays in the loader menu. With a `--once` option:
   `beadm activate -t`, so a failed boot falls back by itself (the Mac Pro
   practice).
7. Later runs = the same command: new versions on oi-local become a new BE.

Options:
- `--root /a`: the same run into an alternate root, for the installer's Shell
  (section 3c with the stock stick; the OI-Apple media does not need it for USB-root installs);
- `--dry-run`: `pkg install -nv`.

### 4.4 Where the repository is served from

One publisher, **several origins** (`pkg set-publisher -g <a> -g <b>
oi-local`):

| Origin | For | Notes |
|---|---|---|
| **`oi-apple-<osnet>.p5p` on the stick** (file origin: pkg uses a p5p archive directly as a read-only origin) | the first run: no network needed. The MBP 2017 and any Mac where NWAM did not come up depend on it | from r1: on the installer medium itself; with the stock fallback stick: a second stick (a FAT or UFS partition after the image, or in `solarismisc.zlib`). A FAT32 partition can be written from macOS too. Size: base/smc/audio/storage are small; gfx-stack (Mesa+LLVM) and GNOME are hundreds of MB, so those are the large part |
| **http** on the LAN | updates after the install | `pkg.depotd` (svc:/application/pkg/server) on the OI VM, read-only. The VM's address changes with DHCP, so a fixed address or name is needed. Alternative: Apache on the VM host (Debian) with the configuration `pkg.depot-config` generates. **U 4** |

The p5p is made from oi-local with `pkgrecv -a -d oi-apple.p5p
metapackages/oi-apple group/apple/*` (with dependencies, `-r`), as the
gnome45/oi-games p5p files are today.

### 4.5 Order on a newly installed Mac

1. Install from the OI-Apple stick (r1/r2). Target: the internal disk or an
   external USB disk; both boot, because the medium's `driver/usb` goes to
   the target. Choose the network setting (4.6).
2. Only with the stock stick (the fallback), and only for a USB target:
   before the first reboot, Shell, `oi-apple-update --root /a` from a p5p on
   a second stick. Without it, the USB-root system does not boot.
3. First boot. `oi-apple-update` from the stick, or from http if the network
   is up. Reboot into the new BE.
4. Then `pkg update` (OI + oi-local) for later changes.

### 4.6 Getting online

- In the installer, "Manually" (static) or "Automatically" (NWAM). NWAM failed
  on the iMac14,1, and the Mac Pro got a DHCP NAK and fell into NoNet. The
  updater runs from the stick, so it does not depend on the network.
- `oi-apple-update --net dhcp`: optional, asked, not silent. It switches to
  DefaultFixed (`svcadm disable nwam`, `enable physical:default`) and runs
  `ipadm create-addr -T dhcp` on the first bge/usbecm link with a link, plus
  DNS in nsswitch.conf. This is what was done by hand on the iMac14,1. It
  stays an updater action, not a package that rewrites network
  configuration on its own.
- The cause of NWAM's failure (link-up event, nwamd `sysevent_subscribe_event:
  Invalid argument`) remains an open topic for illumos, separate from this.

## 5. Implementation steps

Phase 1 (OI-Apple installer r1: hubd + console + updater):
1. ~~Read the INTENSO stick, read-only~~: **done 2026-11-14** (section 1.5):
   osnet 23344, c893fd03ad, CPIO installer, SHA-1 hash, the menu.
2. Merge the old publishers into **oi-local** (oi-gnome, oi-games, amix, the
   illumos-drm p5p): `pkgrecv` into oi-local and rebuild the catalog. No
   rebuilds needed.
3. Pick the osnet build the medium will carry (**U 5**). The simplest is
   **23344**, the build of the INTENSO medium, which is c893fd03ad = our
   trees' base. A newer hipster build (23395 or later) means a rebase first.
   All our kernel packages are built against that one commit.
4. For 23344: package from illumos-usbroot as it is. For a newer build:
   rebase illumos-usbroot (usb-root-scan, console-fb) onto that commit and
   run one nightly. Either way, package:
   - the hubd fix as `driver/usb` .1 (a whole-package republish built from
     the tree, incorporate on that osnet build);
   - the console fix as `system/kernel/platform` .1: decide **U 3** first
     (unix or not). If unix has to be replaced, the console fix can move to
     r2 so that r1 is not held up.

   Test in QEMU: usb-storage root on qemu-xhci at a new port (bugs A and B),
   std vga UEFI (console, no regression).
5. Package bge-57766 like audiohd-apple (`driver/network/bge` .1, same
   osnet build). It goes into the updater, not the medium.
6. New components `packages/meta-packages/oi-apple` (metapackage + groups)
   and `packages/system/oi-apple-updater` (script, models.conf, man page),
   with pkglint against the hipster cache. Make the p5p with `pkgrecv`.
7. distro_const on OI: install `install/distribution-constructor` (system
   package: **ask first**). Take the text manifest from slim_source of the
   same date. Add oi-local (`driver/usb` .1, `system/kernel/platform` .1 if
   ready, the updater) and the p5p. First build in a scratch dataset
   (≥ 20 GB free in rpool beforehand).
8. QEMU boot of r1 (UEFI, from usb-storage on qemu-xhci):
   - is the USB disk listed as a target (section 2)?
   - install to a virtual disk and to a virtual USB disk;
   - boot the USB install from another port (bug A), check rpool after
     boot (bug B), `pkg verify` clean;
   - then run the updater from the medium.
9. Write r1 to a **new** stick (never the INTENSO, the Verbatim test stick or
   the Mac Pro SSD without the user's OK at that moment). Install tests, one
   Mac at a time:
   - install to the **external USB SSD**, then boot it on another port and
     on another Mac, plus many plug cycles of other USB devices; watch for
     "Device is gone" (scsa2usb);
   - Mac Pro first: the installer screen (console fix), then an install to a
     **spare** external USB SSD. Never the SanDisk, which is the Mac Pro's
     running rpool. Then boot it from another port;
   - internal-disk installs on the iMac14,1, iMac13,2, a Polaris iMac and the
     MBP 2017 (NVMe/keyboard/network checks from section 2 first).
10. Run the updater on each installed system (new BE, `beadm activate -t`).

Phase 2 (r2: scsa2usb):
11. scsa2usb `usb_pipe_xopen()` fix (open-topics 2.3), into `driver/usb` .2.
    Test it with the Seagate, the TOSHIBA and the external USB SSD on the
    Fresco ports. If step 9 showed drops on the SSD, this step comes before
    r1 is used for USB-root installs.
12. Respin r2 (console fix too, if it was left out of r1), then the QEMU and
    Mac tests again (steps 8-9). Installed systems get `driver/usb` .2
    through the updater or `pkg update`.
13. Upstream: issues A-D and scsa2usb to illumos (UPSTREAM-DRAFT.md), so
    that the respin can eventually be dropped.

## 6. Decisions for the user (U)

Decided:
- USB-root installs (external USB SSD, stick as the system disk) are a goal
  of the installer (user, 2026-11-14). So the hubd fix goes on the medium
  in r1, and scsa2usb in r2. This was question 5 in the first version of
  this note.

Open:
1. r1 without scsa2usb, as proposed (it moves into r1 only if the SSD tests
   show drops), or wait for scsa2usb before the first respin?
2. Which stick for the OI-Apple image (a new one is recommended; the INTENSO
   keeps the stock installer as the fallback). Which external USB SSD to
   test USB-root installs on.
3. Console fix: keep `plat_dev_owns_boot_fb()` in unix (as in the upstream
   draft), or move it into consconfig_dacf/gfx_private, so the packages do
   not replace unix. With unix it may move to r2.
4. http origin: pkg.depotd on the OI VM (needs a fixed address) or Apache on
   the VM host, or no http at all (p5p only).
5. Which osnet build the medium carries:
   - **23344** (= the INTENSO medium = c893fd03ad = our trees, no rebase;
     recommended for r1);
   - or newer (23395 as on the Mac Pro, or the newest hipster build: rebase
     + nightly first).

   Every kernel package is built against it. Installed systems can
   `pkg update` later, with rebuilt Apple packages.
6. iMac13,2: keep it out of `gfx-stack` (NVIDIA 470 on OI's Xorg), as
   proposed?
7. `oi-apple-update --net dhcp`: wanted, or network setup stays manual?
8. Installing `install/distribution-constructor` on the OI VM (or in a
   zone/BE of its own) for step 7.
