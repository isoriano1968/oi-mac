# Open topics and mitigation plan (2026-11-13, 08:00)

Scope: the Mac Pro 2013 / OpenIndiana work of this session and of the
Radeon session (AMIX/A3000 topics are tracked in their own notes).

Rules for every item (the user's): find the cause first (trace, source),
no guessing; fix it in source as a patch in its component with an upstream
target; deliver only as IPS packages; no hand-copied binaries, links,
site services or config workarounds left in place; test, then install.

Owners: **R** = Radeon session (amdgpu, drm, Mesa, Xorg kernel/graphics
side), **M** = this session. **U** = needs the user's decision.

---

## 0. What is on the Mac by hand today (must become packages)

Found by checking package ownership (`pkg contents -a path=...`) and
comparing boot environments:

| On the Mac now | Where | Package that must own it | Owner |
|---|---|---|---|
| `hald`, `hald-probe-storage`, `hald-probe-volume`, `hal-storage-*` swapped | inside `service/hal`; **amdgpu-x21 has older copies than macpro-full** (cause of today's automount failure) | `service/hal` at OSNET.1 from branch `hal-ntfs` | M |
| `/usr/lib/fs/ntfs-3g/fstyp.so.1` | no package | new component (ntfs-3g fstyp module) or part of `system/file-system/ntfsprogs` | M |
| ~~`/usr/bin/smcstat`, `/kernel/drv/amd64/applesmc` + conf~~ | **done 11-13 09:50**: `pkg://oi-gnome/driver/sensor/applesmc@0.5.11-2026.0.0.2` (ports/applesmc) | owns them now | R |
| ~~`~/.local/bin/mac-sensors`~~ | **done 11-13 09:50**: `desktop/system-monitor/mac-sensors@1.2` (ports/mac-sensors); hand copy removed | owns it now | R |
| Game desktop files and icons, binaries mode 0444 | user home / fixed by hand | the ioquake3 / iortcw / fs-uae components (desktop branches exist) | M |
| `libgallium.so` (u_screen fix), `libdrm_amdgpu.so` (bo_import fix), `.orig` copies | amdgpu-x21 | Mesa 26 component, gfx-drm libdrm | R |
| `hotkey_drv.so` moved aside | amdgpu-x21 | xf86-input-hotkey rebuilt for the 21.1 ABI | R |
| `/dev/fb -> /dev/null` (recreated at every boot), `fbnull` site service | amdgpu-x21 | Xorg patch 39 (built, published) makes it unnecessary | M |
| `/etc/X11/xorg.conf.d/30-console-input.conf` | amdgpu-x21 | removed once item 1 (console/VTs) and item 5 are done | M |
| `/etc/system` (amdgpu_audio line removed), `/etc/power.conf` (autopm) | amdgpu-x21 | defaults again once the driver needs no tunable | R |
| local pkg repo / publisher setup | macpro-full | documented install procedure in the notes | M |

**Mitigation:** item 9 (package-only BE) is built only after every row has
its package; the BE is checked with `pkg verify` and a file-ownership scan
(every file under /usr, /kernel, /etc either packaged or listed as
expected local config).

---

### Tested on the Mac Pro 2026-11-13 14:40-14:55 (amdgpu-x21)

- Xorg patch 39: x11/server/xorg 20261113T014104Z, xserver-common, xephyr
  installed with pkg (p5p ~/repo/xorg21-patch39.p5p); `site/fbnull`
  deleted, `/dev/fb` link removed. X starts both without /dev/fb and with
  /dev/fb -> card0 (as at boot): "(II) /dev/fb is not a console
  framebuffer, running without a console device", glamor GL 4.6 radeonsi.
  The `/dev/fb` and `fbnull` rows of section 0 are done. Reboot check:
  next boot.
- HAL: Unmount as the user refused ("hal-storage-can-unmount-volumes-
  mounted-by-others refused uid 101"): hal-storage-shared.c lets the owner
  of /dev/vt/console_user take over volumes rmvolmgr mounted; on the Mac Pro
  that is a link to /dev/console (no VTs on amdgpu). So the desktop eject
  button waits for 1.1 like logindevperm; nothing to change in HAL. Disk
  stays powered (TEST UNIT READY ok), still mounted.

---

## 1. Graphics and display (amdgpu, D500)

### 1.1 No console / VTs on the amdgpu display  [R]
**Stage 1 done 2026-11-13 21:05 (R), test BE amdgpu-con:** amdgpu attaches the
illumos console itself (illumos-drm fe5b63f: drivers/amdgpu/illumos/
amdgpu_illumos_con.c after i915n's, gfx_private on the loader framebuffer =
Apple's GOP 1600x900x32 at VRAM BAR0 + 1.5 MB; patch 0017 keeps that VRAM
reserved). Boot: no "tem: Compatible fb not found" / consconfig warnings,
/dev/fb -> display@0:gfx1, /dev/vt/* exist, gdm/X on vt7,
/dev/vt/console_user -> 7, logindevperm hands /dev/sound/* to the user;
user: "works out of the box! audio is there". Stage 2 open: restore the
console picture after X (VT switch to text, X exit) and the panic plane
write (amdgpu_illumos_con_kms.c stubs); then share the console code with
i915n. Plan: notes/illumos-drm-amdgpu/console-plan.md.
Symptom: boot log `tem: Compatible fb not found`, `consconfig: terminal
emulator failed to initialize`; X: "VT infrastructure is not available".
Consequences: no boot text, no `/dev/vt/*`; `/dev/vt/console_user` is only
a link to `/dev/console`, so **logindevperm hands nothing to the user**
(sound, kbd, mouse, `/dev/usb/hid*`, `/dev/dri`); input "Read error"
floods (now also on `/dev/mouse`).
Cause (known): amdgpu's fbdev emulation is not registered as illumos
console framebuffer (visual_io for tem via gfx_private).
Plan: implement the console takeover in drm_core/amdgpu (gfx_private /
visual_io), test: boot text on the Dell, `/dev/vt/2..` exist, X on a VT,
devices owned by the user after login. Upstream: illumos-drm (+ gate part
if gfx_private needs one).

### 1.2 Window drag freezes without glxgears; DP audio off  [R]
**Mouse part solved 2026-11-13 17:20 (R):** not vblank/CRTC. Traces
(notes/macpro-2013/crtc-trace-amdgpu-x21-1517.txt and the input traces of
15:20-17:00): the Dell's CRTC stays on after X's mode set, the DCE6 cursor
registers latch normally, every motion reaches dce_v6_0_crtc_cursor_move.
Cause: xf86-input-mouse 1.9.5 (sun_mouse.c includes <errno.h> before the
server headers, built without _REENTRANT) uses the global `errno`. In
Xorg 21's input thread that is the main thread's errno, so after read()
hits EAGAIN vuidReadInput takes "Read error: Error 0" and returns before
PostEvent: motion dropped (works by luck when the main thread's errno is
EAGAIN, e.g. while glxgears runs). Fix: components/x11/xf86-input-mouse
(branch xorg-server-21) CPPFLAGS += -D_REENTRANT, CONFIGURE_OPTIONS +=
CPPFLAGS=..., COMPONENT_REVISION 1 -> xorg-input-mouse@1.9.5-2026.0.0.1
(nm: ___errno), installed on amdgpu-x21 by pkg; after one gdm restart the
user confirms the mouse works. Not yet committed on the branch (VM down
for the host update). Upstream: xf86-input-mouse (sun_mouse.c include
order / _REENTRANT on Solaris with the input thread). Window drag without
glxgears is smooth too (user, 17:40): the drag freeze had the same cause.
Still to check: whether HAL hotplug of the USB HIDs
(the "Read error: Error 0" flood) works again without 30-console-input.conf.
DP audio stays open (1.3).
Cause found so far (traces 01:07, 01:11): after X's mode set DRM sees the
Dell's CRTC as off (only SETCRTC fb 0 from the new X), so no vblank,
no page-flip events, `audio_enable 0`.
Open: why X enables no CRTC (suspect xf86PickCrtcs / possible_crtcs /
possible_clones from our KMS data).
Plan: DTrace pid trace inside Xorg at one X start (needs **one gdm
restart, U**; launcher now runs `prun` after the tracer), fix in drm_core /
amdgpu DCE6, test: drag smooth without glxgears, vblank counter advances,
DP audio audible.

**DP audio WORKS (2026-11-13 20:30, user heard the tone at normal pitch):**
BE amdgpu-audio2 (now the default, amdgpu-x21 = fallback): amdgpu patch 0016
v2 (DP DTO from the VBIOS dispclk 500 MHz) -> /dev/dsp2 47904 Hz; audiohd
23395.3 (HDMI/DP "Digital Other Out" pins driven as outputs, ATI infoframe
defaults). Open: audiohd .4 cleanup (no debug log), channel count verb does
not stick, no presence/ELD (audio goes to all 6 pins), push only on the
user's yes, user access needs 1.1. First hung test boot = ipfilter wait
cycle, not the drivers (see section 6).

### 1.3 DP audio sample rate 0.831  [R]
39 897 Hz measured for 48 000. Plan: trace `dce_v6_0_audio_set_dto`
phase/module and its reference clock in the same test boot, fix, test
audiotest at the right pitch.

### 1.4 Power management, decode, reset  [R]
- dpm stuck at level 0 (SetForcedLevels refused): trace SMC messages.
  Baseline (Mac Pro session, 2026-11-13, amdgpu-x21, Mesa 26.2.6 r1,
  radeonsi ACO): glslbench "Greek Temple" 1920x1080, vsync off, glFinish,
  30 s = 6.34 fps (llvmpipe 0.28). Rerun after dpm reaches the high state:
  `glslbench --size 1920x1080 --seconds 30 --no-vsync --finish
  ~/shadertoy-local/iq-greek-temple/greek-temple.conf` (shader copy local
  only, never publish).
- UVD/VCE disabled ("disabled ip block 7/8"): enable or document.
- linuxkpi "Failed to store PCI saved state": implement `pci_save_state`.
- quiesce(9E) missing (reboot warning; the second reboot powered the Mac
  off): implement in amdgpu, list other drivers without quiesce (M for
  applesmc and the rest), test fast reboot and reboot.
- dma-buf `lseek(SEEK_END)` must return the size (kernel); the libdrm
  change stays as fallback.

### 1.5 Userland graphics  [R, M for Xorg 39]
- libdrm: install `amdgpu.ids` (glxgears: "No such file").
- Mesa: `os_same_file_description` for illumos (glxgears warning).
- Mesa `u_screen.c` patch into the mesa-26 component.
- libdrm `amdgpu_bo_import` + libsync patches into gfx-drm.
- xf86-input-hotkey rebuild; keyboard/mouse components without the
  temporary PKG_CONFIG_PATH lines.
- logindevperm: `/dev/dri/*` rule lists `driver=i915,radeon`, not amdgpu.
- Xorg 21.1.25: patch 39-no-console-fb **built and published** (M);
  pkglint clean (libdrm from gfx-drm), `/usr/X11/include` link TODO.

### 1.6 Repositories and announcement  [R, U]
- `isoriano1968/illumos-drm` public since today (contains the upstream illumos-drm code and
  i915 DMC firmware from its tree). 
- Requested from R: mailing-list draft (first person, signed Iggi), README
  section, userland patches (libdrm, Mesa, Xorg) in the repo.
- Done 08:xx (user's yes in R's session): fork `isoriano1968/gfx-drm`
  branch `libdrm-amdgpu` (66d8bd4 libsync.h, 833a431 bo_import size);
  `isoriano1968/oi-userland` branches `xorg-server-21` (26ae85b0 xorg
  21.1.25 incl. patch 39, 9f78bbfd keyboard/mouse rebuild) and `mesa-26`
  (3679c445 mesa 26.2.6 incl. u_screen). Open before PRs: listed in each
  commit message.

---

## 2. Storage and automount  [M]

### 2.1 Seagate (GPT, NTFS on slice 1) not mounted in amdgpu-x21
Cause (found): amdgpu-x21 was cloned before the GPT HAL fixes of 11-12
18:48; its `hald*` differ from macpro-full's (sha256). The kernel side
worked at 07:39 (`sd10` online, no errors).
Plan:
1. Commit the uncommitted `hal-ntfs` changes (GPT `vtoc_slices`, name
   match for FUSE mounts) as their own commits with tests described.
2. New port `ports/hal-ntfs` (as `ports/audiohd-apple`): pristine illumos
   snapshot of the OS build's commit + `patches/` from `hal-ntfs`, built
   out of tree, republished as `service/hal@<OSNET>.1` from OI's own
   manifest with our binaries.
3. Install with `pkg` into amdgpu-x21 (and macpro-full), restart hal.
4. Test matrix: MBR NTFS (TOSHIBA), GPT NTFS (Seagate), FAT stick, exFAT;
   plug, automount, eject button, replug; no STOP UNIT on plain unmount.
5. Upstream: illumos issues for each commit.

**Done 2026-11-13 09:11 (32-bit, user's choice "32-bit fix now, 64-bit
next"):** commits rebased with the 81116077+ address; new port
`ports/hal-ntfs` (illumos 586678ac1b + patches 0001-0004, 11 programs
rebuilt out of tree, OI's raw service/hal for the rest, incorporate
dependency on osnet 23395, depends on fstyp-ntfs) published as
`pkg://oi-gnome/service/hal@0.5.11,5.11-2026.0.0.23395.1`; on the Mac
(amdgpu-x21) via `~/repo/hal-ntfs-23395.p5p`, facet
`version-lock.service/hal=false`, `pkg install --no-backup-be` together with
`system/file-system/fstyp-ntfs` (replaces the unowned fstyp.so.1);
`pkg verify` clean; hal + rmvolmgr restarted: the Seagate mounts at
/media/5TBSTORAGE. Still to test: desktop icon, eject button, replug;
macpro-full gets the same packages (its hand-copied HAL goes away).

### 2.4 HAL 64-bit (next, user)
illumos changes: libfstyp loads `/usr/lib/fs/<fs>/amd64/fstyp.so.1` in
64-bit processes; build every fstyp plugin 64-bit too (pcfs, ufs, hsfs, udfs;
fstyp-ntfs); libpolkit (usr/src/lib/policykit) 64-bit; HAL 64-bit. All as
illumos patches + packages, then hal-ntfs 64-bit.

**EFI system partition: done 2026-11-13 20:55:** cause: the policy fdi
resets volume.ignore=false after the probers, and its EFI rule covered only
fixed disks with vfat/"EFI". Fix (patch 0005, commit 0567b512):
hald-probe-volume sets volume.partition.type (GPT type GUID, as the HAL
spec says) and 20-storage-methods.fdi ignores C12A7328-... on every disk.
service/hal@...23395.2 installed on the Mac Pro; /media/NONAME gone.

### 2.2 Still open in HAL
- EFI system partition shows up / is mounted: V_SYSTEM ignore does not work
  yet (trace probe-volume on the ESP).
- `volume.is_mounted` for GPT FUSE mounts (hald name match).
- `rmumount`/eject sends STOP UNIT; the disk then stays off until replug:
  eject only on the eject button, plain unmount otherwise; sd should send
  START UNIT on open of a stopped removable disk (check sd.c).
- `/usr/lib/fs/ntfs-3g/fstyp.so.1` into a package (row in section 0).

### 2.3 USB 3 disks  [M]
`scsa2usb must be updated to use usb_pipe_xopen()` on the Seagate (once
followed by "Device is gone" at 00:51; fine at 07:39). Plan: read
scsa2usb's pipe opens, convert to `usb_pipe_xopen()` (bulk endpoints with
SuperSpeed companion), test Seagate and TOSHIBA on the Fresco ports, many
plug cycles; illumos issue + patch.

---

## 3. Sound  [M, R]

- 3.1 audiohd upstream (Toomas' review): per-model GPIO table as Linux
  cs420x (gpio_13 0x0a default; gpio_23 0x0c for iMac 12,1/12,2/14,1;
  CS4208 MacPro6,1 0x01 + inverted HP jack), test iMac27 13,2, iMac21
  14,1, Mac Pro, AMD iMac 21 Retina (codec dump first). **U:** author name
  and address for the commits, illumos.org account, OK for the reply.
- 3.2 Output selector: package GNOME Settings (gnome-control-center,
  oi-gnome) and pavucontrol (oi-userland); pulseaudio module-detect must
  create one named sink per device (CS4208 "Built-in", DP "Dell") and
  follow hotplug.
- 3.3 Device handover for sound: item 1.1 (logindevperm via console owner).
- 3.4 DP audio: items 1.2 / 1.3.
- 3.5 pulseaudio of the gdm greeter probes `/dev/dsp0` (Permission denied):
  greeter's pulse config must not autoload module-detect.

### 3.6 mikmod player (user's decision 2026-11-13: only once user sound works)
- libmikmod 3.3.16 is packaged by OI (library/audio/libmikmod); the mikmod
  player is not: a new oi-userland component (pristine mikmod 3.2.x +
  patches, as in amix-userland), analysis first. Install both only after 1.1
  gives the user access to /dev/sound/* (no root-only workaround).

---

## 4. Sensors (applesmc, Mac Sensors)  [R since 09:20, user's request in R's session: 4.1, 4.2, 4.4]

- 4.1 **Done 2026-11-13 (R session):** `ports/applesmc`: pristine illumos
  586678ac1b + patches/0001 (the gate commit, now d2378d49 on branch
  `applesmc` in ~/src/illumos-usbroot: built smcstat binary removed from git,
  man pages applesmc(4D)/smcstat(8) added, smcstat fan-key format fix,
  `depend system/ksensor`, author 81116077+). Out-of-tree build; /dev/applesmc
  through its own devfsadm link module `applesmc_link.so` (pkg cannot deliver
  into /dev; same mechanism as gfx-drm's SUNW_drm_link_i386.so).
  `pkg://oi-gnome/driver/sensor/applesmc@0.5.11-2026.0.0.2`, installed on the
  Mac Pro (amdgpu-x21, replaces the hand copies; pkg verify clean) and the
  iMac21 (accel-con, with system/ksensor + driver/cpu/sensor). Upstream: illumos
  issue still to file.
- 4.2 **Done:** `ports/mac-sensors`: `desktop/system-monitor/mac-sensors@1.2`
  (app with `#!/usr/bin/python3.9`, desktop file, own hicolor SVG icon
  org.openindiana.MacSensors, deps python-39/pygobject-39/pycairo-39/gtk4,
  optional applesmc + driver/cpu/sensor); installed on both Macs.
- 4.3 ksensor nodes are root 0600: **U** whether a `sys/sensors` group or
  logindevperm rule should open them to the console user.
- 4.4 **iMac14,1 (iMac21) done:** cards chosen from the SMC keys at start:
  GPU in the CPU (TCGc, detail TCSc) when there are no TG keys; Mainboard
  (max Tm\dP, memory TM0P) when there is no drive key; PSU also Tp2H/Tp2F;
  header names per model. Tested on a private Xvfb on the iMac21 (CPU 59,
  GPU 57, display 34, board 53, PSU 43 °C, fan 1400 rpm). Still to check:
  iMac13,2 (iMac27) keys; window is wider than 1400 px (min size).

---

## 5. Input  [M]
- Keyboard/mouse in amdgpu-x21 now via conskbd/consms (config file in 0).
- HAL did not add the re-enumerated Lenovo keyboard (hid2): hald hotplug of
  HID devices; trace hald on a replug of the receiver (ask before touching
  input devices), fix in hald (illumos-gate), part of the `service/hal`
  package of 2.1.
- Mouse "Read error: Error 0" flood: once item 1.1 is in, check whether it
  is gone (it is the conskbd/consms vs. X conflict); else trace the mouse
  driver's read path (xf86-input-mouse sun code).

---

## 6. Boot messages and system  [M unless noted]

| Message | Plan |
|---|---|
| `drmach_init: number of logical CPUs (12) ... not power of 2` | fix the check in drmach_acpi (12-core Xeon is valid), illumos issue |
| bge1 DHCP NAK, nwamd/dhcpagent/in.routed errors | NAK also in normal boots; in the amdgpu-audio test boot bge1 stayed 0.0.0.0, NWAM stayed in NoNet, which enables ipfilter (NoNet ipf.conf: block all but DHCP) -> ipfilter refresh hung 2 min (see next row). Next: record dhcpagent (-d) + snoop during one boot (user's OK for the reboot) |
| ipfilter refresh hangs in boot (found 2026-11-13) | ipf_include.sh service_check_state() waits without limit for services in transition; some wait for milestone/network, which depends on ipfilter. Fix written (illumos worktree ~/src/illumos-ipf, branch ipf-boot-wait, b1ae7f18: wait max 10 s), upstream only (user: no SUNWcs repackaging). ipfilter is already disabled persistently in all BEs; NWAM's NoNet enables it temporarily by design. Related Sun bugs 6939061, 6959722 |
| nwamd `sysevent_subscribe_event: Invalid argument` | compare with a stock OI boot, then report/fix |
| nwamd `nwamd_door_switch: need solaris.network.autoconf.read` (repeats every ~5 s in the session) | find the client (GNOME network applet?) and grant the authorization by profile, packaged |
| gnome-session ConsoleKit `GetSessionClass failed`, `Not owner` | oi-gnome ConsoleKit/gnome-session patch |
| `orca-autostart.desktop ... TryExec` | dependency or packaging fix in oi-gnome |
| nvidia_modeset loads on an AMD Mac | **U:** remove `driver/graphics/nvidia-470` |
| power button "Power off" in GNOME does nothing | trace gnome-session -> logind/ConsoleKit -> shutdown path, fix in oi-gnome |
| gnome-shell black screen after re-login (macpro-full, Xorg 1.19) | SIGSEGV on an X error in XShmGetImage (cogl): reproduce, fix in mutter/cogl or Xorg |
| pcie slot 3 power fault, pci_autoconfig DISABLE | informational, no action |

---

## 7. Blender and its libraries  [M]

Done (worktree `~/src/oi-userland-blender`, branch `blender`):
robin-map 1.4.1, oneTBB 2022.3.0 (1 patch), OpenImageIO 3.1.13.1
(1 patch) published; Blender 5.2.4 component with patches 01 FindFftw3,
02 _Generic C-only, 03 Eigen 3.4, 04 alloca/termios.

Open, in order:
1. **int8_t is unsigned under -funsigned-char** (illumos
   `<sys/int_types.h>`; Blender uses -funsigned-char on every platform).
   Fix written and tested (branch `int8-signed`, ABI-neutral: signed char
   only under `__CHAR_UNSIGNED__`; CHAR_MIN/MAX too). Next: port
   `ports/system-header-int8` that republishes `system/header@<OSNET>.1`
   from OI's raw package with the three patched headers, for 23344 (VM)
   and 23395 (Mac); illumos issue.
2. Finish the Blender build (`gmake -k` to list all remaining errors),
   each fix a patch with upstream target.
3. Package (manifest, desktop file and icon from
   `release/freedesktop`), install with pkg, test on the Mac (GL 4.6
   radeonsi), check idle CPU (oneTBB spin finding).
4. Libraries still missing, each its own component: numpy for Python 3.14
   (**U** / OI maintainers: numpy 2.0.2 is 3.9-only, Inkscape depends on
   it), Ceres Solver (motion tracking), Draco + meshoptimizer (glTF),
   libwebp with CMake config, then Embree, OpenSubdiv, OpenVDB, Alembic,
   USD, OIDN, Manifold.
5. oneTBB `test_task`: idle workers busy ~74 ms of 100 ms: trace the
   worker sleep path on illumos.
6. Upstream submissions: oneTBB, OpenImageIO, Blender (4 patches).

---

## 8. Cleanup and housekeeping

- SSD BE `openindiana` (failed onu): delete (**U** confirms).
- Test BEs `amdgpu-ssd*`, `amdgpu-dpm*`, `amdgpu-disp`, `amdgpu-x`,
  `macpro-full-backup-*`: keep or remove (**U**, with R).
- `gnome3_install` meta package for upstreaming GNOME 3.
- Spawned chip: game binaries mode 0444 (in the components).

---

## 9. The package-only boot environment (end state)

1. Every row of section 0 has its package (published to our repo).
2. New BE created from packages only (`pkg install` into a fresh BE from
   OI + our publishers), no `.orig`, no hand links, no extra xorg.conf.d.
3. Checks: `pkg verify`; file-ownership scan; boot messages free of the
   section 6 warnings that have fixes; console text on the Dell; GNOME
   accelerated, drag smooth; sound on speakers and Dell, switchable in
   Settings; NTFS/GPT/MBR disks automount and eject; Mac Sensors and
   Blender start from the app grid with their icons.
4. Then `beadm activate` it as default (user's OK at that point).

## Order

1. R: 1.2 (CRTC/vblank, one gdm restart) -> 1.3 -> 1.1 (console) -> 1.4.
2. M now: 2.1 HAL package (today's automount), then 7.1 header package
   and 7.2 Blender build, 4.1/4.2 sensor packages, 2.3 scsa2usb, 3.2 mixer.
3. M after 1.1: section 5 and 3.3 re-check, remove 30-console-input.conf.
4. Section 9 when 1.1, 1.2, 2.1, 3.2, 4.x and 7 are packaged.

## Decisions needed from the user (U)

1. One gdm restart for R's Xorg trace (1.2).
2. Announcement of the public illumos-drm fork (1.6).
3. Push gfx-drm fork + Mesa/Xorg branches (1.6).
4. audiohd commit author/illumos account/OK for Toomas reply (3.1).
5. ksensor permissions for the console user (4.3).
6. Remove nvidia-470 (6).
7. numpy for Python 3.14 approach with OI maintainers (7.4).
8. Delete BE `openindiana`; which test BEs to keep (8).

## 10. Tomorrow (2026-11-14): Verbatim stick as the AMD iMac test stick  [M]

User's decision 2026-11-13 evening: prepare the Verbatim USB stick (BE
gnome-llvm, used on the Mac Pro on 11-11), NOT the Mac Pro's 1 TB SanDisk
SSD (that is the Mac Pro's rpool). Plan, analysis first:
- update the stick's OS to osnet 23395 with pkg (or a fresh BE from packages
  only, as in section 9), then our packages: service/hal .2 + fstyp-ntfs,
  audiohd (check the iMac's codec dump first: Cirrus/other, GPIO table),
  Mesa 26 r1, Xorg 21.1.25 (patch 39) + input drivers incl. mouse r1,
  glslbench, Mac Sensors/applesmc;
- iMac 21.5" Retina (Radeon Pro 555/560, Polaris): our amdgpu covers SI only,
  so OI runs on the firmware framebuffer (console + VTs, X illumosfb/vesa,
  no 3D) -> user sound/devperm work there as on the Haswell iMac;
- boot: bless from macOS or Option key; the stick must not be the Mac Pro
  SSD.

## 11. Evening/night 2026-11-13: results and decisions for the morning

**Mac Pro BEs now** (16 old ones destroyed with the user's OK): openindiana,
macpro-full, amdgpu-audio3, **amdgpu-con (default)**, amdgpu-con3,
**amdgpu-con4 (running, one-time boot; works: Dell DP audio, VTs)**.
A reboot without action goes back to amdgpu-con (old audiohd, no VT setup).

Done and tested by the user:
- mikmod 3.2.10 r1 (`media/mikmod`, own component packages/multimedia/mikmod,
  launcher + own MIT icon), libmikmod 3.3.16 (OI). Mods in ~/Music.
- PulseAudio r5, patch 32: a PA card per OSS device (GNOME/gvc lists outputs
  only through cards), HDMI outputs numbered (dsp1 = "Audio 1", dsp2 = Dell
  = "Audio 2"). Output picker in the top bar and Settings works.
- DP audio silence root cause: the GPU HDMI controllers report 0/0 streams in
  GCAP; audiohd put the record engine on the playback stream descriptor, so
  any O_RDWR open (PulseAudio's module-oss) silenced DP. audiohd .5 (amdgpu
  session, patch 0004): 1 output / 0 input streams, no record engine, no
  record-source control. Dell plays through PulseAudio. (Found by direct
  tones: O_WRONLY audible, O_RDWR silent; the Dell unmutes ~1-2 s late.)
- Dock gone: `org.gnome.shell disable-user-extensions` was true (GNOME sets
  it after shell crashes); set back to false, Dash to Dock ENABLED.
- VT switching: vtdaemon + console-login:vt2/vt3 enabled by hand in con3
  (carried into con4); Ctrl-Alt-F2 login and Ctrl-Alt-F7 back work.

Built overnight, not installed (need the user's OK):
- **PulseAudio r6, patch 33**: module-oss asks the mixer (SNDCTL_AUDIOINFO,
  dev -1; verified on the Mac: audiohd#1/#2 caps in=0 out=1) and opens
  playback-only devices O_WRONLY, so no "audiohd#N: open failed (rv 19)"
  warnings at every PA start. VM ~/oi-repo, oi-gnome 17.0-2026.0.0.6.
- **system/vt-hotkeys 1.0** (packages/system/vt-hotkeys): SMF site profile
  /etc/svc/profile/site/vt-hotkeys.xml (vtdaemon enabled, hotkeys=true on
  the instance, secure stays; console-login vt2..vt6), restart_fmri
  manifest-import. Tested by `svccfg apply` on a copy of the repository.
  Replaces the hand-made svcadm/svccfg change.
- **vtxlock fix** (illumos branch vtxlock-consolekit, VM ~/src/illumos-vtxlock,
  commit 5028ce31, local only): the "root" unlock prompt is xlock, which
  vtxlock starts as root because it only knows GNOME 2 (GNOME_SM_DESKTOP)
  and xscreensaver. Fix: lock the ConsoleKit session of display :N
  (ck-list-sessions -> Session.Lock via dbus-send); GNOME Shell locks on
  CK's Lock signal (screenShield.js) and unlocks with the user's password.
  Verified read-only: CK 0.4.6 has Session2 x11-display ':0' with Lock.
  **Decision for the user:** vtxlock is in SUNWcs. Ship it how? (a) illumos
  upstream only, (b) republish SUNWcs with the one file (ports mechanism,
  as hal-ntfs) - the user declined SUNWcs repackaging for ipf before.
- amdgpu restore -19 fix (amdgpu session, illumos-drm 0e03e99): the console
  restore does nothing while DRM has no CRTC enabled (firmware scanout).
- **Monitor names for HDMI/DP outputs (amdgpu session, night 11-13/14),
  section 11.1.** amdgpu patch 0019 (illumos-drm e3a21c2) and audiohd .7
  (patch 0005, illumos branch audiohd-hdmi 2af85c24; .6 was the first
  version, without the "HDMI: "/"DisplayPort: " prefix). Both built, not
  installed, not tested on the Mac.
- **Current p5ps for con5:** VM:~/src/drm/illumos-drm-0.0.2-amdgpu.p5p
  sha256 a60017c9d2eed4d4afcd1790e198bd0615b28237da85d0e77ae12d47f1e275d5
  (= 0e03e99 + 0019; replaces the 90ffcfd7 p5p), and
  VM:~/src/ports/audiohd-apple/build/audiohd-23395.7.p5p sha256
  11accfa319ae8820de48787cc4fe17d6fddedc88b66edb6acbca89b88a81e890
  (driver/audio/audiohd@0.5.11-2026.0.0.23395.7 = .5 + 0005; module sha256
  fe38650a...).

### 11.1 Monitor names and presence of the HDMI/DP outputs  [R built, M for PA]

How Linux does it: the ATI/AMD HDMI codec (1002:aa01) has no ELD buffer;
snd-hda's atihdmi.c (get_eld_ati()) emulates the ELD from AMD verbs: speaker
allocation 0xf70, audio descriptors 0x776/0xf76, A/V delay 0xf7b and, on
codec revision 3 and later, the sink info 0x780 (index) / 0xf81 (data):
manufacturer id (0), product id (1), description length (2), port id
(3, 4), description bytes (5..22). The GPU driver fills them from the EDID:
endpoint registers AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0..8. DC does it on
DCE8 and later (dce_aud_az_configure()). The non-DC DCE6 code
(dce_v6_0_afmt_setmode()) writes SADs, speaker allocation and lip sync but
**no sink info**, so even Linux shows no monitor name on SI without DC. The
Mac Pro's codec is rev 0x100300, so it has the sink info verbs.

**(1) amdgpu, patch 0019** (drivers/amdgpu/patches/0019-illumos-amdgpu-dce6-
audio-sink-info.patch, upstream candidate): new
dce_v6_0_audio_write_sink_info(), called in afmt_setmode after the lip
sync fields. It copies from connector->eld (DRM builds it from the EDID at
probe, under eld_mutex) the manufacturer (ELD 16-17), the product (18-19)
and the monitor name (ELD 20.., MNL, at most 18 bytes) into SINK_INFO0
(MANUFACTURER_ID | PRODUCT_ID), SINK_INFO1 (SINK_DESCRIPTION_LEN),
SINK_INFO2/3 = 0 (port id) and SINK_INFO4..8 (4 name bytes per register,
LSB first). It is written at every modeset with audio (X start, hotplug
reprobe, mode change), before the endpoint's AUDIO_ENABLED, so the codec
sees a complete ELD when it reports ELD valid.

**(2) audiohd, patch 0005** (.7): for every HDMI/DP pin (pin cap HDMI or DP)
- pin sense: EXEC_PIN_SENSE first when the pin needs a trigger (pin cap
  bit 1), then GET_PIN_SENSE: bit 31 presence, bit 30 ELD valid;
- with ELD valid, the connection type (ATI: speaker allocation 0xf70 bit
  0x200 = DisplayPort; others: ELD byte 5 bits 3:2) and the monitor name:
  ATI/AMD rev 3+ from the sink info verbs
  (length at index 2, bytes from index 5), other codecs (Intel, NVIDIA) from
  GET_HDMI_DIP_SIZE(ELD buffer) + GET_HDMI_ELDD (MNL in byte 4, name from
  byte 20). Only printable ASCII is kept (no '%'), trailing blanks cut.
- refreshed at attach, at resume, on a pin's unsolicited response (polled
  while a stream plays, as for headphones) and **on every read of
  hdmi-connected**;
- an unsolicited response from an HDMI/DP pin no longer switches the
  speakers off (before: plugging a monitor into an Intel controller with an
  analog + HDMI codec muted the speakers).

**Interface for PulseAudio (module-oss), per OSS device /dev/sound/audiohd:N:**
- **Presence:** mixer control **`hdmi-connected`**, BOOLEAN, read-only,
  flags READABLE|PLAY|POLL, value 1 while at least one HDMI/DP pin of the
  device senses a monitor (presence bit), else 0. It exists only on devices
  with an HDMI/DP output path (not on audiohd#0, the CS4208). OSS: open the
  device's mixer (/dev/sound/audiohd:Nctl, or /dev/mixer with the device
  index), SNDCTL_MIX_NREXT, then SNDCTL_MIX_EXTINFO over the controls until
  extname/id = "hdmi-connected" (type MIXT_ONOFF), then SNDCTL_MIX_READ
  with that ctrl and timestamp: value 0/1. `audioctl show-control -d
  /dev/sound/audiohd:Nctl` shows it too. The read itself re-reads the pins
  (and the names), so polling it every few seconds is the hotplug check.
  Map to the PA port's availability: 1 = yes, 0 = no.
- **Monitor name:** the device description. SNDCTL_CARDINFO (card = the
  device's card number, or -1 on an open fd) gives longname =
  "<description> (<version>)". On a device with HDMI/DP outputs only (the
  GPU controllers) the description **is** the connected monitors, each as
  **"HDMI: <name>" or "DisplayPort: <name>"**, joined with ", " (normally
  one, e.g. "DisplayPort: DELL U2713HM (a)" as longname); with no monitor
  (or no name in the ELD) it is exactly the description of attach again
  (on the Mac "onboard1", longname "onboard1 (a)"). On a mixed controller
  (analog + HDMI) it is "<controller>, HDMI: <name>". /dev/sndstat shows
  the same. Read hdmi-connected first (that refreshes it), then
  SNDCTL_CARDINFO; the names are the ", "-separated items after "HDMI: " or
  "DisplayPort: " before the last " (". No prefix: no name, use PA's own
  "HDMI / DisplayPort Audio N".
- Why not hw_info lines (the M session's suggestion): the framework's
  hw_info list is append-only (audio_dev_add_info(), no remove or replace),
  so lines that follow hotplug would need a new misc/audio interface; the
  description is the one per-device string a driver may change at run time.
- No new ioctl, no framework change: only audio_dev_set_description() and a
  normal control in audiohd (driver/audio/audiohd only).

Test on the Mac (con5 or later, with both packages): `audioctl
show-control -d /dev/sound/audiohd:2ctl` shows hdmi-connected 1 (Dell on
DP) and `audioctl show-device -d /dev/sound/audiohd:2` (or cat
/dev/sndstat) shows "DisplayPort: DELL ..." as the description; audiohd:1 (no monitor)
shows 0 and the controller name. Not verified yet: that the codec raises
ELD valid with our sink info, and that it sends an unsolicited response at
hotplug (without it, the name follows only on reads of hdmi-connected,
which is enough for PA's polling).

Monitor names (night): amdgpu 0019 (sink info from the EDID into the
azalia codec) + audiohd .7 (pin sense, "hdmi-connected" control, the
description becomes "DisplayPort: <monitor>") from the amdgpu session, and
**PulseAudio r7 = patch 34** (mine): a digital device takes the monitor's
name as its description, port availability follows hdmi-connected
(polled every second, hot-plug updates name and availability; with an
older audiohd nothing changes). Parser unit-tested on the Linux workstation; r7 built
without warnings, no unresolved symbols (a first r7 with a missing
pa_strlcat was removed from ~/oi-repo before anyone installed it).

Plan for the morning (each step with the user's OK): new BE amdgpu-con5 =
clone of con4 + illumos-drm p5p a60017c9... (0e03e99 restore fix + 0019)
+ audiohd .7 (11accfa3...) + PulseAudio r7 + vt-hotkeys (+ vtxlock if
decided), one-time reboot, check: no "restore failed", no "open failed
(rv 19)", Dell audio, output named after the Dell, Audio 1 hidden, Ctrl-Alt-F2/F7, unlock asks for the user; then make
it the default and destroy con3.

Still open: GsmConsoleKit "GetSessionClass failed: Not owner" (gnome-session
vs ConsoleKit 0.4.6), orca-autostart.desktop parse warning in the greeter,
monitor names via ELD for the HDMI outputs (kernel side built, 11.1; the PA
part is open), DHCP
NAK on bge1, drmach CPU-count warning, "Failed to store PCI saved state".
