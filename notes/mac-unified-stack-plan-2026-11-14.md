# One OpenIndiana stack for all our Macs: plan of 2026-11-14

Goal (user, 2026-11-14): a single installation environment we can boot and
test on every Mac, from the Mac Pro 2013 to the MacBook Pro 2017. It brings
together:
- the Haswell work (i915n, iMac14,1);
- the Radeon work (amdgpu SI, Mac Pro), plus Polaris (Radeon Pro
  555/560/570/575/580 and the X variants);
- the new userland graphics stack (libdrm 2.4.134, Mesa 26.2.6, Xorg 21.1.25);
- USB (root on USB, USB 3 disks);
- the SMC (applesmc, smcstat, Mac Sensors).

Every piece stays a package (component + pristine + patches) on the one
publisher `oi-local`. The test environment is a BE built from packages
only (open-topics section 9 rule).

Owners: **R** = Radeon/graphics session (kernel drm, Mesa, Xorg),
**M** = the Mac session (Mac work, install, USB/HAL/sound/SMC packaging).
**U** = a decision for the user.

## 0. Where things stand (checked on the VM today)

| Piece | State | Where |
|---|---|---|
| amdgpu, SI (Tahiti) | works on the Mac Pro; default series con9 | illumos-drm `amdgpu/con9` 977972e = main e50b0ce + 21 commits; adds its own **facade 2.1** (GEM reservations, replace_fences) |
| i915n, Haswell | worked on the iMac14,1 (2026-11-11) | the illumos-drm maintainer's `i915n/haswell-series` f5ff33a: e50b0ce + 13 commits, **our 0016 (execbuffer) and 0017 (X-tiled console fence) are in it**; adds its own **facade 2.1** (`drm_illumos_ioctl_nonblock`), package version 0.0.2.1 |
| Both together | **not possible yet**: the two series share no commit after e50b0ce, and both edit `drm_facade_version.h`, `drm_facade.mapfile`, `drm_core.mapfile` and `bindings.rs` (two different "2.1" ABIs) | - |
| i915n display platforms | the platform gate (0011, the illumos-drm maintainer) admits display on Ivy Bridge, plus Haswell after all-display-paths; Kaby Lake (gen9.5) untested | illumos-drm README |
| Mesa 26.2.6 r1 | `gallium-drivers=softpipe,llvmpipe,i915,r600,radeonsi`; **no crocus, no iris**, no Vulkan; platforms x11 | ~/src/oi-userland-mesa26 branch mesa-26 (3679c445) |
| Haswell's Mesa work | was on Mesa 21.3.9 (i965 + crocus; i965 no longer exists in 26) with patches: strtod locale, disk cache without GNU build ID, brw_program_binary | ~/src/oi-userland-mesa (uncommitted); strtod is already in the 26 patch set |
| Xorg 21.1.25 + glamor, input drivers rebuilt, patch 39 (no console fb) | on the Mac Pro | ~/src/oi-userland-xorg21 |
| Haswell's Xorg fix 38 (DRI2 driver name from the DRM device) | only for 1.19.7 | oi-userland-mesa worktree, untracked |
| libdrm 2.4.134 | gfx-drm, with amdgpu.ids | ~/src/drm/gfx-drm(-libdrm-amdgpu) |
| GPU firmware | only tahiti_{ce,me,pfp,rlc} in a local dir; not a package | ~/src/fw-radeon-tahiti |
| USB | hubd root-on-USB fix (2 bugs), tested in QEMU and on the Mac Pro stick; scsa2usb `usb_pipe_xopen` (USB 3 disks) still open | ~/src/illumos-usbroot, open-topics 4 |
| SMC | applesmc driver + smcstat (`driver/sensor/applesmc` 0.5.11-2026.0.0.2), Mac Sensors 1.2 (oi-userland PR 28357); keys for MacPro6,1 and iMac14,1 | ports/applesmc, ports/mac-sensors |
| Rest of the Mac set | audiohd Apple (CS4206 GPIO, HDMI/DP fixes), bge 57766, HAL (NTFS/GPT/eject), vt-hotkeys, PulseAudio r6 | ports/, packages/ |

