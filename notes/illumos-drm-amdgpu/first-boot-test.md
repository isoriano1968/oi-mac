# amdgpu: plan for the first test on the Mac Pro (2026-11-11)

The goal of the first test is narrow: amdgpu attaches to the **compute
GPU only** (02:00.0, FirePro D500, subsystem 106b:0126, no outputs),
brings up its rings, passes the ring and IB tests, and detaches again
cleanly. The display GPU (06:00.0, 106b:0125) stays on `vgatext` and the
EFI framebuffer throughout, so the console survives whatever happens.
Nothing here is done yet; every step on the Mac needs the user's go-ahead.

## What we know

- OI boots on the Mac Pro from the Verbatim stick (BE `usbroot`, host
  the Mac Pro, illumos c893fd03ad plus our hubd fix); `bge1` gets a link;
  a dump device exists (`/dev/zvol/dsk/rpool/dump`, 2 GB). Both GPUs show up
  as display nodes on `vgatext` (`/pci@0,0/pci8086,e04@2/display@0` is one;
  the other's path is still to be read). `../macpro-2013/README.md`.
- The VBIOS of both GPUs is in ACPI VFCT, one image per bus/device/function
  (patch 0008 matches the exact bus first).
- Firmware amdgpu wants for Tahiti (`MODULE_FIRMWARE`): `amdgpu/tahiti_ce.bin`,
  `_mc`, `_me`, `_pfp`, `_rlc`, `_smc`. LinuxKPI's `request_firmware()` uses
  `firmload(9F)`: `amdgpu/x.bin` is `/kernel/firmware/amdgpu/x.bin`.
- SI is on by default in our build (`CONFIG_DRM_AMDGPU_SI`, no radeon,
  `amdgpu_si_support = -1`). No DC (`CONFIG_DRM_AMD_DC` off): the legacy
  DCE 6 code would drive displays, and the compute GPU has none.
- Module parameters are compiled-in defaults (`linux/moduleparam.h`): to
  change one before attach, `modload` the module (no binding, so no attach)
  and set the variable with `mdb -kw`.
- illumos binds by `compatible`, which includes the subsystem form
  `pci1002,679e.106b.126`: binding amdgpu to that name takes the compute GPU
  and leaves the display GPU (`.106b.125`) alone. To confirm on the box.

## Stage 0: build and package (OI VM, no Mac needed)

1. Build the branch's modules: `drm_core` (our core changes: rounds 25,
   28), `linuxkpi`, `drm_helpers`, plus whatever `amdgpu`'s link names with
   `-N` (read it from `build.sh`), and `amdgpu`.
2. Packages, as the i915n ones in `packaging/`: a new `amdgpu.p5m` with **no
   `driver` action** for this test (no binding at install, so a boot never
   attaches it); `drm-core`, `linuxkpi`, `drm-helpers` from the branch; a
   licence file from `gen-license.sh` over amdgpu's `linux-inputs.txt`.
   One p5p, e.g. `illumos-drm-0.0.2-amdgpu.p5p`.
3. Firmware: the six `tahiti_*.bin` from linux-firmware (`amdgpu/`), with
   linux-firmware's AMD licence, as a package of their own (like
   `firmware/i915`). **Needs a download: the user's yes, with file names,
   source and size first.**
4. A small checklist script for the Mac (read-only first, then the test
   steps), so each run is the same.

### Stage 0 done (2026-11-11)

- `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p` on the OI VM (5.7 MB): amdgpu,
  drm-core, linuxkpi, drm-helpers, drm-kms, drmadm, all 0.0.2, built from
  the worktree by `~/src/drm/package-amdgpu.sh` (VM, outside the repo, as
  `package.sh` is for i915n). drm_kms is built from the i915 tree
  (`~/src/drm/linux`), as i915n's package does: against linux-amdgpu its
  licence check refuses `drm_trace.h` (GPL; that tree does not replace it).
- `packaging/amdgpu.p5m` (worktree): the module, a `driver` action with
  **no aliases** (installing binds nothing), the driver licence from
  `gen-license.sh` (115 copyright lines), the firmware with its own licence.
- Firmware, no download needed: the Linux workstation has Debian's
  firmware-amd-graphics 20250410-2; `tahiti_{ce,me,pfp,rlc,mc,smc,uvd}.bin`
  copied with symlinks resolved, the licence text from Debian's copyright
  file. In the worktree's `firmware/amdgpu/` and `firmware/LICENSE.amdgpu`,
  checksums in `firmware/README.md`. No `tahiti_vce.bin` in that snapshot.
  Binaries: not in git, not in the backup patch.
- Stage 1's checks: `macpro-stage1-check.sh` (read-only; dry run on the OI
  VM: its display node parses; illumos builds `compatible` entries as
  `pciVVVV,DDDD.SSSS.ssss`, so `pci1002,679e.106b.126` should exist on the
  Mac).

## Stage 1: look at the Mac (read-only)

Over ssh if the key is installed on the Mac Pro, otherwise the user runs it:

- `prtconf -v` for both GPU nodes: path, `compatible` (is
  `pci1002,679e.106b.126` there?), `reg`, `assigned-addresses`, `interrupts`.
- `modinfo | grep -i -E "drm|vgatext|agp"` (gfx-drm's `misc/drm` must not
  be loaded: `drm_core` refuses to load beside it), `dumpadm`, free space
  for crash dumps (`df -h /var/crash`), `beadm list`.
- Whether the intel IOMMU (`immu`) is on (DMAR is in the ACPI tables).

### Stage 1 results (2026-11-11 21:17, over ssh)

