# iMac14,1 ("iMac21", 21.5", Late 2013) on OpenIndiana

Machine (address and access: notes/hosts.local.md, not in git): Core i5-4570R
(Haswell, 4 cores, 2.7/3.2 GHz), 8 GB, Intel Iris Pro 5200 (8086:0d22, GT3e),
1920x1080 panel (not Retina). OI 2026.10 text install, `pkg update` to osnet
23381 (illumos-08e59b676b), 2026-11-10.

## Hardware and drivers (2026-11-10)

| Part | ID | State |
|---|---|---|
| Ethernet | BCM57766 14e4:1686 | stock `bge` attaches; NWAM never brought it up (no link-up event before plumbing), so physical:default + `ipadm create-addr -T dhcp bge0/v4`, resolv.conf/nsswitch.dns by hand |
| Graphics | 8086:0d22 Iris Pro 5200 | stock `i915` (gfx-drm, "v1.6.20130900") attaches but `VBT signature missing` -> `failed to init modeset`; X: intel driver "No devices detected", illumosfb gets no framebuffer -> crippled console. **Unbound** (`update_drv -d -i pci8086,d22 i915`, backup /etc/driver_aliases.pre-i915-d22): X runs on illumosfb 1920x1080, softpipe |
| HDA (speakers) | 8086:8c20 @1b, Cirrus CS4206 | stock audiohd silent; our `pkg://oi-gnome/driver/audio/audiohd@...23381.1` (GPIO amp, by codec type) works (user heard audiotest, VLC) |
| HDMI audio | 8086:0d0c @3 | no power without i915: ~1000 "timeout when get response from codec" at boot (audiohd binds by pciclass, no alias to remove). To do: give up at the first timeout in our audiohd |

audiohd instances swapped in /etc/path_to_inst (backup
/etc/path_to_inst.pre-audiohd-swap): @1b is audiohd 0, so /dev/audio, dsp0
and PulseAudio's default are the speakers.

## Software

- `mate_install` + `driver/graphics/drm` (lightdm was installed disabled;
  upower/consolekit maintenance until `svcadm refresh dbus` + clear, as on
  the iMac27).
