# Why OpenIndiana (illumos) and Solaris 11 have no AMD Radeon support

Analysis of 2026-11-10 (read-only: the OI VM, the Solaris 11.4
CBE VM, GitHub and upstream sites; nothing built or installed).

## Short answer

- **It is not a hardware secret and not a law of the OS: nobody has ported the
  Linux `radeon`/`amdgpu` kernel drivers (or the layers they need) to illumos.**
  The driver code is open source (radeon, amdgpu and the DRM core are MIT-licensed), and FreeBSD, OpenBSD,
  NetBSD and DragonFly all run it with full acceleration. illumos lacks the
  kernel infrastructure those drivers sit on (a Linux compatibility layer,
  TTM, dma-buf, the modern DRM core), and building that is a large, mostly
  one-person job that nobody had done until 2026.
- **AMD's own drivers *are* the open-source Linux drivers**: amdgpu/radeon
  (kernel, in Linux's `drivers/gpu/drm/`), Mesa radeonsi/r600/RADV (userland),
  xf86-video-amdgpu/-ati (Xorg DDX). AMD ships no closed Solaris/illumos
  driver and never will; the userland half already builds on illumos, the
  kernel half is what is missing.
- **libdrm is not Intel-only.** OI's libdrm 2.4.109 builds and ships
  `libdrm_amdgpu`, `libdrm_radeon`, `libdrm_nouveau`, `libdrm_intel` and the
  ARM ones. They are just useless without a kernel driver answering their
  ioctls. Only the *kernel* side (gfx-drm) is Intel-only (plus a dead 2008
  DRI1 radeon). Solaris 11.4 ships only the core `libdrm.so.2`.
- **nouveau** is the equivalent NVIDIA story (also not ported); on NVIDIA
  hardware both systems instead use NVIDIA's closed driver, which NVIDIA
  itself builds for Solaris/illumos.

## What is on the systems (checked 2026-11-10)

### OpenIndiana Hipster (VM, illumos-c893fd03ad)

| Package | Version | Contents relevant here |
|---|---|---|
| `driver/graphics/drm` | 0.5.11-2025.0.0.95 | `kernel/misc/amd64/drm`, `kernel/drv/amd64/i915` only |
| `driver/graphics/agpgart` | 2025.0.0.95 | `agpgart`, `agptarget` |
| `x11/library/libdrm` | 2.4.109 | `libdrm`, `libdrm_amdgpu`, `libdrm_radeon`, `libdrm_nouveau`, `libdrm_intel`, `libdrm_etnaviv/exynos/freedreno/omap/tegra`, `libkms` (in `/usr/lib/xorg/amd64`) |
| `x11/library/mesa` | 21.3.9 | DRI drivers `i915_dri.so`, `r600_dri.so`, `swrast_dri.so`, `kms_swrast_dri.so` (no radeonsi, no iris, no RADV) |
| `xorg-video-ati` | 6.14.6 (2015) | `radeon_drv.so`/`ati_drv.so`, built without KMS: for any chip newer than R100–R500 the binary says *"GPU only supported with KMS, using vesa instead"* |
| `xorg-video-nv` | 2.1.22 | old unaccelerated `nv` driver |
| NVIDIA | `kernel/drv/amd64/nvidia`, `nvidia_modeset` | NVIDIA's closed driver |

No xf86-video-amdgpu, no xf86-video-nouveau, no kernel radeon/amdgpu/nouveau.

### Solaris 11.4 CBE (VM, 11.4.90.212.0)

- `libdrm` 2.4.120 delivers only `libdrm.so.2` (no `libdrm_radeon`/`_amdgpu`/`_nouveau`/`_intel`).
- Mesa 21.3.8 delivers only `swrast` and `kms_swrast` DRI drivers.
- `driver/graphics/drm` (Oracle's i915 KMS) is in the repository; NVIDIA's
  closed driver is installed (`driver/graphics/nvidia` 470.182.3, plus legacy
  `nvidiaR390`, `nvidiaR304` branches).
- Xorg DDX: ast, mach64, mga, r128, vboxvideo, vesa, vmware. No ati/radeon/amdgpu.

## History: how illumos got the drivers it has

- **Sun era (2007–2010):** OpenSolaris had DRI1 (pre-KMS) DRM with `i915` and
  a **legacy `radeon` for R100–R300** (`radeon_cp.c`, `r300_cmdbuf.c`,
  "Copyright 2008 Sun Microsystems", from the 2000 VA Linux code). That code
  still sits in [gfx-drm `usr/src/uts/intel/io/radeon`](https://github.com/OpenIndiana/gfx-drm/tree/master/usr/src/uts/intel/io/radeon)
  but is not built or packaged and is useless with any Xorg or Mesa of the
  last decade (UMS/DRI1 was removed from Mesa in 2011 and from xf86-video-ati 7.0).
- **Oracle Solaris 11.1–11.3 (2012–2015):** Oracle ported the **Linux i915
  KMS/GEM driver** (Linux 3.x era, hardware up to Haswell; later updates
  added some Broadwell/Skylake IDs) with a hand-written Solaris shim
  (`drm_linux.c`, `drm_sun_idr.c`, `drm_sun_workqueue.c`, `drm_sun_timer.c`,
  ...). Oracle said in 2013 that Radeon KMS might follow but would need TTM
  ported first; it never shipped. Oracle's XDC 2015 talk on the Solaris
  graphics stack:
  [fishel_solaris.pdf](https://wiki.freedesktop.org/xorg/Events/XDC2015/Program/fishel_solaris.pdf);
  coverage: [Phoronix 2013, "Solaris 12 might finally bring Radeon KMS"](https://www.phoronix.com/news/MTI5MTU),
  [Phoronix 2015, Solaris DRM/KMS state](https://www.phoronix.com/news/Solaris-DRM-KMS-2015),
  [Oracle i915(7D)](https://docs.oracle.com/cd/E26502_01/html/E29044/i915-7d.html).
- **illumos gfx-drm (2016–2017):** Martin Bochnig first backported Oracle's
  S12 i915; **Gordon Ross** then created the separate
  [illumos/gfx-drm](https://github.com/illumos/gfx-drm) gate, commit
  2016-10-09 *"Import kernel drm code from hg.java.net/hg/solaris-x11~x-s12-clone"*,
  i.e. Oracle's open-sourced Solaris 12 X11 tree, and fixed it up through
  2017 (libdrm 2.4.75, PPGTT, GEM fixes). After that it only received
  build fixes (Pyhalov, Fiddaman, Larcher, Rosenfeld). OpenIndiana forked it
  as [OpenIndiana/gfx-drm](https://github.com/OpenIndiana/gfx-drm) (64-bit
  only 2024, libdrm 2.4.134 with an illumos device layer 2026-09-30). See [OI docs: graphics stack](https://docs.openindiana.org/dev/graphics-stack).

So illumos's whole kernel graphics support is one 2013-era Linux driver
(i915), carried by hand. Every newer Intel GPU, every AMD and every NVIDIA
(open) needs a newer DRM core than that.

## Why Radeon is much harder than "port one more driver"

Today's `radeon` (R600–GCN1.x) and `amdgpu` (GCN1.2+/RDNA) drivers are
roughly 3–5 million lines (amdgpu with its register headers is the largest
driver in Linux). They are not self-contained. They expect:

1. **The Linux kernel API**: `struct device`, kref, idr/xarray,
   workqueues, `wait_queue`, rcu, `dma_fence`, `seqlock`, hrtimers, PCI and
   `dma_map_*`, `mmu_notifier`, `kthread`, `i2c`, backlight, ACPI/ATIF,
   power management, `debugfs`, `sysfs` ... An OS without that must emulate
   it. This is FreeBSD's **LinuxKPI** (in base since ~2015, grown for
   drm-kmod). Oracle's i915 shim covers only the small subset that 2013 i915
   used.
2. **TTM** (the Translation Table Manager): radeon/amdgpu keep buffers in
   VRAM and move them between VRAM, GTT and system memory; TTM does this
   (eviction, swapping, page pools, caching attributes, fault handlers
   mapping VRAM into user processes). Intel's integrated GPUs live in system
   RAM and used GEM without TTM, which is why Intel was ported first
   everywhere. illumos needs TTM tied to its VM (segment drivers, page
   attributes, `devmap`).
3. **dma-buf / PRIME, dma-fence, sync objects, drm_sched (GPU scheduler)**:
   Mesa radeonsi and RADV need syncobjs, render nodes and dma-buf sharing.
4. **Firmware loading**: every Radeon since R600 needs microcode blobs
   (`radeon/*.bin`, `amdgpu/*.bin` from linux-firmware), loaded with
   `request_firmware()`. illumos has `firmload(9F)`; a port maps one to the
   other and packages the blobs (FreeBSD: `gpu-firmware-kmod`, as kernel
   modules).
5. **Modern DRM core**: atomic KMS, DRM helpers, fbdev/console emulation;
   amdgpu's display code (**DC/DCN**) needs kernel FPU use
   (`kernel_fpu_begin`), which illumos supports differently.
6. **Userland** follows easily once the kernel is there: newer libdrm
   (already 2.4.134 in the pending PR), Mesa with radeonsi/RADV (needs LLVM
   for radeonsi), xf86-video-amdgpu or just Xorg's `modesetting` DDX, and
   GBM/EGL for Wayland.

And it is a moving target: Linux DRM changes every release; FreeBSD needs a
small team (drm-kmod) to follow it, with release branches (5.10, 5.15, 6.1,
6.6 ...) that lag Linux by a year or more.

## How the other non-Linux systems did it

- **FreeBSD**: [drm-kmod](https://github.com/freebsd/drm-kmod) is the Linux
  `drivers/gpu/drm` tree (amdgpu, radeon, i915) built against **LinuxKPI** in
  the FreeBSD kernel, with few source changes; firmware from
  [drm-kmod-firmware](https://github.com/freebsd/drm-kmod-firmware). Ports:
  [graphics/drm-kmod](https://cgit.freebsd.org/ports/plain/graphics/drm-kmod/pkg-descr),
  [FreeBSD Graphics wiki](https://wiki.freebsd.org/Graphics),
  [porting guide](https://github.com/freebsd/drm-kmod/wiki/Porting-a-new-version-of-DRM-drivers-from-Linux).
  Earlier (2014) FreeBSD ported TTM + radeonkms by hand
  ([r262988, TTM MFC](https://lists.freebsd.org/pipermail/svn-src-stable/2014-March/022108.html)),
  then switched to LinuxKPI because hand-porting could not keep up.
- **OpenBSD**: its own compat layer (`sys/dev/pci/drm/include/linux`);
  TTM + radeon from Linux 3.8 in 2013
  ([Phoronix](https://www.phoronix.com/news/MTQzNjI)), amdgpu since 2019.
- **DragonFly BSD**: own compat layer, TTM updated to Linux 4.19 for amdgpu
  ([Phoronix](https://www.phoronix.com/news/AMDGPU-Ported-DragonFlyBSD)).
- **NetBSD**: Linux DRM under `sys/external/bsd/drm2` with a `linux/` shim.

All of them took the same route: a Linux-API compatibility layer in the
kernel, then the unmodified (or lightly patched) Linux driver. None wrote
their own Radeon driver.

## Has anyone started this for illumos? Yes, in 2026

[OpenIndiana/oi-userland#27816](https://github.com/OpenIndiana/oi-userland/pull/27816)
(draft, opened 2026-10-11 by the illumos-drm maintainer) packages
**illumos-drm 0.0.2** (the illumos-drm upstream repository,
MPL-2.0; carried Linux parts MIT):

- a new **DRM core in Rust** (`misc/drm_core`, no_std, on the
  `illumos-ddi` crates) with a C "facade" using Linux's names, PRIME and
  render nodes;
- **`misc/linuxkpi`**: an illumos Linux compatibility layer, MIT, using
  FreeBSD's LinuxKPI as structural reference;
- `drm-helpers` / `drm-kms`: Linux DRM helper code carried for C drivers;
- drivers: a Rust **virtio-gpu** (QEMU/KVM) and **`i915n`**, the i915 from
  **Linux 7.2** (Gen6–Gen9.5; display so far only on Ivy Bridge, tested by
  the author on an X230 with Mesa crocus, GL 4.2, under MATE);
- `drmadm select new|legacy` to switch between it and the old gfx-drm stack;
  installing binds nothing (no PCI aliases yet).
- It depends on #27813 (libdrm 2.4.134), #27814 (xorg-server 21.1.24) and
  #27815 (Mesa 25.3.6).

For AMD the project README says **TTM is milestone "R5d, deferred to
amdgpu"**, firmware is to go through `firmload(9F)`, and dma-buf is part of
the R5 work. So amdgpu/radeon are on the long-term path but no AMD driver
exists yet, not even in a branch. This is the first effort that could
realistically carry radeon/amdgpu to illumos; it is early (0.0.2, one main
author).

## What a Radeon port would take (on top of illumos-drm)

1. Finish the LinuxKPI enough for amdgpu (much larger than i915's needs:
   `mmu_notifier`, `drm_sched`, `dma_fence` chains, ACPI
   ATIF/ATCS, PCIe ASPM/BAR resizing, `kernel_fpu_*` for DC).
2. TTM on illumos VM: VRAM/GTT placement, eviction, write-combined page
   attributes, page faults on VRAM BOs.
3. dma-buf/syncobj/timeline semaphores for Mesa radeonsi and RADV.
4. Firmware: package linux-firmware's `radeon/` and `amdgpu/` blobs
   (redistributable, AMD licence) and map `request_firmware()` to
   `firmload(9F)`.
5. Build `radeon` (HD 2000–HD 7000/R7 2xx, GCN1.0/1.1 by default) and/or
   `amdgpu` (GCN 1.2+, RDNA) from Linux sources, as FreeBSD does.
6. Userland: Mesa with `gallium-drivers=radeonsi,r600` and
   `vulkan-drivers=amd` (needs LLVM), xf86-video-amdgpu (or Xorg
   modesetting), libdrm with `amdgpu`/`radeon` (already built).

Effort: the FreeBSD experience says person-years for the first port, and a
standing team to follow Linux. Testing needs real Radeon hardware in a
machine running illumos (the VM host's VMs can only do virtio/VBoxSVGA; PCI
passthrough of a Radeon to a VM would be one way to test).

## Practical options today

- Intel iGPU up to Haswell (gfx-drm i915, old) — the iMac's Intel graphics
  falls here or under illumos-drm i915n if Gen6–9.5.
- NVIDIA with NVIDIA's closed driver (OI `openindiana/nvidia-*`, Solaris
  `driver/graphics/nvidia` 470.x, with R390/R304 legacy branches).
- Radeon: `vesa` or the illumos framebuffer (`illumosfb`) only, no
  acceleration; Mesa llvmpipe for GL in software.
- In VMs: VBoxSVGA/vmware, or virtio-gpu via illumos-drm under QEMU/KVM.

## Candidate test machine: Mac Pro 2013 (MacPro6,1)

Idea of 2026-11-10 (the user's Mac Pro has two **FirePro D500**, PCI 1002:679e, Tahiti LE,
read from macOS system_profiler on 2026-11-11; first noted here as D700): develop the AMD port on the user's Mac Pro 2013 the way
i915n was brought up on the iMac14,1 (`notes/illumos-drm-haswell/`).

- GPUs: two AMD FirePro D300 (Pitcairn), D500 (Tahiti LE) or D700 (Tahiti XT),
  all **GCN 1.0 / Southern Islands**
  ([AnandTech](https://at-web1.www.anandtech.com/show/7603/mac-pro-review-late-2013/9)).
  Under macOS one GPU drives the displays and the other only computes.
- Driver choice: since **Linux 6.19 amdgpu is the default for GCN 1.0/1.1**
  (Valve's Timur Kristóf: DC display, power management), replacing radeon
  ([Phoronix](https://phoronix.com/news/Linux-6.19-AMDGPU-GCN-1.0-1.1)).
  illumos-drm's base is Linux 7.2, so the port should be **amdgpu**: one driver
  that also covers every newer AMD GPU, and the one illumos-drm's README
  plans TTM for. Linux users already run amdgpu on this Mac
  ([Fedora guide](https://discussion.fedoraproject.org/t/guide-installing-fedora-40-on-mac-pro-2013-and-enabling-amdgpu-for-video-hardware-acceleration/123704)).
- Advantage: two GPUs. Bring-up can start **headless on the second GPU**
  (attach, read the ATOM VBIOS, load SI firmware, ring and IB tests, then
  TTM/GEM buffers and radeonsi through GBM), while the console stays on the
  first GPU (EFI framebuffer), like the iMac's headless execbuffer phase.
- Userland on OI: libdrm 2.4.134 already has amdgpu; Mesa needs radeonsi
  (and LLVM) and, for Vulkan, RADV.
- Unknowns to check by booting OI there first (live USB, nothing installed):
  does illumos boot (Xeon E5 v2, Apple EFI), do the two Broadcom BCM57762
  Ethernet ports work with `bge`, does the PCIe SSD attach via `ahci`/`nvme`,
  can crash dumps be taken; audio is an Apple/Cirrus HDA codec, relevant to
  `ports/audiohd-apple`. Capture `prtconf -v`, `scanpci`, `dmesg`.
- Size: much more than the iMac work. There the illumos-drm maintainer had already ported
  i915n and we fixed bugs; here the port itself (amdgpu + TTM + the missing
  linuxkpi parts) is the work. Coordinate with the illumos-drm maintainer first so TTM (R5d)
  is not written twice.