Raw output: `../macpro-2013/stage1-2026-11-11.txt`. The Mac ran BE `x11-fb`
(another session's console/X work, kernel `console-fb-0-g8f5f62e4-dirty`),
up 19 minutes; BEs: openindiana, usbroot, usbroot2, consolefb, x11-fb.

- Display GPU: `/pci@0,0/pci8086,e08@3/display@0` on vgatext, compatible
  `pciex1002,679e.106b.125`, `pci1002,679e.106b.125` (both forms, `pciex`
  first), BAR0 0x90000000 (256 MB), MMIO 0xa0600000, I/O 0x2000; its HDMI
  audio 1002:aaa0 at function 1.
- **Compute GPU: not in the device tree.** Root port
  `/pci@0,0/pci8086,e04@2` (pcieb1, bus-range 2-2, link x16 at 8 GT/s,
  slot 3, hotplug-capable) has no child, neither in `prtconf` nor in the
  kernel's own tree (`::prtconf`); `scanpci` lists only bus 6. At boot
  (20:59:08) `/pci@0,0/pci8086,e04@2/display@0 (vgatext2) offline` was
  logged, so the node existed and was removed afterwards. The stock
  kernel's boot (`macpro-boot-messages-usbroot.txt`, 19:58) logs the same
  vgatext offline line. Cause (from the Mac Pro bring-up session,
  2026-11-11): vgatext binds both GPUs through `pciclass,0300`, its attach
  fails on the GPU that is not the console, and that node is offlined, at
  every boot and on the stock kernel too. An `amdgpu` alias for
  `pci1002,679e.106b.126` is more specific than `pciclass,0300`, so with it
  amdgpu, not vgatext, gets that node at boot.
- The Mac is shared: the other session's BE `x11-fb` carries its illumos
  fixes (usba hubdi, consconfig_dacf, gfx_private, unix
  `plat_dev_owns_boot_fb()`) and Xorg with illumosfb; the user tests
  `startx` there. No reboot or install on the Mac from this session without
  the user's go-ahead and without checking that the other work is not
  using it.
- gfx-drm's `misc/drm` not loaded (vgatext and agpgart only); no 1002
  aliases except amd_iommu's `pci1002,5a23`.
- Crash dumps: dedicated 2 GB zvol, savecore on, 17.5 GB free on /var.
  rpool 28.5 GB, 20.6 GB free. IOMMU off (`immu_enable` 0).
- Network: bge1 DHCP (100 Mb/s this boot).

Consequence for Stage 4: a runtime bind (`update_drv -a -i`) finds no node
after boot. Either amdgpu is aliased in the test BE so it binds the node
at boot, before whatever removes it (then the cautious settings go into
the test BE's `/etc/system` as `set amdgpu:amdgpu_dpm = 0` and so on, not
mdb), or first find out what removes the node (a boot with `-v` and the
`ndi`/devinfo trace, or dtrace on `ddi_remove_child`/`ndi_devi_free` in a
boot of the test BE). Both need the user's choice.

### Stage 2 done, first boot of `amdgpu1` (2026-11-11 21:26-21:40)

- BE `amdgpu1` from `consolefb`; `pkg -R` installed the six packages from
  the p5p (sha256 97f1e067...; deleted from /var/tmp afterwards); aliases
  `amdgpu "pciex1002,679e.106b.126"` and `"pci1002,679e.106b.126"` (vgatext's
  aliases are `pciclass,...` forms only, later in the compatible list);
  `/etc/system` settings `amdgpu_dpm`, `_runtime_pm`, `_aspm`, `_audio` 0
  and `amdgpu_ip_block_mask` 0xfffffe7f (Tahiti blocks: 0 common, 1 GMC,
  2 IH, 3 GFX, 4 SDMA, 5 SMU, 6 DCE, 7 UVD, 8 VCE). `consolefb` already had
  `set halt_on_panic = 1`. `beadm activate -t amdgpu1`, reboot 21:30;
  up again 21:32 in `amdgpu1` (x11-fb stays the default).
- Result: `/pci@0,0/pci8086,e04@2/display@0 (amdgpu0) offline` at 21:31:44,
  the same second as `pcieb1` attaches; nothing from amdgpu, the core or
  LinuxKPI. Logs: `../macpro-2013/amdgpu1-boot1-messages.txt`,
  `amdgpu1-boot1-msgbuf.txt`.
- `modload -p drv/amdgpu` works (amdgpu, drm_core, linuxkpi, drm_helpers,
  drm_kms load; the `/etc/system` values are in effect). `devfsadm -i
  amdgpu` fails ("driver failed to attach"), and DTrace on every amdgpu and
  linuxkpi function shows **no call at all**: amdgpu's attach is never
  reached. The node is gone from the kernel tree again.
- **Cause: PCIe native hotplug.** `pcieb1` (root port 00:02.0) runs in
  `PCIE_NATIVE_HP_MODE`; its slot `pcie3` is in `DDI_HP_CN_STATE_PRESENT`
  (occupied, not enabled), so the hotplug framework does not keep the
  child configured: it is offlined and removed at boot, whatever driver is
  bound (vgatext before, amdgpu now). The GPU itself is fine (link x16 at
  8 GT/s). Linux has the device at 02:00.0 normally.
- Next: enable the slot (`svcadm enable hotplug`, then
  `hotplug enable /pci@0,0/pci8086,e04@2 pcie3`), which configures the
  node and attaches amdgpu; or turn native hotplug off for that port.

### First attach of amdgpu (2026-11-11 21:37, slot enabled by hand)

In `amdgpu1`: `svcadm enable hotplug` (stays enabled in that BE), then
`hotplug enable /pci@0,0/pci8086,e04@2 pcie3` (rc 0). Log:
`../macpro-2013/amdgpu1-slot-enable.txt` (from the BE's messages).

- **amdgpu's device init worked**, in about one second:
  `initializing kernel modesetting (TAHITI 0x1002:0x679E 0x106B:0x0126)`,
  MMIO 0xA0700000 (256 KB), the nine IP blocks (UVD and VCE disabled as
  set), `Fetched VBIOS from VFCT`, `ATOM BIOS: 113-C3861LA-029, build:
  357431, ver 015.048.000.070.000000, 2016/08/26`, TMZ and PCIe atomics not
  supported, VM 512 GB 2 levels, VRAM 3072M at 0xF400000000, GART 1024M,
  `Detected VRAM RAM=3072M, BAR=256M`, `RAM width 384bits GDDR5`, 65517M of
  GTT, `PCIE GART of 1024M enabled`, six DP connectors with DDC and
  encoders from the VBIOS (UNIPHY/1/2, as Linux lists for the display GPU),
  `SE 2, SH per SE 2, CU per SH 8, active_cu_number 24`. Warnings: `Failed
  to store PCI saved state`; runtime PM off as set. Then
  `/pci@0,0/pci8086,e04@2/display@0 (amdgpu0) online`.
- Then: `WARNING: (pcieb1): failed to attach driver for a device
  (pci106b,126-1) under the Connection pcie3` (function 1, the HDMI audio,
  has no driver), `amdgpu0: finishing device.` (amdgpu's fini), and the
  **Mac switched itself off** (no panic, no dump; the monitor had no
  signal; the user powered it on, it came up in `x11-fb`).
- **Why the slot is "PRESENT" (root cause):** the firmware grants the OS
  native hotplug through `_OSC` (`pcie_x86_priv_t`: `bus_osc_hp` true), the
  slot has a power controller (`hc_has_pwr`), attention button, and Data
  Link Layer Link Active reporting (`hc_dll_active_rep`), and Apple leaves
  the Slot Control *Power Controller Control* bit set ("off") although the
  GPU is powered and its link is up. `pciehpc_get_slot_state()`
  (`uts/common/io/pciex/hotplug/pciehpc.c`) then says PRESENT, and the
  child is removed at boot. When the framework later took the slot back
  down (after function 1 failed) it really switched the power controller
  off, and the Mac powered off (presumably its firmware reacting to the GPU
  losing power: a guess).
- **Fix idea (illumos):** in `pciehpc_get_slot_state()`, if the slot has
  Link Active reporting and the link is active, treat it as powered whatever
  the power-control bit says (and do not turn power off for an occupied,
  link-active slot at init). With that the node stays at boot and amdgpu
  binds through the alias, without the hotplug path. No tunable exists
  (`pcie_auto_online` only chooses PRESENT or ENABLED as the target). This
  belongs with the Mac Pro bring-up session's illumos fixes (USB root,
  console): their build tree is `~/src/illumos-usbroot` on the build VM.
- First fix by the Mac Pro session (BE `amdgpu1-pciehp`, pcie module with
  `pciehpc_get_slot_state()` treating link-active as powered): that boot
  hung before networking, the console repeating hundreds of
  `pciehpc_slot_poweroff (slot 3): SW state is 12288 but power controller is
  already disabled; continuing`. The user power-cycled to x11-fb.
- **What took the slot down at 21:37** (from the source; to confirm with the
  slot kstat `pcie:*:pcie3`, counters pwrflt/attnsw/pres_chg/dll_chg): most
  likely a **power fault**. `pciehpc_power_fault_handler()` requests
  `DDI_HP_CN_STATE_PRESENT` (power off) and logs nothing. Function 1's
  failure is ignored (`(void)` at `ddi_hp_impl.c:974`); a removal would log
  "card is removed"; the attention button waits 5 s and only posts a
  sysevent. The boot storm fits a stuck Power Fault Detected bit. Suggested
  to the Mac Pro session: for this quirk treat the slot as having no power
  controller (`hc_has_pwr = B_FALSE`, power-fault interrupt off), rather
  than writing power on.
- Confirmed on the hardware (x11-fb, stock pcie module): kstat
  `pcie:1:pcie3` interrupt_count 6, power_fault_count 6, everything else 0.
  Second fix (Mac Pro session, commit 4, BE `pciehp-test` = x11-fb clone
  without amdgpu): `pciehpc_slotinfo_init()` sets `hc_has_pwr = B_FALSE` for
  the quirk (power bit "off", presence, DLL Link Active), so the slot reads
  POWERED, the child makes it ENABLED and `slot_poweroff` is ENOTSUP;
  `pciehpc_enable_intr()` keeps PWR_FAULT_EN off without a power controller
  (and clears one firmware left set). Plan: boot `pciehp-test` alone (node
  stays, slot ENABLED, no power faults acted on), then the same module in
  `amdgpu1-pciehp` for amdgpu's boot-time attach.
- Step 1 with fix v2 (`pciehp-test`): no change (power_fault_count 6,
  node offlined, `hc_has_pwr` still true, no CE_NOTE): at
  `slotinfo_init()` DLL Link Active already read 0, the slot really was
  unpowered. Suspect (from the source, to confirm with DTrace):
  `pciehpc_hpc_init()` runs before `slotinfo_init()` and writes Slot
  Control back with the power bit still "off"; if Apple's controller acts
  on every write, that first write cuts the GPU's power (it was enumerated
  with the link up earlier in the same boot). The quirk check has to be in
  `pciehpc_hpc_init()`, before that write, clearing the power bit in the
  written value (= "on", as the hardware really is).
- **Actual root cause** (Mac Pro session, from our Linux live boot,
  `../macpro-2013/linux-lspci-nnvvk-lmde7-live.txt`, checked): under Linux
  00:02.0 slot #3 has SltCtl `Control: ... Power-` (power controller ON),
  SltSta `PowerFlt+ PresDet+` (Power Fault Detected **stuck** set),
  `Enable: ... PwrFlt-` (Linux leaves the power fault interrupt off), link
  8 GT/s x16; slot #5 (display GPU) has `PowerFlt-`. So Apple leaves slot 3
  powered; illumos enables PWR_FAULT_EN, the interrupt fires at once, and the
  power fault handler powers the slot off: the GPU is removed at every boot,
  and the "power bit off" seen afterwards was illumos' own write. The
  "firmware leaves the bit off" premise above (and my `hpc_init` write
  suspect) were wrong. 21:37 fits: `hotplug enable` powered the slot on, the
  stuck fault fired again, slot off, Mac off.
- Fix v3 (commit 4, replaces v1/v2, module sha256 94c2fa02...):
  `pciehpc_pwr_fault_stuck()` clears the RW1C Power Fault Detected bit and
  reads it again; if it stays set, `pciehpc_enable_intr()` and
  `pciehpc_slot_poweron()` do not enable PWR_FAULT_EN (logged with CE_NOTE).
  In BE `pciehp-test` (no amdgpu) for step 1; `amdgpu1-pciehp` gets it after.
### Step 2: amdgpu attaches at boot (BE `amdgpu1-pciehp`, pcie v3, 22:26)

Log: `../macpro-2013/amdgpu1-pciehp-boot1.txt` (the BE's messages).

- `pcieb1: slot 3: power fault detected cannot be cleared; not enabling
  power fault detection`; amdgpu0 attaches at boot (22:27:27) and its init
  runs to the end, as at 21:37: VFCT VBIOS, ATOM, 3072M VRAM, GART, six
  connectors, 24 CUs, `Failed to store PCI saved state`, runtime PM off.
- **8 s later, 22:27:35: `amdgpu0: finishing device.`** with no error in
  between, then the Mac switched off (dark screen; powered on by the user,
  up in x11-fb 22:30; no dump).
- So amdgpu is **detached after a successful attach**: illumos detaches
  idle driver instances during boot unless they have `ddi-no-autodetach`,
  and nothing opens the compute GPU. Neither i915n nor amdgpu sets it.
  This also means 21:37 may have been the same teardown rather than (only)
  the power fault.
- The teardown powers the Mac off. LinuxKPI's `pci_set_power_state()` is a
  no-op (stays in D0), so not a D3 transition. Unknown what in amdgpu's
  fini does it; one guess: stopping the GPU's SMC (its thermal reporting)
  makes Apple's SMC switch off. To find out later: a verbose boot filmed,
  or tracing fini step by step.
- Next test: `ddi-no-autodetach=1;` in `/kernel/drv/amdgpu.conf` of
  `amdgpu1-pciehp` (no code change), boot once: does the Mac stay up with
  amdgpu attached? (Then the package ships it, or the glue sets the
  property in attach.)
### Step 3: amdgpu stays attached (ddi-no-autodetach, 22:38)

`/kernel/drv/amdgpu.conf` with `ddi-no-autodetach=1;` in
`amdgpu1-pciehp` (the user's yes in the amdgpu session), one boot. Log and
checks: `../macpro-2013/amdgpu1-pciehp-boot2.txt`.

- **The Mac stays up with amdgpu attached.** Same init as step 2, done at
  22:39:19; no "finishing device". amdgpu, drm_core, linuxkpi,
  drm_helpers, drm_kms loaded; `display, instance #0 (driver name:
  amdgpu)` under pcieb1, `pci106b,126` (HDMI audio) without a driver.
  kstat `pcie:1:pcie3`: every counter 0 (amdgpu's init does not touch the
  link). So the auto-detach caused step 2's teardown, and the teardown
  powers the Mac off (to be found before any detach test).
- Minor nodes: `display@0:card0` (285,0) and `display@0:renderD128`
  (285,128), `crw-rw-rw-`, node type `ddi_display:drm` (`::minornodes`).
- **No `/dev/dri`**: `devfsadm -i amdgpu` returns 0 and makes no links.
  The Mac has no gfx-drm `SUNW_drm_link` module; the package's
  `devlink.tab` rule `type=ddi_display:drm;name=amdgpu\tdri/\M0` (copied
  from i915n's) probably does not match because `name=` is the node name
  (`display`), not the driver. Not confirmed. Fix: gfx-drm's drm-devlink
  package, or a rule that matches.
- Packaging follow-ups: ship `amdgpu.conf` with `ddi-no-autodetach=1` (or
  set the property in the glue's attach), and a working devlink rule.
- Not yet run: anything from user space (no libdrm on the Mac), ring/IB
  tests (amdgpu prints only failures; none printed), detach (powers the
  Mac off).
### After step 3: packaging fixes and KMS (2026-11-11, VM only)

Goal from the user: GNOME 3 on the Mac's monitor with the AMD GPUs doing
the work and their power management visible.

- `devlink.tab`'s `name=` is the device node's name (`devfsadm.c:6613`,
  `di_node_name()`), "display" for a VGA-class GPU: the package's rule is
  now `type=ddi_display:drm;name=display\tdri/\M0` (i915n's package has
  the same bug, hidden on the iMac by gfx-drm's `SUNW_drm_link`).
- `packaging/amdgpu.conf` (`ddi-no-autodetach=1`, `preserve=true`) ships
  in the package.
- Patch 0014: `amdgpu_kms_driver.illumos_lkms = &drm_kms_illumos_funcs`
  (misc/drm_kms behind the core, as i915n's patch 0012); amdgpu's own
  mode-setting code (dce_v6_0, no DC) already ran at probe. 407/407, 643
  warnings, links (`drm_kms_illumos_funcs` resolved through drm_kms'
  mapfile). The module calls itself "AMD GPU (Linux amdgpu)" now.
- Order agreed with the Mac Pro session: KMS on the compute GPU first (no
  wired connectors, no console), then the display GPU with console
  takeover (gfx_private, after i915n's `i915_illumos_con*.c`; amdgpu
  reprograms the MC, so the firmware framebuffer at BAR0 0x90180000 needs
  care), then `amdgpu_dpm` back to the default. The Mac Pro session puts
  libdrm 2.4.134 and Mesa 26.2.6 (radeonsi) and GNOME on the Mac.
- Teardown power-off (later): LinuxKPI's `pci_disable_device()` only clears
  Bus Master and `pci_set_power_state()` is a no-op, so it is in amdgpu's
  `hw_fini` steps. Test: a detach with DRM debug output on and the console
  filmed; the last line before the power-off names the step.

### First user space: panic at open (BE `amd-mesa26`, 2026-11-11 22:59)

The Mac Pro session's BE (amdgpu as in `amdgpu1-pciehp`, headless, plus
libdrm 2.4.134, Mesa 26.2.6, gfx-drm's drm-devlink). Log (from the BE):
`../macpro-2013/amd-mesa26-boot1.txt`.

- `/dev/dri/card0` and `renderD128` appeared at boot (SUNW_drm_link), both
  to `/devices/pci@0,0/pci8086,e04@2/display@0`, 0666.
- `AMDTEST_NO_EGL=1 ./amdtest` (open renderD128, drmGetVersion,
  amdgpu_device_initialize, queries; no rendering): **panic** ("core dump"
  on the monitor), no dump saved (x11-fb's /var/crash empty, no savecore
  line; possibly not dumped to the end before the power cycle).
- Last log line: 23:00:06 `[drm] Please enable CONFIG_MTRR and
  CONFIG_X86_PAT for better performance thanks to write-combining`,
  printed once by `amdgpu_bo_support_uswc()` at the first USWC object.
  So the panic is in the open: `amdgpu_driver_open_kms()` ->
  `amdgpu_vm_init()` (root page-table BOs, then most likely the first SDMA
  job to clear them: scheduler, ring, fences, IH; all running for the
  first time). Needs the panic string and stack (photo) or a dump.
- Retry with `halt_on_panic = 0` and a live DTrace stream to the Linux workstation
  (`../macpro-2013/amdtest-trace-0019.txt`, every entry in amdgpu,
  linuxkpi, drm_core, drm_helpers; negative-errno returns): the last lines,
  cpu 8 in amdtest, are `amdgpu_vm_pt_create` -> `amdgpu_bo_create_vm` ->
  `amdgpu_bo_create` -> `amdgpu_bo_validate_size`, `lkpi_kmalloc`,
  `drm_gem_private_object_init` (core `GemCore::create`), 
  `amdgpu_bo_support_uswc`, `__drm_illumos_printk`, `lkpi_drm_vprintk`.
  The next traceable calls (`amdgpu_bo_placement_from_domain`,
  `ttm_bo_init_reserved`) do not appear; the stream may have lost its last
  ~10 ms. The Mac rebooted by itself after ~70 s; no dump header written.
- Reading: most likely a panic whose dump fails. rpool, and with it the
  dump zvol, is on the USB stick, and illumos (as far as I know; not
  verified) cannot write crash dumps to USB mass storage (no polled I/O).
  Then no panic on this Mac leaves a dump. Next run: `halt_on_panic = 1`
  and one photo of the panic screen, with the same trace.
### Third amdtest run: queries work, panic on close (2026-11-12 00:00)

`amd-mesa26`, `halt_on_panic = 1`, trace `../macpro-2013/amdtest-trace-0030.txt`
(run by the Mac Pro session, the user's yes there).

- Open and every query worked: `drm: driver amdgpu 3.64.0 (AMD GPU)`,
  marketing name "AMD Radeon Graphics" (no `/usr/share/libdrm/amdgpu.ids`),
  asic_id 0x679e family 110 rev 1, 24 CUs, 2 SEs, max engine clock
  450 MHz, max memory clock 150 MHz (dpm off: boot clocks), VRAM 3061 MB
  (253 MB visible), GTT 65512 MB.
- Panic on close (photo):
  `recursive mutex_enter ... linuxkpi:xa_find+20 <- amdgpu:amdgpu_userq_mgr_fini+6b
  <- amdgpu:amdgpu_driver_postclose_kms+296 <- drm_core: CharDevice close <-
  drm_close <- spec_close <- close`, and "dump succeeded": **dumps to the
  USB zvol work** (my "no USB dumps" guess was wrong; the two earlier runs
  ended without a dump for another reason, not known).
- Cause: `amdgpu_userq_mgr_fini()` calls `xa_find()` and `__xa_erase()`
  inside `xa_lock()`, which Linux allows (its readers are RCU-only); our
  `xa_find()` took xa_lock again. Fix (linuxkpi `lkpi_xarray.c`): the
  readers (`xa_load`, `xa_find`, `xa_find_after`, `xa_get_mark`,
  `xa_extract`) take the lock only if the calling thread does not hold it
  (`MUTEX_HELD`, the lock is an adaptive kmutex). Host test
  `xarray_read_under_lock` (the userq pattern). Host tests 44/44, 1521
  checks; linuxkpi 42/42; amdgpu and i915n link.
- p5p `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p` sha256 675ee2ad...: the
  xarray fix, KMS (patch 0014), amdgpu.conf, the devlink rule.
- The user went to bed and gave the Mac Pro session the approval for
  installs and test boots tonight; that session installs and tests, this
  one builds and analyses.

- Correction (Mac Pro session): after the photo panic the forced
  savecore found only the old 963 MB live dump again, although the screen
  said "dump succeeded": dumps to the zvol on the USB stick are not
  reliable. Photos and the live trace stay the record.
- The fixed p5p is in BE `amd-xafix` (clone of `amd-mesa26`), installed by
  the Mac Pro session, booted after its GNOME BE's OS update.

### The way to GNOME (plan, 2026-11-12 night)

After the three amdtest steps (open/queries/close, EGL clear+readback,
more radeonsi offscreen):

- d. **dpm on the compute GPU**: drop `set amdgpu:amdgpu_dpm = 0` (keep
  UVD/VCE masked): si_dpm with `tahiti_smc.bin`; amdtest's max engine and
  memory clocks should rise from the boot values 450/150 MHz. Without it
  there is no "fast acceleration".
- e. **The display GPU on amdgpu without console takeover code**: aliases
  `pciex1002,679e.106b.125` and `pci1002,679e.106b.125` for amdgpu. Then
  vgatext does not attach to e08@3 and nothing draws into the firmware
  framebuffer any more (no VRAM conflict with amdgpu's allocator). Cost:
  consconfig finds no console driver for the screen device, so the kernel
  console leaves the monitor after boot (ssh for everything). amdgpu1:
  card1/renderD129. Then Xorg's modesetting driver with glamor (radeonsi)
  on card1, then GNOME. The proper console takeover (gfx_private, after
  i915n's `i915_illumos_con*.c`) comes later.
- Test a in `amd-xafix` (00:13, the xarray fix): the Mac stopped
  answering within seconds and sits (halt_on_panic 1) on a presumed panic
  until the user is up; the trace was written on the Mac, not streamed, so
  nothing is known yet. Runs 1-3 were probably all the close panic (open
  and the queries take milliseconds; the streams lost the end). Statically
  checked tonight: every `xa_lock` section in amdgpu calls only readers or
  `__xa_*` inside (covered by the fix); dma_resv readers with the
  reservation held are safe (the core's `Resv` keeps its fences under a
  separate inner lock). So the next failure is most likely later in
  `amdgpu_driver_postclose_kms()` (evf_mgr_fini, vm_fini, ctx_mgr_fini).
- Kernel versions: the Mac Pro session's GNOME BE has illumos 586678ac1b
  (osnet 23395); our modules are built against c893fd03ad. Between them 51
  commits and no change to the headers the modules depend on (file, vnode,
  user, proc, DDI, devmap, seg_dev, poll, specfs, fio, umem, firmload):
  no rebuild expected.
- Never detach amdgpu (the teardown powers the Mac off): amdgpu.conf's
  `ddi-no-autodetach` and no `rem_drv`/`modunload`.

### The Mac moved to the SSD (2026-11-12 afternoon)

From `../macpro-2013/README.md` ("SSD system"): the Mac now boots from a
SanDisk Portable SSD 1 TB on USB (blessed from macOS). Default BE
`macpro-full` = a zfs send of the stick's `gnome-llvm` (GNOME 45, osnet
23395 = illumos 586678ac1b, Mesa 26.2.6 llvmpipe, libdrm 2.4.134,
drm-devlink 98) plus MATE, build-essential, vlc, the games, fs-uae and
driver/graphics/drm 98. No amdgpu in it. Dump device on swap on the SSD.

What this means for the amdgpu tests:
- The stick BEs (`amdgpu1-pciehp`, `amd-mesa26`, `amd-xafix`) stay on the
  stick; the next tests go into a clone of `macpro-full` on the SSD:
  `beadm create -e macpro-full amdgpu-ssd`, then in it the p5p
  `illumos-drm-0.0.2-amdgpu.p5p` (sha256 675ee2ad...), the pcie v3 module
  (94c2fa02...), the amdgpu aliases for `.106b.126` only, and the /etc/system
  lines (amdgpu_dpm=0, runtime_pm=0, aspm=0, audio=0, ip_block_mask
  0xfffffe7f, halt_on_panic). drm-devlink is already there.
- Check first that `macpro-full` has the same kernel (586678ac1b): then no
  rebuild (see above). driver/graphics/drm 98 (gfx-drm's i915) is installed
  there; it must not claim the AMD GPUs (it binds Intel IDs only) and does
  not conflict with our drm_core path, to be checked in the clone
  (`pkg contents driver/graphics/drm`, `/etc/driver_aliases`).
- Dumps: the dump device is a zvol on the SSD's swap; with halt_on_panic 0
  in the test BE a panic now writes a crash dump to read with mdb (better
  than a photo), still on USB, so check `dumpadm` and test once.
- The test-a photo from `amd-xafix` (stick) is still the first input, if
  the user took one; otherwise rerun test a in `amdgpu-ssd`, trace streamed
  to the Linux workstation.

#### Checked on the Mac (2026-11-12 16:11 Mac time, read-only ssh)

- `illumos-586678ac1b-macpro`, osnet 23395 (20261110T223116Z), up 48 min.
  BEs: `macpro-full` NR (11.8 G), `macpro` (2.4 M), `openindiana` (the
  failed gate build, 7.2 G, to delete). rpool = the SanDisk SSD (c3t0d0,
  xhci storage@1), 928 G, 907 G free, healthy. The internal Crucial P1 NVMe
  (c6t00A07520E4CB500Cd0, macOS) is visible; leave it alone.
- The user is logged in to GNOME on :0 (gdm, Xorg illumosfb) with vlc
  running: no reboots or heavy work without asking.
- **Both GPUs attached to vgatext** (display e04 instance 2 = compute GPU,
  e08 instance 1 = display GPU): the pcie fault fix is in this BE
  (`/platform/i86pc/kernel/misc/amd64/pcie` sha256 e22533a0..., the
  586678ac1b build). No amdgpu, linuxkpi or drm_core; no amdgpu aliases;
  /etc/system only `halt_on_panic = 0`; no /dev/dri.
- **Dumps are off**: `dumpadm` says "Dump device: none (dumps disabled)",
  although rpool/swap (8.5 G) exists. Needs `dumpadm -d swap` (root, a
  system setting: ask).
- Packages: libdrm 2.4.134 + drm-devlink 98 + driver/graphics/drm 98
  (gfx-drm, delivers `kernel/misc/amd64/drm` + i915; no file overlap with
  ours: drm_core, drm_helpers, drm_kms, linuxkpi, amdgpu, drmadm), Mesa
  26.2.6. Our p5p has no aliases in its driver action (the `.106b.126`
  alias stays a test-BE setting) and ships an SMF manifest
  (drm-perms.xml).
- Xorg log: "AIGLX error: swrast exports no extensions (__driDriverExtensions)",
  "GLX: could not load software renderer": X's own GLX with Mesa 26 fails
  (Mesa 26 has only libdril_dri.so); GNOME's clients render with llvmpipe
  themselves. To fix with glamor/radeonsi later (Xorg's GLX loader vs
  Mesa 26's dril).

#### Plan (next steps, in order)

1. **Prepare on the Mac, no reboot** (root, the user's yes): `dumpadm -d
   /dev/zvol/dsk/rpool/swap`; `beadm create -e macpro-full amdgpu-ssd`;
   mount it; `pkg -R` install the p5p (675ee2ad...); in the clone: aliases
   `amdgpu "pciex1002,679e.106b.126"` + `"pci1002,679e.106b.126"`,
   /etc/system amdgpu_dpm=0, runtime_pm=0, aspm=0, audio=0,
   ip_block_mask=0xfffffe7f, halt_on_panic=0 (so the dump is written and
   the Mac reboots); bootadm update-archive -R. Copy amdtest to the Mac.
   Default BE stays `macpro-full`; boot the clone once with
   `beadm activate -t amdgpu-ssd` (as for `amdgpu1`).

   **Done 2026-11-12 17:55-18:05 (Mac time), the user's yes:**
   - The swap zvol cannot be the dump device ("already in use"): new
     `rpool/dump` (8 G, volblocksize 128k), `dumpadm -d
     /dev/zvol/dsk/rpool/dump` (dedicated, savecore on, compressed). The
     clone has the same `/etc/dumpadm.conf`.
   - `beadm create -e macpro-full amdgpu-ssd` (with its `/var`), mounted on
     /tmp/amdgpu-ssd; `pkg -R ... install -g` the p5p (675ee2ad..., 6
     packages, 115 actions, drm-perms SMF service); `update_drv -b` aliases
     `"pciex1002,679e.106b.126"` and `"pci1002,679e.106b.126"` (major 334);
     /etc/system: amdgpu_dpm, _runtime_pm, _aspm, _audio 0,
     ip_block_mask 0xfffffe7f, halt_on_panic 0 (inherited);
     `bootadm update-archive -R -F cpio` (130 MB; contains amdgpu, the four
     misc modules, firmware, amdgpu.conf, the aliases, /etc/system).
     Unmounted, p5p deleted from /var/tmp. Live system unchanged apart from
     the dump device.
   - New `amdtest` (`amdtest.c` here; old one was on the stick only), built
     on the VM (the Mac lacks `system/header/header-drm`: our libdrm
     2.4.134 package ships no `drm.h`, a packaging gap), in `~/amdtest` on
     the Mac. `./amdtest [node] [o|q|c]`: open only / + init and queries /
     + deinit and close; every step printed unbuffered on stderr.
   - Found in passing: an 894 MB `~/core` (17:33) of some user program on
     the Mac (not ours, left alone); BEs `macpro-full-backup-1/-2` (16:33)
     from another session's pkg runs.

#### First boot of `amdgpu-ssd` and test o (2026-11-12 18:12-18:16)

- `beadm activate -t amdgpu-ssd`, `init 6` 18:12 (user's yes); up 18:14.
  amdgpu0 attached to the compute GPU (e04@2/display@0) at 18:14:05:
  ATOM BIOS 113-C3861LA-029 (2016/08/26), UVD/VCE disabled as asked, VRAM
  3072M + GART 1024M, 65517M GTT, the six DP connectors listed (no
  monitor), 24 CUs, "Failed to store PCI saved state" (linuxkpi), runtime
  pm off. No ring or IB test failure. `/dev/dri/card0`, `renderD128`;
  drm-perms online; gdm and GNOME (illumosfb) came up; nothing had the
  render node open.
- **Test o** (`../macpro-2013/amdtest-ssd-o.txt`): open, drmGetVersion
  "amdgpu 3.64.0", then `close()` and the Mac was gone; it booted again
  by itself (into `macpro-full`, the activation was one-time) at 18:16.
  **No dump**: `savecore: bad magic number 0` on the new rpool/dump, no
  "system down" in wtmp. The user had just logged in to GNOME on the
  Mac's monitor at that moment (could matter: nothing of GNOME had the
  render node open before).
- So close of a file that did nothing but GET_VERSION kills the machine,
  without device init, contexts or BOs of its own (the file's open still
  creates the VM: `amdgpu_driver_open_kms` -> amdgpu_vm_init, root PD
  cleared through SDMA). drm_kms's `lastclose` is empty and its
  `file_close` only frees fbs/blobs/events, so the death is in
  `amdgpu_driver_postclose_kms`: our flush/release block (ctx entity
  flush, vm_wait_idle, evf/userq shutdown and fini), then prt_va del,
  ctx_mgr_fini, **vm_fini** (PD/PT free, fence waits), pasid delayed free.
- Open question: a reset without a panic (DMA/PCIe/triple fault) or a
  panic whose dump never reached the USB SSD. Hence for the next run:
  `halt_on_panic = 1` in `amdgpu-ssd` (18:19; dumps still first), and
  `close-trace.d` (here, copied to ~/amdtest on the Mac): fbt entries of
  the five modules, from any thread, after the test's first close(2),
  streamed to the Linux workstation.

   Next run (needs the user's yes, Mac not in use, someone looking at the
   screen: panic text = photo, black/chime = reset): `beadm activate -t
   amdgpu-ssd`, `init 6`; then from the Linux workstation
   `ssh -tt mac 'cd amdtest; sudo -n dtrace -qs close-trace.d -c "./amdtest /dev/dri/renderD128 o"' | tee ../macpro-2013/amdtest-ssd-o-trace.txt`.

#### Traced test o (2026-11-12 18:28, `../macpro-2013/amdtest-ssd-o-trace.txt`)

- Boot of `amdgpu-ssd` again, amdgpu0 attached, nothing on the render
  node, halt_on_panic 1. A GNOME session was logged in on :0 again.
- close(3) at t=62.778 s -> drm_close -> begin_close -> drm_kms_file_close
  -> GEM and syncobj file_close -> `amdgpu_driver_postclose_kms`: our
  flush/release block (entity flush, vm_wait_idle = 1000 immediately,
  evf shutdown, userq cancel/fini, evf fini: all fine), UVD/VCE
  free_handles, seq64_unmap (drm_exec on the PD), prt_va bo_del,
  ctx_mgr_fini, `amdgpu_vm_fini` (pasid xa_erase, two fence waits that
  return at once, free_mapping), then `amdgpu_vm_pt_free_root`: the DFS
  over the root PD's **131072 entries** (512 GB VM, 2 levels, 10-bit
  blocks), every `amdgpu_vm_pt_free` = 0 (no PT below). The stream ends
  at t=62.872 s (94 ms after close) in the middle of that walk, after
  2717 traced entries; DTrace reported 27096 drops on CPU 10, so the real
  position is further. Nothing touched the hardware in the traced part;
  no other thread appears.
- Candidates: the walk reads `to_amdgpu_bo_vm(root)->entries[i]` (the
  root's `amdgpu_bo_vm` with its ~7 MB flexible entries array, allocated
  by `lkpi_kmalloc` in `amdgpu_bo_create_vm`): a short allocation or a
  freed root would fault part-way (panic); or the end of the walk frees
  the root PD BO (TTM release of VRAM, GART/VM teardown) and that touches
  the GPU. Screen result needed to tell panic from reset.

#### The panic, on the console (2026-11-12 18:40, gdm off in `amdgpu-ssd`)

Photo `../macpro-2013/panic-amdgpu-ssd-2026-11-12-1840.jpg`, test o
(`../macpro-2013/amdtest-ssd-o-console.txt`):

```
panic[cpu6]/thread=fffffe00f58b1c20: mutex_enter: bad mutex, lp=fffffeb1f8b961f8
    owner=fffffeb1ee5d2c00 thread=fffffe00f58b1c20
unix:mutex_panic+54
unix:mutex_vector_enter+40f
drm_core:...drm_core4resv...Resv6fences+28
drm_core:...drm_core4resv...Resv4wait+29
drm_core:dma_resv_wait_timeout+e0
amdgpu:ttm_bo_delayed_delete+33
linuxkpi:lkpi_wq_worker+f9
genunix:taskq_thread+2b5
dumping to /dev/zvol/dsk/rpool/dump ... dump succeeded
```

**Cause: a use-after-free of a GEM object's reservation in our core.**
The core's GEM object owns the `Resv` that `obj->_resv` points to
(`fobj::init_public`, `MAGIC_RESV_GEM`); at the last reference the core
destroys its object, `FObj::release` calls the driver's `free`, and the
`Resv` goes with the core object right after. The facade documented
`drm_gem_object_release()` as a no-op ("the core has released its state
before calling free"). Linux's order is the opposite: `free` (amdgpu:
`amdgpu_gem_object_free` -> `ttm_bo_fini`/`ttm_bo_release`) still locks
and tests `_resv`, and `ttm_bo_release` queues `ttm_bo_delayed_delete`,
which waits on `bo->base._resv` from a work queue; only `bo->destroy`
-> `drm_gem_object_release()` finalises `_resv`, at the very end. So the
work item waited on freed memory. Every open creates a VM with a root PD
BO (fences from its SDMA clear), so every close hit it; the 94 ms walk of
the trace was only the time until the work item ran.

Fix (VM, uncommitted, p5p sha256 bb717398...):
- `crates/drm-core/src/resv.rs`: `Resv::move_fences_to()` (hands every
  fence over, allocates nothing for an empty target), host test
  `move_fences_to_a_fresh_reservation` (compiled only: the host tests do
  not link on illumos, the Linux workstation has no Rust).
- `modules/drm_core/src/ffi/resv.rs`: `detach_gem_resv()` gives `_resv`
  a reservation of its own (`MAGIC_RESV`, as `dma_resv_init`) with the
  fences.
- `modules/drm_core/src/fobj.rs`: `release()` calls it before `free`;
  `import_free` (the core's own import objects) finalises it.
- `modules/drm_core/src/ffi/gem.rs`: `drm_gem_object_release()` =
  `dma_resv_fini(&obj->_resv)` (a live object's reservation is left
  alone, as before).

Dumps on the SSD: the screen said "dump succeeded", `savecore` on the next
boot found "bad magic number 0" (header at the end of rpool/dump is
zero). Most likely the SanDisk USB bridge's write cache lost the polled
dump writes at the reset/power cut (the 12:43 dump to the Verbatim stick
arrived). Until that is solved: the console without GNOME plus a photo.

#### The fix works; the first triangle (2026-11-12 19:02-19:10)

- `amdgpu-ssd` with the fixed drm_core (p5p bb717398..., drm_core sha256
  e2a60c84...), gdm off, console on the monitor.
- amdtest o, q, c all rc=0 (`../macpro-2013/amdtest-ssd-{o,q,c}-fix1.txt`),
  20/20 open/init/close rounds (`amdtest-ssd-loop20-fix1.txt`). One WARN:
  `dma_resv_copy_fences is not supported` (TTM's individualize at a BO's
  release) -> implemented in linuxkpi (snapshot of src, reserve, add to
  dst; selftest case `st_copy` in st_resv.c, built, not run: the selftest
  needs the QEMU guest of run-r5c-test.sh). p5p with it: d86a27bf...,
  not installed yet.
- `egltri.c` (here): EGL surfaceless (Mesa picks renderD128), GLES2, FBO
  256x256. `clear`: "GL_RENDERER AMD Radeon Graphics (radeonsi, tahiti,
  ACO, DRM 3.64)", "OpenGL ES 3.2 Mesa 26.2.6", read back 25 31 51 =
  the clear colour. Triangle: **`../macpro-2013/first-triangle-2026-11-12.png`**,
  the RGB triangle as drawn. Logs `egltri-clear-1.txt`, `egltri-tri-1.txt`.
- Kernel warnings during the runs, to fix next:
  1. `ttm_buffer_object_transfer()` (TTM's ghost BO for pipelined moves)
     memcpys the BO and calls `dma_resv_init(&fbo->base.base._resv)`: the
     copy still carries the original's `_resv.__core` pair, so drm_core
     says "dma_resv_init of a reservation that has one already; ignored",
     the ghost shares the original's reservation, `dma_resv_trylock` fails
     (WARN at ttm_bo_util.c:259) and its unlock is refused. Fix: bind the
     `_resv` pair to the structure's address (as `__core` of GEM objects
     with MAGIC_GEM), so a copy reads as fresh and gets its own.
  2. `dma_resv_replace_fences is not supported` (amdgpu replaces its
     eviction/VM fences): needs a core operation (replace every fence of a
     context) through a new facade export.
  3. libdrm: "os_same_file_description couldn't determine if two DRM fds
     reference the same file description" (no kcmp on illumos; libdrm
     falls back to comparing; harmless for one process) and no
     `/usr/share/libdrm/amdgpu.ids` (marketing name only).

#### The spinning cube on the console, without X (2026-11-12 19:14-19:17)

`fbcube.c` (here): EGL surfaceless + GLES2 on the compute GPU (radeonsi),
a lit cube with depth into a 400x400 FBO, glReadPixels each frame, copied
into the top-right corner of the console framebuffer (`/dev/fb` =
vgatext's illumos_fb on the display GPU, VIS_GETIDENTIFIER + FBIOGATTR +
mmap, as xf86-video-illumosfb does; root only, so `sudo`). An overlay over
the text console, not scanout.
- Run 1 (`../macpro-2013/fbcube-1.txt`): 35108 frames in 30 s = 1170 fps,
  but the picture sheared: pitch computed as fb_size/height = 6403;
  FBIOGATTR's fb_size is rounded up (5763072 = 1600*900*4 + 3072). The
  user: "half right and half left with scanlines in between".
- Run 2: pitch = width * 4 = 6400, 1160 fps (tears: no vblank on the
  firmware framebuffer). Run 3: paced to 60 fps, 3600 frames in 60 s.
- Readback of the whole framebuffer after run 3:
  `../macpro-2013/fbcube-console-2026-11-12.png` (cube correct top right,
  run 1's stripes left, the boot console and the TTM warnings below).
  The user: "nice".

Kernel fixes built while the cube ran (VM, uncommitted, not installed):
- `_resv` pair bound to its address (`fobj::init_public`, `ffi/resv.rs`
  init/fini/detach): TTM's ghost BO copy gets its own reservation.
- facade 2.1: `drm_illumos_resv_replace_fences()` (core
  `Resv::replace_fences_raw`, header `dma_resv.h`, both mapfiles,
  `DRM_FACADE_MINOR` 1); linuxkpi's `dma_resv_replace_fences` uses it.
  `crates/drm-facade-sys/src/bindings.rs` edited by hand to match (no
  bindgen on the VM): rerun `tools/facade/generate.sh` +
  `check-generated.sh` where bindgen is available.

#### Reservation fixes tested (2026-11-12 19:20-19:24, BE `amdgpu-ssd2`)

- `pkg update` of these packages is refused on the live image ("cannot be
  performed on a live image" with --deny-new-be), so `pkg update --be-name
  amdgpu-ssd2 --no-be-activate -g` the p5p (6c5f9276...): a clone of the
  running `amdgpu-ssd` (gdm off, aliases, /etc/system) with drm_core
  563ec129..., linuxkpi 9a664ff8.... `beadm activate -t amdgpu-ssd2`.
- `../macpro-2013/all-tests-amdgpu-ssd2.txt`: amdtest o, q, c rc=0, 20/20
  rounds, egltri (radeonsi, same pixels), fbcube 20 s at 60 fps.
- Kernel log since boot: **no TTM or reservation warning any more** (no
  ghost-BO dma_resv_init, no WARN at ttm_bo_util.c:259, no refused
  unlock, no replace/copy_fences); only the known boot lines (UVD/VCE
  disabled, "Failed to store PCI saved state").
- BEs on the SSD now: `macpro-full` (default, GNOME), `amdgpu-ssd` and
  `amdgpu-ssd2` (tests, gdm off), `macpro-full-backup-1/-2`, `macpro`,
  `openindiana` (to delete).

#### d: dpm (2026-11-12 19:30-19:45)

- `amdtest ... s [seconds]` (sensors through AMDGPU_INFO_SENSOR), and
  `gpuburn.c` (here): 2048x2048 FBO, a 64-iteration fragment shader,
  glFinish per frame, no readback.
- Baseline, dpm off (`../macpro-2013/dpm-baseline-off.txt`): every sensor
  -ENOENT; fbcube unpaced 1206 fps; SMC: GPU dies 56 C. (`smcstat` and
  the `applesmc` driver are the Mac sensor task's, see
  `../macpro-2013/README.md`; used here read-only, not built or changed
  by this work.)
- BE `amdgpu-dpm` (clone of amdgpu-ssd2, only `amdgpu_dpm = 0` commented
  out): "Internal thermal controller with fan control", "si dpm
  initialized"; "Unable to register hwmon device: -19" and "registering
  pm sysfs failed (-19)" (no hwmon/sysfs on illumos; sensors work
  through the ioctl). Power and load sensors: -EINVAL on SI.
- `dpm-on-1.txt`, `dpm-on-burn-1.txt`: **sclk 300 / mclk 150 MHz idle and
  under full load** (gpuburn 80 fps = 21.5 G pixel-iterations/s), 59-61
  C. `dpm-on-dtrace-1.txt`: during the load only si_dpm_get_sclk/mclk
  run; never amdgpu_dpm_compute_clocks / si_dpm_set_power_state.
- Cause: on Linux only display changes (dce_v6_0_crtc_dpms), UVD/VCE
  and sysfs call amdgpu_dpm_compute_clocks(); a headless GPU gets it from
  the fbdev client's first commit (drm_client_setup() disables the
  unused CRTCs). Our LinuxKPI's drm_client_setup() does nothing, so the
  boot state stays. First patch 0015 (compute_clocks at probe): built
  (BE `amdgpu-dpm2`, p5p 5f2e386e...), still 300/150 under load
  (`dpm-on-0015.txt`). **That cause was wrong**: anonymous DTrace from
  boot (`dtrace -A`, amdgpu_dpm = 1) shows the state *is* switched at
  boot without the patch (amdgpu_legacy_dpm_compute_clocks ->
  change_power_state_locked -> si_dpm_set_power_state = 0 ->
  si_dpm_force_performance_level(AUTO=1)); the later calls find the state
  equal. My earlier DTrace only watched during the load.
- The chosen state (amdgpu_ps at the pick's return, read with mdb; no
  CTF for amdgpu): caps 0x78, class 5, si_ps with 4 levels (sclk/mclk
  MHz, VDDC mV): 300/150/825, 450/1270/881, 650/1270/950, 725/1270/1006.
  So the levels are right (725/1270 is the D500's top) but the SMC stays
  at level 0 under load, or the sensor reads index 0 only.
- **Patch 0015 reworked** (`0015-illumos-amdgpu-dpm-level-from-etc-system.patch`,
  the compute_clocks version deleted): `amdgpu_illumos_dpm_level` in
  /etc/system (0 auto, 1 low, 2 high), applied at probe like Linux's
  sysfs power_dpm_force_performance_level. Level 2 tells a stuck SMC
  (clocks then 725/1270, gpuburn faster) from a wrong sensor.
- BE `amdgpu-dpm3` (p5p 729f160e..., level 2): the probe logs
  "illumos: dpm performance level high (-22)"; clocks still 300/150.
  (The run started on `amdgpu-dpm2` because `init 6` took effect only
  later, 20:04; a clean shutdown, rpcbind stopped normally.)
- Anonymous DTrace of every SMC message from boot
  (`../macpro-2013/dpm-smc-trace-1.txt`): firmware upload 0, start_smc 0,
  si_dpm_enable 0, is_smc_running true 35 times (false once, before the
  start); every message answers 0x1 (PPSMC_Result_OK) **except
  PPSMC_MSG_SetForcedLevels (0x83) with parameter 1 -> 0xFF (Failed)**:
  boot's AUTO (SetForcedLevels 0, SetEnabledLevels 4) is accepted, our
  HIGH (SetEnabledLevels 4 OK, then SetForcedLevels 1) refused. So the
  SMC runs and talks, all four levels are enabled, and it refuses to go
  above level 0, on request and by load. Linux radeon on this Mac (LMDE
  live, `../macpro-2013/linux-dmesg-lmde7-live.txt` line 1388) logged
  "si_restrict_performance_levels_before_switch failed" on the display
  GPU: an SMC level refusal of the same family under Linux too.
- Open: why the SMC refuses (state table it was given: voltages via
  SVI2/GPIO, mclk switching and the MC register table, ULV, the
  display-less configuration, Apple's VBIOS powerplay table). Next
  evidence without our code: boot the LMDE DVD, load both GPUs
  (e.g. glmark2 or an OpenCL run on the compute GPU) and read
  /sys/kernel/debug/dri/*/radeon_pm_info (current sclk/mclk) or with
  amdgpu.si_support=1 pp_dpm_sclk: if Linux also stays at level 0, it is
  this Mac's firmware/VBIOS, not our port.

#### e: the display GPU on amdgpu (2026-11-12 20:15-20:25, BE `amdgpu-disp`)

- `kmstest.c` (here): lists connectors, picks the first connected one and
  its preferred mode, dumb buffer with colour bars/gradient/frame,
  drmModeSetCrtc for N seconds, restores. On the compute GPU (before):
  6 DP connectors, all disconnected (DP AUX probing works).
- BE `amdgpu-disp` (clone of amdgpu-dpm3): aliases `.106b.125` added
  (display GPU), amdgpu_dpm = 0 again, level line removed, gdm off.
  Both GPUs attach: display GPU e08@3 = amdgpu1 = card0/renderD128,
  compute GPU e04@2 = amdgpu0 = card1/renderD129 (attach order).
  `/dev/fb` now links to the display GPU's card0 node.
- Display GPU: **DP-4 connected, 41 modes, preferred 3840x2160@60** (the
  Dell, EDID over DP AUX). `kmstest /dev/dri/card0 20`
  (`../macpro-2013/kmstest-disp-1.txt`): crtc 44, 3840x2160@60,
  drmModeSetCrtc = 0, 20 s, restore, no error; only "No connected audio
  pins found" (amdgpu_audio = 0).
- 20:28-20:32: the user saw the bars (upper half) and the gradient (lower
  half), then phase 2's full-screen checkerboard (`kmstest-disp-2/3/4.txt`;
  read-back of all 8100 pages correct; amdgpu_bo_pin at the mode set, no
  move). **KMS on the display GPU works**, 3840x2160@60 on DP-4.
- BE `amdgpu-x` (clone of amdgpu-disp): version-lock facets for
  x11/server/xorg, xserver-common, xephyr released, then
  `pkg://userland/...@1.19.7-2026.0.0.19` from xorg-glamor.p5p (the
  installed rev 18 is publisher openindiana.org: name the publisher);
  illumosfb conf moved to .off; `20-amdgpu-modesetting.conf`
  (modesetting, kmsdev /dev/dri/card0, AccelMethod glamor); gdm on
  (profiles + SMF repository).
- Boot 20:39: Xorg modesetting on card0, EDID of DP-1..6 read, **"glamor
  initialization failed"** after "glamor: EGL version 1.5" -> ShadowFB.
  libglamoregl.so (xserver 1.19.7) requires `EGL_MESA_drm_image`
  ("EGL_MESA_drm_image required."); Mesa 26.2.6's libEGL no longer has
  it (Mesa dropped it; the iMac's glamor ran on Mesa 21). Fix: an Xorg
  whose glamor does not need it (xserver 21.1.x; 1.20 already dropped
  the requirement) or a backport into 1.19.7's glamor_egl.c.
- The monitor kept the last console text ("wsconfig"): every gdm X start
  (:0..:5, /var/log/Xorg.[0-5].log) died with "drmSetMaster failed:
  Device busy" / "AddScreen/ScreenInit failed". Same trap as the iMac
  (`../imac2013-imac21.md`): devfsadm links /dev/fb to the display GPU's
  card0 node; OI's Xorg opens /dev/fb first (console handling) and so
  becomes DRM master on that file; modesetting's own file then cannot
  take master. Workaround for this boot: `/dev/fb -> /dev/null`, `svcadm
  restart gdm` (20:50): Xorg + the gdm greeter (gnome-shell as gdm) run
  on amdgpu KMS, DP-4 connected, unaccelerated (ShadowFB, llvmpipe).
  Proper fix to do: no /dev/fb link to a DRM node (drm-devlink /
  devfsadm), or Xorg not opening /dev/fb when a KMS driver is used.
- Decision (user): a newer Xorg, since Mesa is current. Base: Oracle's
  solaris-userland `components/x11/xserver/xorg` = xorg-server 21.1.25,
  autotools, 27 patches (the Sun set rebased; glamor disabled there),
  sparse checkout in `~/src/solaris-userland` on the VM. OI's component
  is 1.19.7 with 41 patches; `libxcvt` (new 21.1 dependency) is already
  in oi-userland. The input drivers (xf86-input-keyboard/mouse, Solaris)
  need rebuilding for the new driver ABI.
#### GNOME accelerated on the D500 (2026-11-13 00:20, BE `amdgpu-x21`)

Xorg 21.1.25 (oi-userland worktree `~/src/oi-userland-xorg21`, branch
xorg-server-21: Oracle's 27 patches + OI's 12/14/18/22/33/37, sun-src,
Oracle's mdb module rules with `BITS = 64`, `-z parent` dropped, xdmx gone,
glamor on, libepoxy/libxcvt/mesa in REQUIRED_PACKAGES, `dir
usr/include/xorg`, the /usr/X11/include/xorg link commented out (TODO),
published with PKGLINT=/usr/bin/true because libdrm is from gfx-drm),
keyboard 1.9.0 rev 5 and mouse 1.9.5 rebuilt against the 21.1 SDK
(temporary PKG_CONFIG_PATH.prepend lines: do not commit). p5p
`~/src/xorg21-test.p5p` (29148ec5...). BE `amdgpu-x21` = amdgpu-x + these
(version-lock facets released, publisher named in the FMRIs).

Three more causes before GNOME showed:
1. Mesa `src/gallium/auxiliary/util/u_screen.c`: the dmabuf (PRIME) cap is
   taken from the kernel only `#if DETECT_OS_LINUX || BSD || MANAGARM`;
   on illumos 0, so GBM's DRI backend had no dma-buf export and fell back
   to dumb buffers (XRGB scanout / ARGB cursor only); modesetting's ARGB
   front buffer failed silently (ScreenInit). Found with gbmtest.c (here)
   and DTrace pid probes (drmmode_create_bo = 0). Fix: `||
   DETECT_OS_SOLARIS`, patch
   `patch-src_gallium_auxiliary_util_u__screen.c.patch` in the mesa-26
   worktree; libgallium rebuilt with ninja and swapped into amdgpu-x21
   by hand (runpath set with elfedit; original kept as .orig). Upstream
   candidate.
2. xf86-input-hotkey (old ABI) crashed X in EnableDevice: moved aside in
   amdgpu-x21 (`hotkey_drv.so.abi19-off`); needs a rebuild for 21.1.
3. DRI3: every PixmapFromBuffers BadAlloc (gnome-shell black, pointer
   only). libdrm's amdgpu_bo_import() takes a dma-buf's size from
   lseek(SEEK_END), which is 0 on our dma-buf fds -> GEM_VA map_size 0
   -> amdgpu_vm_verify_parameters -EINVAL. Fix in libdrm (gfx-drm
   `usr/src/common/libdrm/patches/amdgpu-bo-import-size.patch`: size 0 ->
   AMDGPU_GEM_OP_GET_GEM_CREATE_INFO), libdrm_amdgpu built with bldenv +
   dmake (CTFCONVERT/CTFMERGE from /opt/onbld), swapped in by hand
   (.orig kept). Kernel fix to do: lseek SEEK_END on a dma-buf fd = size,
   as Linux (a "Size" property of the dma-buf minor, specfs spec_size).
   Upstream candidate for libdrm.

Result: gdm greeter, login, GNOME 45 on glamor; glxinfo in the session:
direct rendering, "AMD Radeon Graphics (radeonsi, tahiti, ACO, DRM 3.64)",
OpenGL 4.6 compat, Mesa 26.2.6. Still by hand in amdgpu-x21: /dev/fb ->
/dev/null (the `fbnull` site service runs but devfsadm removes the link
again), /dev/sound, /dev/kbd, /dev/mouse to the user after login (gdm /
logindevperm gap; kbd/mouse fixed by the Mac Pro session, its plan 1b).

#### After that boot (00:39, Mac Pro session, the user's request)

- `amdgpu_audio = 0` removed in amdgpu-x21: "No connected audio pins"
  gone; audiotest on /dev/dsp2 (display GPU's HDA function, 106b:125)
  runs at 39897 Hz measured (48000 x 0.831) and **the Dell plays
  nothing**. To check: ELD/audio infoframe, DP audio stream enable
  (AFMT / DIG audio packet enable, encoder audio_enable), mute bits,
  routing of the DCE6 audio endpoint to the active DIG, and the DTO
  (dce_v6_0_audio_set_dto: DCCG_AUDIO_DTO1 phase/module and its clock).
  Monitor volume/source unconfirmed.
- X without VTs ("VT infrastructure is not available": no console on
  the screen since amdgpu owns the display GPU) opens /dev/fb (the
  no-VT fallback in sun_init.c xf86OpenConsole); /dev/fb -> /dev/null
  lost at every boot (devfsadm). Input: HAL-added /dev/usb/hid* devices
  flood "mouse: Read error: Error 0" from X start (console drivers own
  the hid streams). Interim (Mac Pro session):
  `/etc/X11/xorg.conf.d/30-console-input.conf` in amdgpu-x21
  (AutoAddDevices false, kbd on /dev/kbd, mouse VUID on /dev/mouse =
  conskbd/consms): no read errors, German layout. Remove it when VTs
  are back.
- Window drag freezes GNOME unless a GL client runs (Mac Pro session):
  `vbltest.c` (here): every CRTC reports mode_valid 0 via GETCRTC (44
  too, while the desktop shows), WAIT_VBLANK -> drm_vblank_get -EINVAL
  without calling amdgpu (refcount > 0, vblank->enabled false = the
  drm_crtc_vblank_off "inmodeset" state).
  Trace 1 (`/var/tmp/modeset-trace.txt` on the Mac, gdm restart 01:07):
  X's SETCRTC on CRTC 44: mode_set, dpms ON, drm_crtc_vblank_on, DP audio
  set_dto(600000)/audio_enable 1/unmute; ~190 ms later a kernel worker
  ("sched") drm_crtc_helper_set_config(NULL) turns all six CRTCs off
  (dpms OFF, vblank_off, audio_enable 0); nothing turns them on again.
  Trace 2 (`/var/tmp/rmfb-trace.txt`, restart 01:11): old X RMFB fb 68 and
  closes cleanly; new X: ADDFB, six SETCRTC with fb 0 (all off, each
  followed by vblank_off on all CRTCs from the legacy helper), no SETCRTC
  that turns 44 on in 3 minutes; X log without mode-set errors. So the
  CRTC stays off from DRM's view (no vblank: freezes; no DP audio:
  silent Dell). Next: SETCRTC with mode_valid and return values,
  PAGE_FLIP, GETCRTC replies (our core/drm_kms legacy state vs X).
  Trace 3 inside Xorg (2026-11-13 06:43, pid provider, X stopped at
  exec by a destructive launcher script and resumed by `dtrace -p`;
  `../macpro-2013/xorgin-trace-amdgpu-x21-0643.txt`, scripts xorgin.d,
  xlaunch.d): **X is right**: encoders possible_crtcs 0x3f, xf86PickCrtcs,
  `drmModeSetCrtc(crtc 44, fb 68, 1 connector, mode) = 0`, then
  SETCRTC fb 0 on 46..54 (= 0). Combined with trace 1 (each of those
  "off" SETCRTCs also ran crtc_disable/dpms OFF/vblank_off on CRTC 44):
  the legacy helper's `drm_crtc_helper_disable()` /
  `__drm_helper_disable_unused_functions()` on another CRTC treats CRTC
  44's encoder as unused (drm_helper_encoder_in_use: connector->encoder
  over drm_connector_list_iter) and switches 44 off. Next: kernel trace of
  those helpers with return values during an X start; then the fix in
  drm_kms/linuxkpi (connector iteration or connector->encoder).
- **Next big item: amdgpu console takeover** (tem on amdgpu's
  framebuffer through gfx_private, as i915n's i915_illumos_con*.c):
  VTs back, X on /dev/vt/N, no /dev/fb and no input fight.

- (Earlier plan) Xorg with glamor from `~/src/drm/xorg-glamor.p5p` on the VM
  (xorg-server 1.19.7 rev 19 --enable-glamor, the iMac's accel work:
  libglamoregl.so, modesetting_drv.so) in a clone, `modesetting` on
  /dev/dri/card0 with AccelMethod glamor, then gdm/GNOME.

   Earlier plan (kept): from the Linux workstation
   `ssh -tt ... 'cd amdtest; ./amdtest /dev/dri/renderD128 o' 2>&1 | tee
   notes/macpro-2013/amdtest-ssd-o.txt`, then `q`, then `c`.
2. **Boot amdgpu-ssd when the user is not using GNOME.** Check attach,
   /dev/dri, no autodetach. GNOME on illumosfb must still come up (amdgpu
   only on the compute GPU).
3. **Test a again** (open, queries, close), trace streamed to the Linux workstation
   (`ssh -tt ... | tee notes/macpro-2013/...`). On a panic: savecore
   on the next boot, `mdb` the dump on the VM, `::status ::stack ::msgbuf`.
4. Fix on the VM, new p5p, repeat until close is clean. Then b (EGL
   surfaceless clear + readback), c (radeonsi offscreen, glmark2-like).
5. d: dpm (drop amdgpu_dpm=0) and check clocks rise and fan/temperatures
   (applesmc) behave. e: the display GPU on amdgpu (alias `.125`), X
   modesetting + glamor on card1, GNOME accelerated.
6. Later: console takeover, teardown power-off, packaging cleanups, delete
   BE `openindiana`.

- Mistake on my side: while looking for a dump I ran `savecore -L` (a live
  dump of the running x11-fb system, 963 MB); there was no panic dump to
  lose, and I deleted `vmdump.0` and `bounds` again.

## Stage 2: a test BE (needs the user's yes)

`beadm create amdgpu1` from **`consolefb`** (not `usbroot`: its first
version of the hubd fix leaves the USB keyboard dead until it is replugged;
`consolefb` has all three of the bring-up session's fixes, console on the
monitor, no X), install the p5p into it (`pkg -R`), add the amdgpu alias
for `pci1002,679e.106b.126` and the cautious settings in its `/etc/system`,
update its boot archive, boot it once. The current default BE stays the
default, so a power cycle always returns to it. Only when the Mac is not in
use by the other work (the user tests `startx` in `x11-fb`).

## Stage 3: load without attaching

`modload` amdgpu (it pulls in `drm_core`, `linuxkpi`, `drm_helpers`).
Expected: the modules' `_init` messages, no attach (nothing is bound).
Then `modunload` and load again, to see that unload is clean.

## Stage 4: first attach, cautious settings

With the module loaded, set with `mdb -kw` (variables in `amdgpu_drv.c`):
`amdgpu_dpm` 0 (no power management), `amdgpu_runtime_pm` 0,
`amdgpu_aspm` 0, `amdgpu_audio` 0, and `amdgpu_ip_block_mask` without UVD
and VCE (work out the bits from `si_set_ip_blocks()` for `CHIP_TAHITI`
first). Then bind the compute GPU only:
`add_drv -i '"pci1002,679e.106b.126"' amdgpu` (or `update_drv -a` if the
driver is known). What should appear, in order: the VFCT VBIOS for bus 2,
ATOM init (the compute GPU may not be posted by Apple's EFI: then amdgpu
posts it through the VBIOS), the six firmware files, GMC with 3 GB VRAM
and the 256 MB BAR, IH (MSI), then the GFX, compute and SDMA ring tests
and the IB tests.

Capture every time: `dmesg`, `echo ::msgbuf | mdb -k`, `prtconf -v` of the
node, `kstat -m drm_core` (or the core's kstats), `modinfo`.

Stop rules: a panic gives a crash dump (`savecore`, keep it with the
logs); a hang means a power cycle into `usbroot` and the BE stays as it
was; any warning about leaks or a refused detach is recorded before the
next try. One change per run.

## Stage 5: detach and unload

`rem_drv amdgpu` (or `update_drv -d`), `modunload`: the core's checks for
remaining objects and fences (a refused detach keeps the module), and our
fini paths (DMA bindings, tt pages, `lkpi_dma_fini` leftovers).

## Later stages (not the first test)

1. Power management on (`amdgpu_dpm` default), then UVD/VCE.
2. User space on the compute GPU: libdrm's `amdgpu` for device info and GEM
   create/map, then Mesa radeonsi. Both exist on the build VM but not on
   the Mac: libdrm 2.4.134-2026.0.0.98 (gfx-drm, repo
   `~/src/drm/gfx-drm/packages/i386/nightly/repo.drm`), and Mesa 26.2.6 with
   radeonsi and LLVM 21 (`~/src/oi-userland-mesa26`, branch mesa-26; not
   published yet: unresolved dependencies; needs libdrm >= 2.4.133).
3. PRIME between the two GPUs (rounds 27 to 29).
4. The display GPU: console takeover from the EFI framebuffer at its BAR 0
   (0x90000000; `fb_info.paddr` 0x90180000), DCE 6, then X. With the
   bring-up session's console fix the console is `e08@3`, so amdgpu there
   takes over from vgatext/gfx_private. Only after the compute GPU is
   solid.

## Open risks

- Apple's EFI may leave the compute GPU unposted; ATOM posting runs VBIOS
  code that may use the I/O BAR (0x3000): check LinuxKPI's port I/O.
- MSI is limited to 32-bit addresses on these GPUs (Linux notes it).
- The IOMMU, if on, changes every DMA address the GPU sees.
- Everything from rounds 18 to 29 runs for the first time: user mappings,
  TTM DMA, sync objects, PRIME. Expect bugs in the order they run at attach:
  VBIOS, firmware, MMIO, interrupts, GTT, rings.