- Publishers: openindiana.org, hipster-encumbered, oi-gnome / oi-games /
  amix from p5p files in ~/xfer (made with pkgrecv from the OI VM's repos).
- Installed: VLC (encumbered), our pulseaudio 2026.0.0.4 (+ /32), pt2-clone,
  ioquake3, iortcw, innoextract, fs-uae + launcher 3.2.35 (amix; encumbered
  has an older fs-uae of the same name: install pkg://amix/...), gcc-27,
  gcc-14, GNOME 45 (44 oi-gnome packages, 18 version locks released).
- Game data: ~/.local/share/Quake3 (retail baseq3/pak0-8, missionpack/pak0-3
  from the Linux workstation's "Quake 3 Arena.zip", CRC-checked), ~/.local/share/iortcw/main
  (GOG installer via innoextract -I app/Main).
- FS-UAE menu: per-user override ~/.local/share/applications (Categories
  Game;Emulator); the package change was handed to the FS-UAE session.

BEs: audiohd-apple (MATE + audio fix + tools), gnome45 (active, gdm 45
instead of lightdm), automatic -backup-N clones.

## Baseline: software rendering (2026-11-10, gnome45, gdm greeter)

`glxinfo -B`: softpipe, Mesa 21.3.9, OpenGL 3.3 compat, Accelerated: no.
`glxgears` (default window): 267 FPS. GNOME 45 "feels really slow" (user);
MATE usable.

## Plan: hardware acceleration

the illumos-drm maintainer's new stack, illumos-drm (main:
HEAD ab02009, version 0.0.2): Rust DRM core misc/drm_core, linuxkpi,
drm_helpers, drm_kms (Linux v7.2 KMS), `i915n` = Linux v7.2 i915. VBT from
the ACPI OpRegion, else Linux's VBT-missing defaults (solves the Mac case).
Display only on Ivy Bridge (platform gate, patch 0011); every other platform
runs headless. Haswell display needs DDI, HSW power wells, IPS etc. in
display-tus.txt. Build: Rust 1.97.1 + pinned nightly (-Zbuild-std),
illumos-ddi (illumos-drm upstream), Linux v7.2 tree. Opt-in via `drmadm select`.

Steps: build on the OI VM; install in a test BE; boot it with `beadm
activate -t` (one boot only, a panic falls back); headless i915n on Haswell
first (untested there per the README); then the Haswell display gate.
Mesa side: oi-userland #27679 (i965) / #27815 (Mesa 25.3.6).

## Build of illumos-drm 0.0.2 on the OI VM (2026-11-10, steps A+B done)

OI VM (osnet 23344), ~/src/drm: illumos-drm ab02009,
illumos-gate 08e59b67 (ddi.mapfile), linux = v7.2 8d3ae592 + 16 patches
(drivers/i915/fetch-and-patch.sh, 1.9 GB). Needs `system/header/header-drm`
(drm/drm.h) and Rust (LD_LIBRARY_PATH=/usr/gcc/14/lib/amd64 for rustup/cargo).
Scripts: ~/src/drm/build-all.sh (linuxkpi, drm_helpers, drm_kms, i915n; drm_core
via `cargo xtask build drm-module` + ctf/build-ctf.sh + ld), ~/src/drm/package.sh.
All links "0 unresolved"; i915n 292 TUs, licence checks 0 refused.
Result: ~/src/drm/illumos-drm-0.0.2.p5p (4.8 MB, publisher illumos-drm:
drm-core, linuxkpi, drm-helpers, drm-kms, i915n, drmadm; virtio-gpu not built).
i915n also NEEDS misc/gfx_private (from driver/graphics/drm).

## Step C: headless i915n on the iMac21 (2026-11-10)

Test BE `drmtest` (clone of gnome45 + the p5p + `drmadm -R select new`),
booted only with `beadm activate -t drmtest` + `reboot -p` (a fast reboot
skips the loader and the one-time setting). All imports resolve on 23381.

- i915n attaches to 8086:0d22: 32 MiB stolen (gen7), "Found 128MB of eDRAM",
  "display on haswell is not supported; running headless", /dev/dri/card0 +
  renderD128 (0666). Warnings: drm_WARN_ON intel_pch.c:91 (platform flags
  headless), "dma_resv_init of a reservation that has one already".
- X: 20-drmadm.conf (modesetting) -> "no screens found", no GUI in that BE;
  the screen shows stripes (firmware framebuffer in stolen memory reused).
- Bug 1 (panic): 2nd EXECBUFFER2 with a request in flight -> eb_throttle ->
  `eb->file->filp->f_flags`, filp NULL on illumos -> BAD TRAP addr 0x2c
  eb_pin_timeline+0x105. Dump in /var/crash/drmtest on the iMac.
  Fix: drivers/i915/patches/0014-illumos-execbuffer-throttle-without-filp.patch
  (on the OI VM, uncommitted in ~/src/drm/illumos-drm).
- Legacy GEM_MMAP -> ENODEV (expected: mappings go through the core / MMAP_OFFSET).
- With 0014: ~/xfer/i915n_smoke (src ~/src/drm/t on the VM): VERSION,
  chipset 0x0d22, GEM create, pwrite batch, MI_STORE_DWORD_IMM on the render
  ring, wait, pread = 0xC0FFEE42 OK; 1000 batches 32 ms (31.8 us each).

## Step D: Haswell display on i915n (2026-11-10, BE drmhsw)

Patch 0015 (gate ivybridge || haswell) + Haswell IDs in i915_illumos_con.c
(i915n_con_ids, 60 = Linux INTEL_HSW_IDS). Boot: "Found haswell (device ID
0d22) integrated display version 7.00", "VBT from the ACPI OpRegion
(mailbox 4)" (the iMac has a VBT after all). Panel = DP-3 (DDI D, DPCD 1.1,
2 lanes, 1.62/2.7 Gbit/s, EDID "APP a012 iMac" 1920x1080@60 138.5 MHz, 10 bpc).

