# Mac Pro 2013 (MacPro6,1): GPU facts from Linux (LMDE 7 live DVD, 2026-11-11)

Raw files here: linux-dmesg-lmde7-live.txt (`sudo dmesg`),
linux-lspci-nnvvk-lmde7-live.txt (`sudo lspci -nnvvk`). Kernel
6.12.48+deb13-amd64 (Debian 13), firmware "BIOS 481.0.0.0.0 10/07/2023".
Wi-Fi (BCM4360) has no driver on the live DVD; Ethernet (tg3) works.

## The two GPUs

| | 06:00.0 | 02:00.0 |
|---|---|---|
| chip | Tahiti LE 1002:679e | Tahiti LE 1002:679e |
| subsystem | Apple FirePro D500 106b:0125 | Apple FirePro D500 106b:0126 |
| role | display GPU: efifb on its BAR0 (0x90000000, 1600x900x32), radeon minor 0, 6 DP connectors (DP-1..6, UNIPHY/1/2) | compute GPU: radeon minor 1, "Cannot find any crtc or sizes" (no outputs) |
| BAR0 | 0x90000000, 256 MB prefetchable (VRAM aperture) | 0x80000000, 256 MB |
| BAR2 | 0xa0600000, 256 KB (MMIO) | 0xa0700000, 256 KB |
| I/O | 0x2000 | 0x3000 |
| ROM BAR | 0xa0640000, 128 KB, disabled | 0xa0740000, 128 KB, disabled |
| link | PCIe 3.0 x16 (8 GT/s), LnkSta = LnkCap | same |
| VRAM | 3072 MB, 384-bit DDR, BAR 256 MB | same |
| audio | 06:00.1 HDMI/DP audio (snd_hda_intel, vga_switcheroo client) | 02:00.1 |

The firmware framebuffer is at 0x90000000 = the display GPU's BAR0, like
the iMac21's Intel case (console takeover must find it there).

## Linux driver behaviour

- radeon binds both (radeon 2.50.0). amdgpu loads, says "SI support
  provided by radeon. Use radeon.si_support=0 amdgpu.si_support=1 to
  override" and leaves both: Linux's default for Tahiti is radeon.
- VBIOS: "ATOM BIOS: Tahiti"; radeon prints no source line. No ATRM/VFCT
  messages, ROM BAR disabled at boot: most likely the ACPI VFCT table or the
  PCI ROM (radeon tries ATRM, then ACPI VFCT, then ROM BAR, then platform);
  to pin it down boot once with drm.debug=0x4 or check /sys/firmware/acpi/tables/VFCT.
- Firmware: "Loading tahiti Microcode" (radeon/tahiti_{pfp,me,ce,mc,rlc,smc}.bin),
  VCE 50.0.1/17, UVD initialised; dpm initialised on both.
- Rings 0..7 and IB tests pass on the display GPU (GFX, 2 compute, 2 DMA,
  UVD, 2 VCE). PCIe gen 3 enabled. GART 2048 MB. MSI (limited to 32-bit).
- One error at boot: `si_dpm_set_power_state: si_restrict_performance_levels_before_switch failed`
  (display GPU, 4.6 s), harmless on Linux.
- apple_gmux: "Found gmux version 4.0.11 [indexed]" then a WARNING in
  gmux_probe (apple-gmux.c:884); vga_switcheroo enabled. The Mac Pro has no
  mux to switch (both GPUs fixed roles); the gmux device is only for
  backlight/legacy; nothing to port for display.
- Display path: radeon's legacy DCE6 code (si/evergreen display), not DC.
  With amdgpu.si_support=1 Linux would use amdgpu's DCE6 legacy code
  (dce_v6_0) unless amdgpu.dc=1.

## VBIOS source (check 2026-11-11, root, read-only)

