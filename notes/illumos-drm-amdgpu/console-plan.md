# amdgpu as the illumos console (open-topics 1.1): plan

2026-11-13. Goal: on the Mac Pro (and any machine where amdgpu drives the
display the loader used) the illumos console, tem and the virtual
terminals work on amdgpu's display, so `/dev/vt/console_user` exists and
logindevperm hands sound, keyboard, mouse and `/dev/dri` to the user who
logs in, as on the iMacs (vgatext/gfx_private on the loader framebuffer).

## Today

- Boot: `tem: terminal emulator: Compatible fb not found`, `consconfig:
  terminal emulator failed to initialize`. consconfig opens the console
  display node (amdgpu1, the display GPU at `pci8086,e08@3`), its first
  minor is DRM's card0, which knows no VIS_DEVINIT.
- No VTs: X runs without a console device (Xorg patch 39), input through
  conskbd/consms, `/dev/vt/console_user` -> `/dev/console`, so logindevperm
  gives the user nothing (sound root 0600, HAL eject refused).

## Model: i915n's console (illumos-drm)

`drivers/i915/illumos/i915_illumos_con.c` (illumos side, 501 lines) and
`i915_illumos_con_kms.c` (KMS side, 561 lines). No illumos-gate change: it
uses misc/gfx_private, the gate's contract-private interface for DRM
drivers (as the legacy i915 did):

1. at attach, before the DRM minors: `gfxp_fb_attach()` on the driver's
   node and minor "gfx<instance>" (DDI_NT_DISPLAY) as the first minor =
   the console and /dev/fb; only when the loader left a 32-bit RGB
   framebuffer (UEFI GOP) inside this device's aperture;
2. the console minor's open/close/ioctl/devmap/segmap go to gfx_private,
   every other minor to the DRM core;
3. the KMS side keeps the firmware framebuffer on screen (takeover); the
   gfx_private mode hook (`gfxp_bm_register_fbops`, setmode) records
   KD_TEXT/KD_GRAPHICS, and KD_TEXT (VT switch, X exiting) queues a
   restore of the console configuration;
4. DRM master closed in KD_GRAPHICS (X killed) -> KD_TEXT;
5. CB_CL_PANIC callback points the console CRTC back at the console
   framebuffer;
6. a console device refuses detach.

## For amdgpu

- **illumos side:** share i915n's `*_con.c` rather than copy it. It is
  driver-neutral except for names and the "wanted" test; move it to a
  common place (drm_helpers or drm_core facade) with a small per-driver
  ops table (takeover, restore, panic), and keep i915n on it.
- **amdgpu attach** (our patch series, like 0012 objects with the core):
  call the console attach first when the GOP framebuffer lies in this
  device's VRAM BAR (the loader's fb_info paddr inside BAR0); on the Mac
  Pro that is the display GPU only (two GPUs: the compute GPU gets no
  console).
- **VRAM:** the firmware framebuffer lives in VRAM. amdgpu reserves the
  VBIOS's "stolen" VGA/fb area (gmc_v6_0 `stolen_vga_size`) and frees it
  after init (`amdgpu_bo_free_kernel(&adev->mman.stolen_vga_memory)`
  late in init, as on Linux where fbcon has replaced efifb by then). For
  the console it must stay reserved (or be re-reserved as a pinned BO at
  the GOP fb's offset and size) for as long as gfx_private draws there.
- **KMS side, DCE6 = legacy KMS (no atomic):**
  - takeover: amdgpu must not switch the console CRTC off at init (check
    the legacy helper's disable_unused at load), and creates a DRM
    framebuffer on the pinned firmware-fb BO;
  - restore on KD_TEXT/panic: `drm_crtc_helper_set_config()` (or
    `crtc->funcs->set_config`) of the console CRTC with the console
    framebuffer and the loader's mode; panic path: only the scanout
    address (`dce_v6_0_crtc_do_set_base` / page flip register write),
    no allocation, no locks that X may hold.
- **Xorg:** with VTs back, X runs on a VT (`/dev/vt/N`), Xorg patch 39's
  no-console path is no longer taken, logindevperm works; the Mac Pro
  session removes 30-console-input.conf and checks HAL hotplug (5).

## Test plan

1. Build in illumos-drm (worktree amdgpu/si-bringup), package; test BE
   with one-time activation.
2. Boot: console text on the Dell (boot messages after amdgpu attach), no
   tem error; `/dev/fb` = amdgpu's gfx minor; `/dev/vt/*` exist.
3. Login on the Dell via gdm: `/dev/vt/console_user` owned by the user,
   `/dev/sound/*` and `/dev/dri/*` handed over (logindevperm); PulseAudio
   as the user sees the HDA devices, GNOME Settings sound output.
4. VT switch (Ctrl-Alt-F2 / back), X exit -> console restored; panic test
   only with the user's OK (`reboot -d` gives a dump test too).

## Effort and order

About a day of work plus test boots. Order: (a) read i915_illumos_con*.c
and amdgpu's stolen-VRAM / display init in detail; (b) shared console
module; (c) amdgpu attach + VRAM reservation; (d) legacy KMS takeover and
restore; (e) build, test BE, boot test (user's OK per reboot).