Open issues found:
1. Console: Apple's EFI GOP framebuffer is X-tiled (modifier
   0x100000000000001); i915n_con_kms_takeover() keeps linear only -> "no
   console on this device", tem fails, screen shows the boot menu distorted.
2. Because of 1, /dev/fb -> display@2:card0; Xorg's xf86OpenConsole opens
   /dev/fb first, becomes DRM master, modesetting's own open gets
   "drmSetMaster failed: Device busy". Test workaround: /dev/fb -> /dev/null
   (pseudo/mm@0:null), by hand, in drmhsw only.
3. First X start: DP-3 "connected" but 0 modes (probe before the DP sink
   answered?); a later GETCONNECTOR (~/xfer/drmprobe) returns both modes.
With the workaround and gdm restarted: Xorg modesetting on i915n 1920x1080,
gdm greeter up (ShadowFB, swrast: no glamor in the X server, no Mesa
Intel driver yet). Evidence: ~/Sources/OpenIndiana/notes/illumos-drm-haswell/.
drm_core debug: echo "drm_debug/W 0x6" | mdb -kw (0 to stop).

## Accelerated OpenGL on the iMac21 (2026-11-10, BE accel)

BE `accel` (clone of gnome45, one-time boots only):
- illumos-drm `imac21-test` = main + origin/i915n/all-display-paths +
  origin/drm/hotplug-sysevent + our drivers/i915/patches/0016 (filp
  throttle fix); p5p ~/src/drm/illumos-drm-0.0.2-imac21.p5p (OI VM),
  `drmadm select new` (220 aliases).
- OpenIndiana/gfx-drm master e95220c (libdrm 2.4.134 with the new illumos
  device layer, drm-devlink, drm with the drm_core guard), built with
  `make install` + `make install PKGVERS_BRANCH=2026.0.0.96` in usr/src/pkg
  (bldenv); without 2.4.134 drmGetDevice2 = ENODEV -> "failed to find EGLDevice".
- oi-userland worktree ~/src/oi-userland-mesa (branch mesa-intel-i915n):
  mesa rev 4 (crocus added, i965_dri.so packaged + runpath, patch
  patch-src_mesa_drivers_dri_i965_brw__program__binary.c.patch: no GNU build
  ID on illumos -> SIGSEGV 0x4 in brw_program_binary_init), xorg-server rev 19
  (--enable-glamor, libglamoregl.so, REQUIRED libepoxy+mesa).
  New oi-userland rule transforms/symlinks (no dangling links): links to dirs
  and through ogl-select need pkg.tmp.allow-dangling=true.
  Version locks released in accel for mesa, xorg*, libdrm, drm, agpgart, header-drm.