- ACPI tables: APIC DMAR DSDT FACP FACS HPET MCFG SSDT1-8 **VFCT**.
- PCI ROM BAR (`echo 1 > /sys/bus/pci/devices/<d>/rom; head -c 16`):
  02:00.0 (compute GPU) read fails with an I/O error; 06:00.0 (display GPU)
  returns 0 bytes. Neither GPU serves its VBIOS through the ROM BAR.
- So the VBIOS comes from the ACPI **VFCT** table (radeon/amdgpu
  `radeon_acpi_vfct_bios` / `amdgpu_acpi_vfct_bios`, matched by PCI
  bus/device/function). Whether VFCT carries an image for both GPUs or only
  the display GPU is not yet known (dump VFCT: `cat /sys/firmware/acpi/tables/VFCT`).
- amdgpu on SI can also read the ROM through MMIO (ROM_INDEX/ROM_DATA), a
  fallback if VFCT lacks the compute GPU.

## VFCT contents (VFCT.bin, 131232 bytes, OEM "APPLE " "Apple00")

| image | PCI | IDs | subsystem in VFCT | size | content |
|---|---|---|---|---|---|
| 0 | 02:00.0 (compute GPU) | 1002:679e | 0000:0000 | 65536 | 55 aa, ATOM BIOS |
| 1 | 06:00.0 (display GPU) | 1002:679e | 0000:0000 | 65536 | 55 aa, ATOM BIOS |
| 2 | 00:00.0 | 0000:0000 | | 0 | end marker |

VFCT has a VBIOS for both GPUs, matched by bus/device/function. The VFCT
entries carry subsystem 0000:0000 (the real ones are 106b:0125/0126), so a
reader must match on vendor/device + bus/dev/func, not on the subsystem.
VFCT.bin holds Apple's/AMD's VBIOS images: keep it local, never commit or
publish it.

## 2026-11-11 evening: OI on the Verbatim stick, root mount

- The loader sees the stick (disk1) and rpool ONLINE; the kernel panics "cannot mount root"
  because the label path is the VM's (`/pci@0,0/pci1af4,1100@4/storage@5/disk@0,0:b`).
- `set bootpath=...` is overwritten by the loader; `-B bootpath=/pci@0,0/...` is split at the comma.
  What works: `boot -v -B bootpath='"/pci@0,0/..."'` (single quotes around double quotes).
- With an explicit path the Fresco FL1100 xhci attaches:
  `hid1 is /pci@0,0/pci8086,e02@1/pci10b5,8723@0/pci10b5,8723@1/pci1b73,1100@0/device@2/keyboard@1`
  (Magic Keyboard, which Linux had at usb 2-3). storage@6 and storage@2 failed (error 5).
- Next guess: storage@1 (Linux had the stick at usb 2-2; keyboard 2-3 -> illumos @2).
- storage@1 failed too (error 5); that boot showed no hid1/keyboard line under the xhci.
  Stopped guessing ports. Next: find out from the illumos source how root-on-USB finds the
  disk (devid fallback in vdev_disk / spa_import_rootpool, full scan) and make the stick
  independent of the port. The label devid came from the real stick (QEMU usb-host
  passthrough), so a devid match should work once the stick is enumerated before root mount.
- Cause (illumos hubdi.c): before root is mounted the hotplug threads wait for modrootloaded,
  so USB ports are only enumerated through BUS_CONFIG_ONE for the exact port in the bootpath
  (unit address parsed as hex). The ZFS full scan (BUS_CONFIG_ALL) sees no USB disks then.
- The loader stores its EFI boot device path: at `ok` run `efi-show -g illumos -v LoaderDev`;
  USB(n,0) is the 0-based root hub port, so storage@<n+1 in hex>. After one good boot ZFS
  rewrites the label path and later boots need no option.

## 2026-11-11 night: illumos fix for root on USB (two hubd bugs)

Patch: illumos-usb-root/0001-hubd-usb-root-relocation.patch (usr/src/uts/common/io/usb/usba/hubdi.c,
built against c893fd03ad on the build VM in ~/src/illumos-usbroot, module installed in BE `usbroot` on the stick).
1. hubd_bus_config: before modrootloaded, BUS_CONFIG_ALL/DRIVER now enumerates every port, so the ZFS
   pre-root full scan finds a USB root disk on any port/machine.