Mesa 26's source has `crocus` and `iris` (gallium) and `intel` and
`intel_hasvk` (Vulkan), so the Intel side needs no new Mesa code, only
the build options and the illumos patches.

## 1. The machines and what each one needs

| Mac | GPU(s) | Kernel driver | Mesa | Notes |
|---|---|---|---|---|
| Mac Pro 2013 (MacPro6,1) | 2x FirePro D500, Tahiti (SI) | amdgpu SI (done) | radeonsi | reference machine |
| iMac 21.5" Late 2013 (iMac14,1) | Iris Pro 5200, Haswell GT3e | i915n (done on the old stack) | crocus (new; i965 is gone) | must be retested on Mesa 26 / Xorg 21 |
| iMac 27" 2012 (iMac13,2) | NVIDIA Kepler (GTX 6x0M) | none of ours (nvidia-470 is the only option) | - | firmware framebuffer only; out of this plan's scope |
| iMac 21.5" Retina 4K (to confirm: iMac18,2 2017 or iMac19,2 2019) | Radeon Pro 555/560 (X), Polaris 21 | **amdgpu VI (new)** | radeonsi | best first Polaris target: desktop, no mux, no battery |
| iMac 27" 5K 2019 (iMac19,1) | Radeon Pro 570X/575X/580X, Polaris 20 (or Vega 48: GFX9, out of scope) | **amdgpu VI (new)** | radeonsi | second Polaris target |
| MacBook Pro 2017 (to confirm: 13" MacBookPro14,1/14,2 or 15" MacBookPro14,3) | 13": Iris Plus 640/650 (Kaby Lake GT3); 15": HD 630 (Kaby Lake GT2) + Radeon Pro 555/560 behind gmux | i915n gen9.5 display (untested) / amdgpu VI | iris / radeonsi | the most work: section 6 |

**U 1:** which MacBook Pro 2017 (13" or 15", with or without Touch Bar),
and which Polaris iMac is on hand first.

## 2. Step A: one illumos-drm series with i915n and amdgpu  [R]

1. New branch `mac/unified` in the original tree path
   (~/src/drm/illumos-drm), based on `origin/main`:
   - merge `i915n/haswell-series` (the illumos-drm maintainer's facade 2.1, our 0016/0017);
   - then rebase our 21 amdgpu commits (`amdgpu/con9`) on top.
2. One facade ABI: the illumos-drm maintainer's 2.1 stays 2.1 (it is upstream-bound and
   already numbered). Our additions (GEM reservations in Linux's order,
   replace_fences, the LinuxKPI pci_dev change) become **facade 2.2** on
   top: new mapfile version node, `drm_facade_version.h` 2.2, regenerated
   `bindings.rs`, package version 0.0.2.2.
3. Rebuild everything against it: i915n (pending since the LinuxKPI
   pci_dev change), amdgpu, drm_core, drm_kms, drm_helpers, linuxkpi.
4. One p5p (`illumos-drm-0.0.2.2-unified.p5p`) with i915n and amdgpu.
   `drmadm` selects per device ID, so each Mac binds only its own driver.
5. Regression tests before anything new:
   - Mac Pro: con9's checks (console, VTs, X/glamor, DP audio, 80-85 fps).
   - iMac14,1: i915n_smoke and the 0017 console line.
6. The facade 2.2 work goes to the illumos-drm maintainer as a PR (**U**: when, and as which
   series).

### State 2026-11-15 (built, not yet tested on a Mac)

Branch `mac/unified` on the VM (~/src/drm/illumos-drm), local only,
nothing pushed. On top of origin/main 56e0bd3 (which already has the
Haswell series) and the 21 replayed commits (facade 2.2):