- /dev/fb -> /dev/null by hand after each boot (devfsadm recreates -> card0;
  console can't keep the X-tiled Apple framebuffer).

Results: glamor initialized, DRI2, gdm/gnome-shell up. glxinfo: Accelerated
yes, i965 = GL 4.5 core / 3.0 compat / ES 3.1; crocus = GL 4.6 core / 3.1
compat / ES 3.2. glxgears ~1400 FPS (softpipe 267). Offscreen GBM 1080p test:
crocus 3252 fps, i965 3172, softpipe 39. AIGLX (indirect) falls back to swrast:
DRI2 driver name "i915" instead of i965.
Open: console X-tiled fence, first-probe no modes, sound (gdm gives
/dev/sound to gdm/root: vtdaemon off, console_user=/dev/console), wake-up
pixel artifacts (FBC suspect), AIGLX name.

### BE accel: sound, disk, desktop drawing (2026-11-10 evening)

- **Sound under GDM:** gdm calls di_devperm_login("/dev/console") (vtdaemon
  off); /etc/logindevperm keys everything on /dev/vt/console_user, which
  only exists with vtdaemon, so /dev/sound stayed root 0600 and PulseAudio
  used the dummy sink. accel: added `/dev/console 0600 /dev/sound/*` and
  `/dev/video[0-9]+` lines. Proper fix: gdm (oi-gnome patch 0010) or the
  system logindevperm, to be decided.
- **PulseAudio hang:** with permission, module-detect also opens dsp1 =
  audiohd#1 (Haswell HDMI audio), "Failed to reset stream 1", and the
  daemon's IO thread hangs (autospawn storm behind the lock). Workaround:
  ~/.config/pulse/default.pa = system file with module-detect replaced by
  `module-oss device=/dev/dsp0 sink_name=internal`. audiotest: internal
  codec OK (47949 Hz), only audiohd#1 "I/O error". HDMI audio under i915n
  needs the display power well (Linux: i915 audio component): open.
- **USB disk:** accel's SMF hal had no HALD_RUNNER_PATH=/usr/lib/hal/oi
  (gnome45's repository has none either), so the stock prober ignored the
  GPT partitions. Set + restart hal, rmvolmgr: /media/Megastore21 rw.
- **vblank:** DRM_IOCTL_WAIT_VBLANK EINVAL on all pipes after boot
  (Linux drm_vblank_get: refcount > 0, enabled = 0, i.e. vblank_reset
  without intel_crtc_vblank_on for the firmware-lit pipe). A DPMS off/on
  (xset dpms force off/on) runs intel_crtc_vblank_on and fixes it until
  the next boot; the user session's own X start does too. Test tool
  illumos-drm-haswell/vblwait.c. Kernel bug for the illumos-drm maintainer.
- **Desktop drawing wrong** (black boxes for ARGB shadows, GTK4 windows
  empty, Shell clock missing): not Mesa uploads (textest.c: RGBA/R8/BGRA,
  atlas sub-images all OK on i965 and crocus). Xorg: `[DRI2] DRI driver:
  i915`, AIGLX "Calling driver entry point failed", swrast. Cause:
  dri2_probe_driver_name() without a PCI entity (modesetting old probe +
  kmsdev) uses the kernel name. xorg-server patch
  38-dri2-driver-name-from-drm-device.patch (drmGetDevice2 PCI IDs ->
  driver_map), COMPONENT_REVISION 20.
- **Later the same evening (BE accel, after the clean reboot 21:02):**
  xorg-server rev 20 with 38-dri2-driver-name-from-drm-device.patch:
  `[DRI2] DRI driver: i965`, "AIGLX: Loaded and initialized i965". GTK4
  draws correctly on the GPU (widget factory ngl == cairo), but GNOME
  Shell keeps showing a stale frame of GPU-drawn windows (Nautilus
  skeleton, black ARGB shadows): texture-from-pixmap over DRI2 does not
  see updates. Workaround: `export GSK_RENDERER=cairo` in ~/.profile
  (gdm's Xsession sources it). Window frames, close buttons, Nautilus OK.
- ConsoleKit session: x11-display-device '?', active FALSE, so gdm skips
  di_devperm_login (logindevperm lines for /dev/console never apply).
  Likely from the /dev/fb -> /dev/null workaround; real fix = console
  takeover in i915n. Sound devices handed over by hand once per boot.
- PulseAudio: module-suspend-on-idle off in ~/.config/pulse/default.pa
  (waking codec + Apple amp lost the start of sounds). After a pulse
  restart, gsd-media-keys loses its volume-click sound until re-login.
- Two gnome-shell crashes at login 21:05/21:06 (core in ~): SIGSEGV in
  st_theme_node_lookup_shadow, pointer = "FS-UAE L" text (heap reuse).
  Not reproduced since; GNOME then set disable-user-extensions=true
  (reset to false by hand).
- HAL: HALD_RUNNER_PATH=/usr/lib/hal/oi set on svc:/system/hal in accel.
- Button layout appmenu:minimize,maximize,close; GNOME Tweaks 45.1
  packaged (packages/desktop/gnome-tweaks, oi-gnome) and installed.
- Amiga files on the iMac: Kickstarts, Floppies, OI-adapted configs,
  AMIGA_UNIX/Workbench/Work.hdf (sha256 checked).
- User: GNOME, Quake videos and RtCW smooth on i965.
- **Compositor investigation (22:00-23:10, no fix yet):** stale GPU-drawn
  windows + black ARGB margins only in the user session; the greeter on
  the same Xorg draws correctly (glxgears frame, GTK4 ngl window with
  transparent margin). Ruled out: Mesa uploads (textest.c), MUTTER_SYNC
  (Solaris gnome-session patch 12-34592575; tested with the line off for
  one login, then pkg revert), X credential switch at login (no ioctl
  EPERM; DTrace), GEM_OPEN handle aliasing (drm-core matches Linux),
  texture-from-pixmap itself (tfptest.c: alpha and updates correct, DRI3
  and DRI2), sync ring (no GL_EXT_x11_sync_object), GTK compositing check
  (is_composited TRUE). Seen: clients and shell run DRI3/Present
  (glamor_pixmap_from_fd ok, present_fake_get_ust_msc for windows);
  GTK4 presents 1-2 frames then stops; gnome-shell schedule_update x20
  with 1 swap (but some traces were taken while the session was locked
  and DPMS off: mutter PowerSaveMode 3 inhibits frames: redo active).
  Side finding: Xorg gets EINVAL from 4 DRM_IOCTL_GEM_CLOSE calls.
  vblank timestamps are CLOCK_MONOTONIC (cap 1) but ~0.4 ms in the future
  (vbltime.c).
- gnome-shell --replace inside a gdm session ends the session (and the
  replaced shell crashed in st_theme_node_lookup_shadow again, core in
  ~/cores/core.2236): never do that again. Shell cores go to ~/cores.
- 23:10 reboot with an anonymous DTrace enabling (vblank on/off/reset +
  stacks) to see who leaves vblank off after boot.
- **Console fix (23:18, BE accel-con = accel + new i915n):** i915n console
  takeover now fences the X-tiled Apple GOP framebuffer
  (illumos-drm-haswell/0017-...diff in drivers/i915/illumos/
  i915_illumos_con_kms.c, uncommitted in ~/src/drm/illumos-drm on the VM).
  Console works, /dev/fb -> gfx0, X on VT 7 without the /dev/fb hack,
  ConsoleKit session active (x11-display-device /dev/vt/7), /dev/sound
  owned by the gdm greeter user (logindevperm works again). My
  /dev/console lines removed from /etc/logindevperm in accel-con (now as
  gnome45 apart from drmadm's own /dev/dri rule).
- Hypothesis for the compositor stale frames: the user session was
  ConsoleKit-inactive (active FALSE, display device '?') because of the
  /dev/fb hack; the greeter was active. Check at the next user login on
  accel-con.
- GNOME shows UTC (22:21 at 23:21 CET): TZ not passed to the session; open.
- Workaround list (accel-con): /dev/fb hack GONE; manual /dev/sound chown
  GONE (to verify at login); logindevperm lines GONE; GSK_RENDERER GONE;
  OPEN: ~/.config/pulse/default.pa (dsp0 only, HDMI dsp1 hangs PA: fix
  audiohd to fail cleanly), HALD_RUNNER_PATH set by svccfg (package it),
  facet version-locks released by hand (incorporation).
- BEs now: gnome45 (default), accel, accel-con (boot -t), accel-backup-1
  (pkg's automatic backup), plus the older test BEs.

## 2026-11-11 morning: empty windows = locale bug in Mesa; shader cache

- Console fix 0017 on a cold boot: fence kept, /dev/fb -> gfx0, X on VT 7,
  ConsoleKit session active (/dev/vt/7), sound devices handed over by
  logindevperm. Still: the console text is garbled for a moment between i915
  init (fences cleared) and the console takeover; real fix = pin the fence
  right after fence init. Open.
- gnome-shell crash at login (st_theme_node_lookup_shadow, NULL CSS
  declaration via a StThemeNodeTransition): only with animations, which
  GNOME turns off for software rendering (why gnome45 never hit it).
  Temporarily enable-animations=false for the user; St patch open.
- "Stale/empty windows" (Nautilus without text/icons, empty mutter-x11-frames
  title bars, Firefox private window white + garbled text) were NOT the
  compositor or i915n: LC_NUMERIC=de_DE.UTF-8 in the session. Two causes in
  Mesa 21.3.9 src/util/strtod.c: (1) strtod_l only under _GNU_SOURCE (Linux
  only) -> plain strtod; (2) newlocale(LC_CTYPE_MASK, "C", NULL): on illumos
  a NULL base copies the process locale for the other categories, so
  LC_NUMERIC stayed de_DE (looks like an illumos libc bug vs POSIX; report).
  Fix: patch-src_util_strtod.c.patch (HAVE_STRTOD_L only, LC_NUMERIC_MASK).
  Test: floattest.c (GBM+EGL, float literals under de_DE).
- OI Mesa was built with -Dshader-cache=false: every program compiled all
  shaders at each start (title bars took seconds). Now enabled +
  patch-src_mesa_drivers_dri_i965_brw__disk__cache.c.patch (no GNU build ID
  on illumos) + REQUIRED_PACKAGES compress/zstd. Mesa 21.3.9 rev 7 installed
  in accel-con; GL test 0.78 s -> 0.05 s with a warm cache.
- Ruled out on the way: i915n purge/shrink/evict (15 min kernel trace: none),
  glamor put/copy/composite (puttest.c, bit-exact), GLX texture_from_pixmap.
  "Black frames" were my xwd -root screenshots (bypass compositing); use
  gnome-screenshot for what the user sees.
- Session env has no TZ (ssh login has TZ=<zone>) -> GNOME shows UTC.

## 2026-11-11 later: accel-con is the default BE

- accel-con became the default BE (user, after sound, HDD and desktop
  checks); gnome45 stays as fallback. Clean reboots at 10:00 and 10:35: all
  checks green (console fence, /dev/fb -> gfx0, HAL env from the package
  profile, Megastore21 auto-mounted, TZ in the session, ConsoleKit active,
  /dev/audio to the user, no gnome-shell or gsettings-data-convert crash).
- Packages fixed today (all in components, uncommitted): Mesa 21.3.9 rev 7
  (strtod locale, shader cache + i965 build-ID fallback, zstd dep), gdm
  45.0.1 rev 1 (0033 session TZ), gnome-shell 45.9 rev 1 (22 St stylesheet
  refs), hal-probe-storage-gpt rev 2 (SMF site profile; no hal restart on
  install: a hald restart unmounts removable disks, and IPS also runs the
  old version's restart_fmri on update), gnome-settings-daemon 45.1 rev 1
  (08 convert file -> org.gnome.desktop.peripherals.mouse), desktop-cache
  0.2.2 rev 25 (12 icon-cache checks the directories index.theme lists:
  23202 -> 2371 stat calls, 3.3 -> 0.1 s warm; skips cursor-only themes).
  tools/oi-release-version-locks.sh for a fresh install.
- Boot stays slow (~90 s to gdm, icon-cache still 22 s cold although it
  rebuilds nothing): the boot disk is an APPLE HDD HTS541 (2.5" 5400 rpm),
  every service hits it cold at once. User: leave as is; an SSD is the real
  fix. Login 16 s (was 29 s with the gsettings-data-convert crash loop).
- Left open: console garbled briefly at driver load (fence right after
  init), HDMI audio (i915 audio power well; then ~/.config/pulse/default.pa
  can go), nwam-manager core at logout (X goes away).

## 2026-11-15: unified stack and the audio/SMC updates (BEs unified-imac, unified-imac-a)

- `unified-imac`: clone of accel-con + illumos-drm 0.0.2.2 (series
  d3b8f9b7: drm-core, drm-helpers, drm-kms, drmadm, i915n, linuxkpi);
  regression passed (see mac-unified-stack-plan-2026-11-14.md).
- `unified-imac-a` (user: "the audio fixes should got into it", "update Mac
  Sensors mikmod etc"): clone of unified-imac + audiohd 23381.7, applesmc
  0.5.11-2026.0.0.3 (fans), Mac Sensors 1.3-2026.0.0.1 (RBAC helper),
  system/vt-hotkeys 1.0, from ~/xfer/imac-extras-20261115.p5p (sha256
  c3670544...5593), added as an extra origin of oi-gnome and userland.
  Installed with `--be-name unified-imac-a --no-be-activate`, booted once
  (`beadm activate -t`); accel-con stays the default.
- audiohd 23381.7 = ports/audiohd-apple (patches 0001-0005) built for this
  OS: `gmake GATE_COMMIT=08e59b676b426617f8720d1f2fa14d9e2db2afa2
  COMPONENT_ARCHIVE_HASH=sha256:<archive> OSNET_VERSION=0.5.11-2026.0.0.23381
  REPO=<tmp repo> publish` in a copy of the port (all five patches apply,
  72 undefined symbols in unix/genunix/drv/audio). The port's .7 is for 23395.
- Result: /dev/sndstat audiohd#0 CS4206 (DUPLEX) + audiohd#1 Intel HDMI
  0x80862807 (OUTPUT, open fails ENODEV without a monitor); /dev/sound/*
  owned by the user after login, PulseAudio per user; applesmc 1 fan;
  vtdaemon online. Still 68 HDMI codec timeouts at boot (display audio
  power well, i915n).
- Not installed: PulseAudio r7 and media/mikmod 3.2.10: built on the VM
  (23395), they require system/library (and math, ksh93) @23395, i.e. an
  OS update of the iMac's BE to 23395. The fan presets in Mac Sensors need
  the "Apple SMC Fan Control" profile for the user (not assigned).

## 2026-11-15 afternoon: HDMI audio (audiohd#1) and the garbled boot console

Analysis (boot messages, mdb -k, source):
- audiohd#1 (8086:0d0c, codec Intel Haswell HDMI 0x80862807): the 68
  "timeout when get response from codec" at each boot were all node 4,
  which does not exist: stock audiohd sets last_wid = first + count and
  loops <= last_wid (one node past the end; Intel's HDMI codec does not
  answer it). The codec also showed 1 converter + 1 pin until the vendor
  bit (node 0x08, get 0xf81 / set 0x781, bit 0x01, Linux
  intel_haswell_enable_all_pins); converters/pins may stay in D3.
- Display power/CDCLK: i915n 8fcd677 exports i915n_audio_power_get/put
  and i915n_audio_cdclk_khz; audiohd patch 0006 uses them (CDCLK 540000
  kHz, BCLK M/N 2/45).
- Garbled screen before the console prompt: Apple's GOP framebuffer is
  X-tiled and shown linearly through fence 0; intel_ggtt_init_fences()
  cleared all fences early in attach and the console takeover re-fenced
  only at the end, so boot-console writes and scrolls in between landed
  untiled. i915n 03cde28 keeps a fence the firmware left valid until the
  takeover adopts it (or releases and clears it without a console).

Fixes: audiohd 23395.8 = patches 0001-0008 (0007 node range, 0008 Intel
display codecs: vendor bit, D0, names); i915n 0.0.2.2 (20261115T131554Z)
with 8fcd677 + 03cde28; ~/xfer/imac-hdmi-audio-20261115b.p5p (sha256
d5323c54...0713). BE unified-imac-d, booted once 14:22 (accel-con stays
default).

Checked on that boot: 0 codec timeouts (68 before); codec named "Intel
Haswell HDMI"; mdb: nodes 2-4 converters, 5-7 pins, 8 vendor, last_wid 8,
3 output paths; "fence 0: kept from the firmware (0x00fff03b00000001)"
then "console: the firmware's fence 0 adopted" and "shown linearly
through fence 0"; i915n_smoke OK, gdm, glamor, /dev/fb -> gfx0, CS4206
speakers present. Not checked: the screen itself during boot (user), the
widgets' power state read back, HDMI sound with a monitor. The anonymous
DTrace boot trace of fence writes was lost (dtrace -ae consumed it
without printing).