2. hubd_hotplug_thread: the attach-time pass (held back until root is mounted) no longer treats an
   already-enumerated device as re-plugged (posted HOT_REMOVAL + port reset -> "Device is gone",
   rpool suspended right after root mount; hit every USB root, also with the correct path).
Tested in QEMU with the stick as usb-storage on qemu-xhci at a new slot/port: "trying an alternate
path (/pci@0,0/pci1af4,1100@7/storage@2/disk@0,0:b)", rpool healthy, ssh up. Earlier "tests" booted the
stick as a SATA disk, which never exercised the USB path. usb-host passthrough also hit bug 2.

## 2026-11-11 late: unaccelerated X on the Mac Pro (BE x11-fb)

Works: Xorg 1.19.7 + xorg-video-illumosfb on /dev/fb (e08@3), 1600x900x24 (firmware mode),
twm + xterm, mouse, keyboard, fonts. Installed: x11/server/xorg, xorg-video-illumosfb,
x11/session/xinit, terminal/xterm, desktop/window-manager/twm, x11/xrandr, then
x11/server/xserver-common, system/font/xorg/xorg-core, system/font/xorg/iso8859-1.
Open (proper fixes, OI packaging/driver):
- x11/server/xorg does not pull in x11/server/xserver-common (/usr/bin/X), so startx
  fails with "xinit: unable to connect to X server: Connection refused".
- `fixed` needs system/font/xorg/iso8859-1 (xorg-core misc fonts are iso10646-1 only).
- xf86-video-illumosfb defaults to /dev/fb0; on the stick that is a stale link from the
  QEMU install (fbs/text-0 -> pci@0,0/display@1), devfsadm -C keeps it. Default should be
  /dev/fb (console fb). Workaround in place: /etc/X11/xorg.conf.d/10-illumosfb-console.conf.

## 2026-11-11 night: Mesa 26.2.6 (radeonsi) local test packages

Build VM, worktree ~/src/oi-userland-mesa26 (branch mesa-26, uncommitted):
- Makefile: runpath step over every installed .so (/usr/lib/mesa/amd64:/usr/lib/xorg/amd64:
  GCC_LIBDIR:/usr/clang/21/lib), REQUIRED_PACKAGES += runtime/clang-21 (libLLVM.so.21.1).
- Published to the workspace repo (i386/repo, publisher userland) with PKGLINT=/usr/bin/true:
  the only lint ERROR is userland.manifest1001 (libdrm from the non-OI publisher gfx-drm).
  OI's official libdrm is 2.4.109; Mesa 26 needs >= 2.4.133, so libdrm 2.4.134 has to go
  into OpenIndiana/gfx-drm first for a real submission.
- Archives on the VM: ~/src/mesa-26.2.6-test.p5p (14 MB, sha256 6e6926ce...) and
  ~/src/libdrm-2.4.134-98.p5p (sha256 c550ab91...). DRI: radeonsi, r600, i915, swrast,
  kms_swrast -> libdril_dri.so; gbm/dri_gbm.so.

## Correction (2026-11-11 night): why the compute GPU disappears

My earlier explanation ("vgatext attach fails on the non-console GPU, so it is offlined")
was wrong. Found by the amdgpu session, confirmed in pciehpc.c: root port 00:02.0
(pcieb1, slot pcie3) runs in native PCIe hotplug mode (_OSC grants it), the slot has a power
controller and DLL Link Active reporting, and Apple leaves Slot Control "Power Controller
Control" = off while the GPU is powered and the link is up. pciehpc_get_slot_state() then
reports only PRESENT (hc_has_pwr && PWR_CONTROL set), and the framework removes the child
at every boot ("vgatext offline" is only the consequence). `hotplug enable` let amdgpu
attach; then function 1 (HDMI audio) failed, the slot was powered off and the Mac switched
off (no panic/dump). Fix idea: with Link Active reporting and an active link, treat the slot
as powered. Details: ../illumos-drm-amdgpu/first-boot-test.md ("First attach of amdgpu").