- `linux-pristine-amdgpu` (worktree ~/src/drm/lp-amdgpu), db8f0dd: the
  pristine import of the 1387 Linux v7.2 files amdgpu builds from,
  322 MB, mostly amd/include/asic_reg. Classes: 1253 MIT notice, 109
  SPDX MIT, 23 GPL-2.0 OR MIT, 1 (GPL-2.0 WITH Linux-syscall-note) OR
  MIT (kfd_sysfs.h; my importer first refused it, the gate's own
  check-licences.py accepts it), 1 HPND. Refused: amdgpu_vkms.h
  (GPL-2.0+). Merged as dbe04eb.
- abd72d6: MIT stand-in for amdgpu_vkms.h (the seventh);
  verify-vendored.sh passes (2236 digests, 7 stand-ins, 1440 inputs).
- 4e0d1e7 .. 855f8c8: the 26 drivers/amdgpu patches as port commits.
  All 1440 amdgpu inputs equal the old fetch-and-patch tree byte for byte.
- 20d2c39: amdgpu as a dmake module (usr/src/uts/intel/io/amdgpu,
  usr/src/uts/intel/amdgpu), fetch-and-patch/build.sh/patches retired;
  same 409 TUs; same 14729 defined and undefined symbols as con9's
  module.
- 1814c44: every package at 0.0.2.2.

Build: `. ./myenv.sh` needs `PATH=$HOME/.cargo/bin:$PATH
LD_LIBRARY_PATH=/usr/gcc/14/lib/amd64` (/usr/bin/cargo cannot take
`+nightly-2026-04-16`) and `DDI_MAPFILE=~/src/drm/illumos-gate/usr/src/
uts/common/mapfiles/ddi.mapfile`; then `cd usr/src && dmake setup &&
dmake install && dmake package` (about 10 min at 8 jobs).

p5p: ~/src/drm/illumos-drm-0.0.2.2-unified.p5p on the VM, 8 packages
(amdgpu, drm-core, drm-helpers, drm-kms, drmadm, i915n, linuxkpi,
virtio-gpu), sha256
1f238e4f2793e83747e6283ce3283bc1d670a5e11f9b3ca6889c576e0c58cc98.
The Tahiti firmware is in it but not in Git (staging refuses without it).

Left in step A: the regression tests of point 5 (each Mac install on the
user's OK), then point 6.

### State 2026-11-15 late morning: the series for upstream (point 6)

- VM branch `series/iggi-amdgpu-20261115` (worktree ~/src/drm/series-amdgpu),
  38 commits on upstream main dae1bc8 (after his #33), head 5562b4e:
  shared pieces (check-licences, drm_helpers, 2x linuxkpi, facade 2.2),
  merge of linux-pristine-amdgpu, the vkms stand-in, the 26 port commits,
  then gate / packaging / docs / 0.0.2.2. The fetch-and-patch history of
  mac/unified is left out. Its tree = mac/unified + #33 (changed lines
  identical) + README/userland docs updated + the dead duplicate
  illumos/amdgpu_vkms.h removed.
- Built (dmake setup/install/package), amdgpu symbols and NEEDED identical
  to mac/unified; verify-vendored.sh with all 4 input lists: PASS (2236
  digests, 7 stand-ins, 2813 inputs, 2243 files 0 refused).
- p5p `~/src/drm/illumos-drm-0.0.2.2-series-20261115.p5p`, sha256
  15bae79f0985c5d9699836f290de5aa563a052d08289b830518db46c06981109; the Mac
  session tests this one on the Mac Pro (BE unified-a1, not activated)
  instead of the mac/unified p5p 1f238e4f.
- Pushed 2026-11-15 (user: "Yes, push it") to
  github.com/isoriano1968/illumos-drm (5562b4e), then the fix 431847d
  (user: "Yes, push the fix").
- 2026-11-15 VA-API deadlock on con9 (unkillable ffmpeg): TTM ghost
  objects copied _resv.lock.ctx, dma_resv_init kept it, the ghost's unlock
  went down the transaction path and was refused silently; ghosts stayed
  Raw-locked behind threads back in user space (mdb: 22 delayed-delete
  workers waiting; the core's "unlock_ctx ... refused" warning 11 times
  since 2026-11-13). Fixed in LinuxKPI, 431847d; p5p
  `illumos-drm-0.0.2.2-series-20261115b.p5p` sha256 d3b8f9b7...e727.
- Mac Pro regression in BE unified-a1 (d3b8f9b7, booted once, con9 stays
  default): passed (console, VTs, DP audio, X/glamor, 83 fps per GPU,
  VA-API + gpuburn + Firefox WebGL without resv waiters or warnings).
- iMac14,1 regression 2026-11-15 12:23 (user: "do the iMac test now"): BE
  unified-imac = clone of accel-con + drm-core, drm-helpers, drm-kms,
  drmadm, i915n, linuxkpi 0.0.2.2 from d3b8f9b7 (illumos-drm origin in that
  BE only switched to ~/xfer/illumos-drm-0.0.2.2-series-20261115b.p5p),
  `beadm activate -t` + `reboot -p`; accel-con stays default. Passed: VBT
  from OpRegion, X-tiled console through fence 0 on pipe B, /dev/fb ->
  gfx0, console_user -> 7, i915n_smoke OK (1000 batches 0.022 s), gdm,
  Xorg modeset + glamor, DRI2 i965, AIGLX, DP-3 1920x1080. The old
  "dma_resv_init of a reservation that has one already" warning is gone.
- Not done: send the mail (user's OK on the text).

## 3. Step B: userland for Intel on the new stack  [R]

1. Mesa 26 r2: `gallium-drivers += crocus,iris`. Port the Haswell
   patches that still apply in 26:
   - strtod: already in the 26 patch set;
   - disk cache without a GNU build ID: the util part only, the i965
     part is gone;
   - check whether crocus or iris need the brw_program_binary
     equivalent (the `intel/compiler` build-ID lookup).
   Vulkan (`intel`, `intel_hasvk`, `amd`) is a separate later step, not
   in r2 (needs the DRM syncobj and timeline ioctls checked on both
   drivers).
2. libdrm: build `intel` along with `amdgpu`/`radeon` (iris and crocus no
   longer use libdrm_intel, but other consumers still do).
3. Xorg 21.1 r2: check whether 21.1's modesetting still picks the wrong
   DRI2 driver name on i915n, which is what patch 38 fixed on 1.19.7. If
   it does, forward-port 38; if not, drop it.
4. The rule from the graphics-stack memory: Mesa, libdrm, Xorg and the
   drivers are checked and published as one set (one `pkg update`).
5. Test on the iMac14,1, in a new BE from packages:
   - GBM/EGL triangle (`eglgbm.c`), crocus;
   - X with glamor;
   - the textest and locale checks;
   - GNOME: check the stale-frame compositor problem again (it was on
     1.19.7 DRI2);
   - ioquake3.

## 4. Step C: GPU firmware as packages  [R]

New components from the pristine linux-firmware release (license
LICENSE.amdgpu / LICENCE.i915), redistributable as-is:
- `system/firmware/amdgpu-si`: tahiti_* (replaces ~/src/fw-radeon-tahiti
  and whatever the con9 p5p carries today).
- `system/firmware/amdgpu-polaris`: polaris10/11/12_{ce,me,mec,mec2,pfp,
  rlc,sdma,sdma1,smc,smc_sk,k_smc,mc,k_mc,uvd,vce}.bin.
- `system/firmware/i915`: kbl_dmc (Kaby Lake display power states), and
  skl/kbl GuC/HuC only if i915n ever uses them.
illumos-drm's loader paths decide the install location (check against
the con9 p5p).

## 5. Step D: Polaris (VI, GFX8) in amdgpu  [R]

Facts from the Linux v7.2 tree:
- `vi_set_ip_blocks` for POLARIS10/11/12 adds vi_common, gmc_v8_0,
  tonga_ih, gfx_v8_0, sdma_v3_1, pp_smu (powerplay SMU7) and
  uvd_v6_3/vce_v3_4.
- For the display it has **only `dm_ip_block` (Display Core)**: v7.2
  has no legacy `dce_v11_0.c` any more (only dce_v6/v8/v10 remain), so
  the dce_v6_0-style path used on the Mac Pro does not exist for Polaris.

Plan, in test order (each step its own build and p5p name, one test at a
time):
1. **Headless bring-up** (display as `virtual` / no DC): vi.c,
   gmc_v8_0, tonga_ih, gfx_v8_0, sdma_v3_0, ring and IB tests, GEM and
   render node. radeonsi offscreen (GBM/EGL) on the firmware framebuffer
   console. Firmware loading for polaris*. VBIOS source on the Mac to
   be found first (VFCT like the Mac Pro, or ATRM): from macOS
   (`ioreg`) or the ACPI tables read by illumos, not Linux (no Linux
   boots).
2. **Powerplay (SMU7)**: smu7_hwmgr + polaris10_smumgr. Mature on Linux,
   and different code from SI dpm, so SI's parked problem does not carry
   over. Clocks up under load.
3. **Display Core** for DCE 11.2: amdgpu_dm + dc/core, dc/dce,
   dce110, dce112, bios, gpio, irq, link, hwss. **CONFIG_DRM_AMD_DC_FP
   off**, so there is no DCN or DML and no kernel FPU. The DRM helpers
   DC needs come in through drm_kms/drm_helpers: atomic helpers, DP
   helper, DP MST (later); HDCP and CEC off.
   This is the largest single item, a few hundred translation units; SI
   was 409 in total.
4. **Console** on DC (reuse amdgpu_illumos_con_kms.c; the firmware
   framebuffer takeover is the same idea) and VTs.
5. X with glamor (radeonsi), DP/HDMI audio through the DC audio path +
   audiohd, then UVD/VCE.
First machine: the 555/560 iMac (no mux), then the iMac19,1, then the
MacBook Pro 15".

## 6. Step E: the MacBook Pro 2017  [R graphics, M rest]

Each item needs to be checked on the machine (from the illumos side;
`prtconf -vp`, `pcitool`, ACPI table dump) before any code:

| Area | What is there | illumos today | Plan |
|---|---|---|---|
| iGPU (all models) | Kaby Lake: Iris Plus 640/650 (13"), HD 630 (15") | i915n headless at least; display untested on gen9.5 | admit Kaby Lake in the display gate, eDP panel, DMC firmware; iris |
| dGPU (15") | Radeon Pro 555/560 behind Apple gmux | - | step D; first only the GPU that EFI hands the panel to (the dGPU on Linux by default) |
| gmux (15") | panel mux + backlight | none | small driver: read the mux state; backlight first; switching later or never |
| Keyboard/trackpad | Apple SPI (Linux `applespi`) | **no driver** | USB keyboard/mouse first; an SPI driver (Intel LPSS SPI + Apple protocol) is its own project (**U**) |
| SSD | Apple NVMe | nvme(4D): to test | check attach and boot; Apple quirks if needed |
| USB-C / Thunderbolt 3 | xHCI on the Alpine Ridge controller, PCIe hotplug | pciehp + xhci: to test; hubd root fix needed for booting the stick | boot the test stick from USB-C; check hotplug |
| Wi-Fi | Broadcom BCM43602 | **no driver** | USB Ethernet / USB-C dock |
| Sound | Cirrus codec (to read from the codec dump) | audiohd: to test | codec dump first, then decide whether it is like the CS4206 GPIO fix |
| Battery / AC | ACPI battery + SMC keys | acpi_drv: to test | acpi_drv first; SMC keys in smcstat |
| SMC | same SMC, newer key set | applesmc: to test | section 7 |
| Suspend | - | not usable on illumos | out of scope |

## 7. Step F: USB and SMC for every Mac  [M, R for SMC]

USB:
1. hubd root-on-USB fix (2 bugs) as a package (`system/kernel` port, as
   applesmc) and upstream illumos issue. Needed by every Mac: the test
   environment is a USB stick, booted on any port.
2. scsa2usb `usb_pipe_xopen()` (USB 3 disks drop after attach):
   - fix, test with the Seagate and the TOSHIBA, package;
   - check the xHCI on Thunderbolt/USB-C on the MacBook.

SMC (applesmc, smcstat, Mac Sensors):
1. Key types: the newer SMCs (2016 and later) report fans and some
   sensors as `flt ` (32-bit float). Check smcstat's decoding against
   the MacBook's key list (`smcstat -l`), and add `flt ` if it is missing.
2. MacBook keys:
   - battery: B0AC, B0AV, B0RM, B0FC, BATP;
   - AC: AC-W;
   - charge limit: BCLM, read only unless the user wants write;
   - fans: F0Ac/F0Mn/F0Mx (and F1 on the 15");
   - ambient light: ALV0/ALI0;
   - keyboard backlight: LKSB, if SMC-driven on 2017; else SPI;
   - the CPU/GPU/palm-rest temperatures.
   The key names are to be confirmed from the machine's own key list,
   not assumed.
3. Mac Sensors: a MacBook drawing and cards (CPU, GPU, battery, palm
   rest, fans) chosen from the keys present, as done for the iMac14,1.
   Also the Polaris iMacs' keys.
4. ksensor node permissions for the console user (**U**, open-topics
   4.3).
5. Write access (fan minimum, charge limit) only behind root/RBAC, and
   only if the user wants it (**U**).

## 8. Step G: the one test environment  [M]

1. A USB stick, the Verbatim or a new one; **never** the Mac Pro SSD. One
   BE built from packages only:
   - OI hipster;
   - oi-local, which carries: illumos-drm unified, firmware packages,
     libdrm, Mesa 26 r2, Xorg 21.1 r2 + drivers, audiohd Apple, applesmc,
     Mac Sensors, hubd/scsa2usb fixes, HAL set, bge 57766, vt-hotkeys,
     PulseAudio, GNOME 45 set.
2. Nothing machine-specific in the BE: drmadm and driver aliases pick the
   GPU driver. Where a Mac has no driver (iMac13,2 NVIDIA, Polaris
   before step D), the firmware framebuffer + illumosfb/vesa path is the
   fallback.
3. Per-machine checklist (one table per Mac in its notes file):
   - boot from the stick on any USB port;
   - console and VTs;
   - X with glamor, `glxinfo -B`, GNOME, window drag;
   - sound on speakers and HDMI/DP;
   - disks automount and eject;
   - Mac Sensors cards;
   - boot messages free of known warnings;
   - `pkg verify` clean.
4. Updating the stick = `pkg update` from oi-local, never a hand copy.

## 9. Order and dependencies

1. **A** (unified illumos-drm). It blocks the rest, because one driver
   package must serve all Macs.
2. **B** and **C** next to A (userland and firmware; no kernel change).
3. **G** with A+B+C: stick, Mac Pro + iMac14,1 regression. This is
   the first milestone: "one stick, both current Macs, no loss".
4. **F** in parallel (M): USB packages first (the stick depends on hubd).
5. **D** Polaris: headless, powerplay, DC, console. The long item.
6. **E** MacBook Pro last, once the machine has been read (section 6
   table filled in), and after **U 1**.

## 10. Decisions for the user (U)

1. Which MacBook Pro 2017 model, and which Polaris iMac first.
2. When and how the facade 2.2 series goes to the illumos-drm maintainer (and whether to
   rebase onto his newer main first).
3. MacBook internal keyboard/trackpad: own SPI driver project, or USB
   input only for now.
4. ksensor permissions; SMC write access (fans, charge limit) or read only.
5. Vulkan (anv/hasvk/radv) in this round or later.