## GNOME 45 on the Mac Pro (2026-11-12, BE gnome-llvm)

BEs: `gnome` = x11-fb updated to osnet 23395 + oi-gnome (GNOME 45.9, gdm 45,
PulseAudio 17, audiohd 23395.1 with the CS4208 fix, driver/cpu/sensor, our
kernel modules on 586678ac1b, halt_on_panic 0, SMF site profile
gnome-macpro.xml for polkit/accountsservice/colord/geoclue/gdm).
`gnome-llvm` = gnome + `--reject driver/graphics/drm` (i915 only, useless
here; gfx_private is in system/kernel/platform) + libdrm 2.4.134 +
drm-devlink 98 + Mesa 26.2.6 (llvmpipe, LLVM 21.1.8) + audio/audio-utilities
(audiotest, audioctl, mixerctl) + userland 35458 + dbus-x11. No amdgpu in
this BE (amdgpu close panic still open), X = illumosfb.

Findings, all for the oi-gnome install meta package:
1. **gdm greeter dies without `dbus-launch`.** gnome-session logs (only with
   gdm `[debug] Enable=true` + `daemon.debug` in syslog.conf) "Failed to
   upload environment to DBus: Failed to execute child process dbus-launch",
   loses org.gnome.SessionManager; gdm loops through displays :0..:5 with
   "Session never registered". The greeter log only shows the misleading
   gnome-session-failed "Cannot open display:". Fix: install
   `system/library/dbus/dbus-x11`. The iMac never hit it because MATE pulls
   dbus-x11 in. gdm (or gnome-session) must depend on it.
2. Mesa 21.3.9 (stock) has only softpipe, which gnome-session's
   hardware-compatibility list rejects as "No hardware 3D"; the GLES helper
   then decides (exit 2 = software rendering is accepted). With Mesa 26 the
   renderer is `llvmpipe (LLVM 21.1.8, 256 bits)`, multi-threaded.
   "GL Helper exited 256" is harmless; it is not the greeter failure.
3. oi-gnome's FMRIs + `driver/cpu/sensor` + audio tools + dbus-x11 + the SMF
   profile = what `gnome3_install` (or desktop/gnome3) has to pull in.
4. pkg in an ssh session: a dropped connection aborts the install with
   nothing done. Run long installs under nohup.
5. Sound: PulseAudio finds "Built-in Audio (Cirrus CS4208)" (headphones,
   speakers) and two HDMI/DP outputs; terminal bell audible.

Temperatures: driver/cpu/sensor gives /dev/sensors/temperature/cpu
(coretemp). New: `applesmc` driver (illumos branch `applesmc` on the VM,
uts/i86pc/io/applesmc.c, sys/applesmc.h): pseudo driver, finds ACPI APP0001,
I/O ports from _CRS (default 0x300), read only; every T* key (sp78/flt) as a
ksensor under /dev/sensors/temperature/smc/, plus /dev/applesmc ioctls
(NKEYS, KEYBYIDX, KEYINFO, READ) for fans and the rest; `smcstat` tool.
Built, not yet tested on the Mac.
6. **Mesa swrast black screen on illumos:** with the gdm greeter running,
   only the cursor showed; greeter log full of "MESA: error: Failed to attach
   to x11 shm". gallium/winsys/sw/dri/dri_sw_winsys.c marks every display
   target's SysV segment IPC_RMID right after shmat; illumos/Solaris refuse
   shmat of a removed segment, so the X server's MIT-SHM attach fails and
   each frame is dropped. Patch
   oi-userland-mesa26 components/x11/mesa/patches/patch-src_gallium_winsys_sw_dri_dri__sw__winsys.c.patch
   (`#if !defined(__sun)` around the early IPC_RMID; destroy removes it).
   Mesa 26.2.6 build 20261112T080632Z. Upstream candidate (Mesa + oi-userland).
   Note: this component's patches are -p0 (no a/ b/ prefixes).
7. Result 2026-11-12 ~09:30: gdm login screen, login as a normal user, GNOME 45
   on llvmpipe "not that slow" (user). gnome-llvm is now the default BE.
   Free on rpool after this: 4.4 GB.

## Plan: Mac sensor app (agreed 2026-11-12, after the SSD install)

- libapplesmc (illumos, next to the driver): C API over /dev/applesmc ioctls
  (count, enumerate, keyinfo, read, decode sp78/fpe2/flt/ui*).
- "Mac Sensors" GTK4/libadwaita app (oi-gnome component): per-model profile
  chosen by SMBIOS model (MacPro6,1, iMac14,x, iMac18,x ...): own SVG drawing
  (no Apple artwork), key -> position map, fan names, colour limits; live
  temperatures on the drawing, fan with real RPM/min/max, history graphs,
  full key list. Fallback without profile: groups by key prefix (TC CPU, TG
  GPU, TM memory, TH drive, TA ambient, Tp PSU, TI Thunderbolt, TPCD PCH, F*
  fans). Profiles are data files, contributable without code.
- Vitals GNOME Shell extension: illumos backend (libapplesmc / smcstat -p),
  upstream candidate.
- Driver: port I/O only; T2 Macs (2018+) need the MMIO SMC path (Linux
  applesmc has it since 2022) - later.
- Findings so far: MacPro6,1 has 770 keys, 76 plausible T* keys; one fan
  "Main" 790-1900 RPM; GPUs ~58 C idle without dpm; mono internal speaker
  (left channel), audiotest -2 /dev/dsp0 OK at 47928 Hz.

## SSD system (2026-11-12 afternoon) - SanDisk Portable SSD 1 TB on USB, blessed

- Fresh full illumos-gate build (onu, on-nightly 999999.1) on the SSD hung in
  early boot before init (after CPU/USB attach, no SMF). Abandoned: user rule,
  OS core stays OI osnet + our tested modules only (memory
  oi-kernel-plus-patched-modules). Fresh BE rpool/ROOT/openindiana kept,
  not default (to delete).
- Working route: zfs send of the stick BE gnome-llvm -> rpool/ROOT/macpro.
  TRAP: the BE has a child dataset .../var; send it too (missing /var =
  filesystem/minimal maintenance, sshd "missing /var/empty"). Remove
  /etc/zfs/zpool.cache in the copy; dump on swap; bootadm -F cpio
  (create_ramdisk UFS image ran out of space).
- Then pkg install --be-name macpro-full: mate_install, build-essential,
  media/vlc (hipster-encumbered), ioquake3, iortcw, pt2-clone, pyopengl,
  fs-uae + launcher 3.2.35 (PR p5p in ~/repo), driver/graphics/drm 98
  (gfx-drm, needed by mate_install next to libdrm 2.4.134). 869 packages.
  Our 7 modules identical to the stick. Default BE macpro-full; blessed
  from macOS (bless --mount /Volumes/EFI --setBoot --file .../BOOTX64.EFI).
  Full form (illumos installboot writes /EFI/Boot/bootx64.efi; FAT ignores
  case): `diskutil mount diskNs1` (the ESP), then
  `sudo bless --mount /Volumes/EFI --setBoot --file /Volumes/EFI/EFI/Boot/bootx64.efi`.
  Needed because the Option menu does not list an internal ESP; same on
  the MacBook Pro 14,3 (notes/macbookpro-2017-drivers.md).
- Open: gdm leaves /dev/sound to the greeter user after login (chown by
  hand meanwhile); GNOME power-off button does nothing (init 5 works);
  intermittent black screen after login (gdm restart helps); stick shutdown
  dump without panic message (~/crash/stick-shutdown on the VM);
  oi-gnome meta package must use full pkg://oi-gnome FMRIs and pull
  dbus-x11; mate_install on a mixed image failed, on a fresh one fine.
