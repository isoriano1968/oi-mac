# SI dpm on the Mac Pro 2013: boot messages, init walk, fix (2026-11-14)

Goal: both FirePro D500 (Tahiti 0x679E, Apple subsystems 106b:0125 display
GPU on bus 6, 106b:0126 compute GPU on bus 2) ramp their clocks under load
on illumos, as they do under Linux radeon on the same Mac.

What is known (measured, not guessed):

| | Linux radeon (LMDE 7 live, 6.12) | illumos amdgpu (dpm4/dpm5) |
|---|---|---|
| idle level | L0 300/150 MHz | L0 (index 0) |
| under load | **L3 725/1270** (glxgears, both GPUs) | **L0**, GPU busy 18-20/20 samples |
| forced "high" | accepted, L3 at once | SMC answers 0xFF to SetForcedLevels(1) |
| errors | `si_restrict_performance_levels_before_switch failed` once at boot | none |

Sources: `linux-t2-dmesg-radeon.txt` (local, has MAC addresses),
`illumos-dpm5-boot-messages.txt`, the Mac Pro session's T1/T2 reports.

The SMC firmware is byte-identical (tahiti_smc.bin, 63932 bytes, sha256
6fa1ebd7...cfa51, = linux-firmware amdgpu/ and radeon/). The PowerPlay table
and the activity thresholds the driver uploads are the ones Linux computes
(section 2).

## 1. Boot messages of an illumos dpm boot (dpm5), each explained

Every amdgpu/drm/linuxkpi warning or notice that is not plain information,
in boot order. For each one: what Linux does at that point, what we do,
whether it can touch dpm/the SMC, and the fix.

1. **`disabled ip block: 7 <uvd_v3_1>`, `8 <vce_v1_0>`** (both GPUs).
   Cause: our `/etc/system` `amdgpu:amdgpu_ip_block_mask = 0xfffffe7f` from
   the 11-11 bring-up (caution, not a failure). Linux radeon initialises
   both (log: "UVD initialized successfully", "VCE initialized
   successfully", VCE firmware 50.0.1). dpm impact: the SI state table has
   UVD/VCE clock fields, but every state we upload has vclk/dclk/evclk/ecclk
   0, and `si_set_vce_clock()` / `ni_set_uvd_clock_*()` only act when those
   change; the SMC does not depend on the UVD/VCE blocks being up. Radeon's
   UVD init does put the UPLL into a defined state (`si_set_uvd_clocks()`
   10/10 MHz, then 0/0 = bypass and sleep). Fix: needed anyway for video
   decode; it needs `amdgpu/tahiti_vce.bin` in our firmware package (only
   tahiti_uvd.bin is there today). Not the dpm cause (see 2.4), so not in
   the dpm test.
2. **`[drm] Internal thermal controller with fan control`**: information,
   the same line as Linux.
3. **`Unable to register hwmon device: -19`**. Cause: LinuxKPI has no hwmon
   class (`hwmon_device_register_with_groups()` returns -ENODEV). Linux:
   creates /sys/class/hwmon/hwmonN (temperature, fan) for user space only.
   dpm impact: none; the thermal work handler and the SMC do not use hwmon.
   Fix: none needed for dpm; the sensors are readable through
   AMDGPU_INFO_SENSOR (amdtest s). Will be silenced in the log (patch 0023,
   NOTICE instead of WARNING) so real warnings stand out.
4. **`registering pm sysfs failed (-19)`**. Cause: no sysfs
   (`amdgpu_pm_sysfs_init()`). Linux: power_dpm_state,
   power_dpm_force_performance_level, pp_dpm_sclk, ... for user space.
   dpm impact: none on the state machine; we lose the user interface, which
   patch 0015 (`amdgpu_illumos_dpm_level`) replaces for the forced level.
   Fix: as 3 (log level only).
5. **`Failed to store PCI saved state`**. Cause: LinuxKPI's
   `pci_store_saved_state()` returns NULL. Linux keeps the config space for
   a later GPU reset or PCI error recovery (`amdgpu_device_load_pci_state()`).
   dpm impact: none (only used on reset/resume). Fix: implement
   `pci_save_state()`/`pci_store_saved_state()`/`pci_load_saved_state()` in
   LinuxKPI from the 256/4096-byte config space (open item 1.4, needed for
   GPU reset); not part of the dpm fix.
6. **`Runtime PM not available` / `runtime pm is manually disabled`**: our
   `amdgpu_runtime_pm = 0`; the Mac's GPUs are not PX/BOCO devices, Linux
   would not use runtime PM either. dpm impact: none.
7. **`PCIE atomic ops is not supported`, `TMZ not supported`**: the same
   on Linux for Tahiti (no atomics to the root port on this Mac). None.
8. **`Please enable CONFIG_MTRR and CONFIG_X86_PAT ... write-combining`**
   (08:20:43, first USWC buffer from Mesa). Cause:
   `amdgpu_bo_support_uswc()` without CONFIG_X86_PAT. Linux built without
   PAT behaves exactly as we do (GTT cached and snooped); CPU-visible VRAM
   is already write-combined through BAR 0 (round 21). dpm impact: none
   (CPU mapping attribute only). Fix: later performance item (needs illumos
   kpm page attributes), see README round 24.
9. **`pcieb1: slot 3: power fault detected cannot be cleared; not enabling
   power fault detection`** (illumos pciehp, the compute GPU's slot e04@2).
   Linux: `pciehp: Slot #3 ... PwrCtrl+` without a fault message. Both GPUs
   behave the same (display GPU slot e08@3 has no such line), so it is not
   the dpm cause; noted for the hotplug work.
10. **Missing compared with Linux**: radeon prints `PCIE gen 3 link speeds
    already enabled` (`si_pcie_gen3_enable()`); amdgpu's
    `si_pcie_gen3_enable()` returns silently on our device because
    LinuxKPI reports the GPU's bus as a root bus (`pci_is_root_bus()`,
    `bus->parent == NULL`, `bus->self == NULL`). The link already runs at
    gen 3 (levels show pcie gen 3), so the effect is nil here, but see 2.3.

## 2. The SI power-management init, Linux radeon vs amdgpu vs our port

### 2.1 dpm and SMC code: identical

Mechanical comparison (script `callwalk.py`, scratch): for every function
in `radeon/si_dpm.c` and `amd/pm/legacy-dpm/si_dpm.c` (pinned v7.2 tree,
without our patches) the sequence of driver calls. 91 functions are
identical, 65 differ only by renamed helpers (`si_send_msg_to_smc` ->
`amdgpu_si_send_msg_to_smc`, `ni_get_ps` -> `si_get_ps`, `r600_*` ->
`amdgpu_*`). Real differences:

- `si_dpm_setup_asic()`: radeon also loads the MC microcode and reads the
  memory type; amdgpu does both in gmc_v6_0 (sw_init reads MC_SEQ_MISC0 ->
  GDDR5, logged "RAM width 384bits GDDR5"; hw_init loads the MC ucode if
  the MC is not running). Same effect.
- `si_upload_smc_data()`: amdgpu writes crtc_index/mclk_change_block_cp_*
  from the display configuration, 0 without a display; radeon writes them
  only with an active CRTC. Same SMC soft-register values for the compute
  GPU (none written vs 0 written on a zeroed table).
- `si_dpm_late_enable()` (radeon) = `si_dpm_late_init()` (amdgpu): both
  `si_set_temperature_range()`.
- `si_notify_hw_of_powersource()`: `#if 0` in radeon, amdgpu only from an
  ACPI AC event; RunningOnAC is never sent by either at init.

Also identical: the SMC ucode loader (`si_load_smc_ucode` /
`amdgpu_si_load_smc_ucode`, header, BE words, auto-increment),
`si_init_smc_table()` system flags (GPIO_DC from HARDWAREDC,
REGULATOR_HOT, GDDR5, AC2DC_GPIO5_POLARITY_HIGH, REGULATOR_HOT_PROG_GPIO),
force-level code, PPSMC message numbers, Tahiti/0x679E PowerTune data
("aruba pro" DTE), `r600_calculate_at()` thresholds (logged L0 up 2412,
L1 down 1232 = computed by hand), firmware selection (no `_k_` image for
Tahiti).

Patches of ours that touch this path: only logging (0020/0021) and the
read-only register window (0022). None changes behaviour with quirks 0.

### 2.2 Order: radeon enables dpm before the GPU is started

radeon `si_init()`: `si_mc_init`, `radeon_bo_init`, `si_init_microcode`,
**`radeon_pm_init()` -> `radeon_pm_init_dpm()`** (power states, hwmon,
`si_dpm_init`, `si_dpm_setup_asic`, **`si_dpm_enable`**), `si_uvd_init`,
`si_vce_init`, IH ring, then `si_startup()` (`si_pcie_gen3_enable`,
`si_program_aspm`, `si_mc_program`, GART, RLC with `si_init_pg/cg`, CP,
DMA, UVD/VCE start, IRQ). Later `radeon_pm_late_init()` ->
`si_dpm_late_enable` (temperature range) + `radeon_pm_compute_clocks()`.

amdgpu: `amdgpu_device_ip_hw_init_phase1/2` in IP order: si_common
(golden registers, gen3, ASPM), gmc_v6_0 (MC program, GART), si_ih,
gfx_v6_0 (RLC, CP), si_dma, **si_dpm (`si_dpm_setup_asic`,
`si_dpm_enable`, `amdgpu_legacy_dpm_compute_clocks`)**, dce_v6_0; then
late_init: si_dpm late_init (temperature range) and clock gating
(`amdgpu_device_set_cg_state(GATE)`). Our port runs this unchanged.

So the SMC is started after RLC/CP on amdgpu, before them on radeon. This
is upstream amdgpu behaviour on every SI GPU and works on PCs; it is a
difference, not shown to matter.

### 2.3 PCIe/ASPM: radeon programs it on this Mac, we never do

- radeon `si_program_aspm()` runs whenever `radeon_aspm != 0` (default -1)
  and the GPU is PCIe; it does not look at the platform. Linux's PCI core
  disabled ASPM from the FADT ("FADT declares the system doesn't support
  PCIe ASPM"), but that only affects the link-control bits the core owns;
  radeon's GPU-side programming (LC_N_FTS, LC_L0S/L1 inactivity, PIF PHY
  power-down, PCIE_CNTL2 memory light sleep) still runs.
- Inside it, only if the root port advertises CLKPM (`bus->self`,
  LNKCAP), radeon also switches clock sources: THM_CLK_CNTL CMON/TMON 1,
  MISC_CLK_CNTL DEEP_SLEEP/ZCLK 1, CG_CLKPIN_CNTL BCLK_AS_XCLK off,
  CG_CLKPIN_CNTL_2 FORCE_BIF_REFCLK_EN off, MPLL_BYPASSCLK_SEL 4,
  SPLL_CNTL_MODE SPLL_REFCLK_SEL 0.
- Ours: `/etc/system amdgpu_aspm = 0` -> `si_program_aspm()` returns at
  once. With -1 it would also return: `amdgpu_device_should_use_aspm()`
  asks `pcie_aspm_enabled()` (off from the FADT). And LinuxKPI has no
  `bus->self`, so the CLKPM branch could never run.

These are power-saving settings that run on every Linux SI system with
radeon; machines without CLKPM (most desktops) skip the clock-source part
and ramp fine. Not the cause on its own evidence.

### 2.4 UVD/VCE

Masked by us, initialised by radeon. dpm states carry vclk = dclk = 0 on
both; nothing in si_dpm waits for UVD/VCE. Not the cause.

### 2.5 The environment the SMC sees: board GPIOs and ACPI

The VBIOS platform caps (0x017D9022) tell the GPU SMC to obey board
signals: HARDWAREDC (AC/DC on a GPIO, polarity reversed:
REVERT_GPIO5_POLARITY), REGULATOR_HOT with VRHOT_GPIO_CONFIGURABLE,
OUTPUT_THERMAL2GPIO17, TEMP_INVERSION and
COMBINE_PCC_WITH_THERMAL_SIGNAL (a platform power-capping input combined
with the thermal signal). An SMC that sees "DC", "regulator hot" or a
PCC/thermal request stays at its lowest level and refuses forced levels;
that matches every illumos observation (no ramp, SetForcedLevels(1) =
0xFF, no error elsewhere) and none of the driver-side settings can explain
it, since they are identical to Linux.

Those signals come from the Mac's own firmware (Apple SMC, ACPI). The one
OS difference in that area found in the logs: on Apple hardware Linux
removes every Windows _OSI string and answers only `_OSI("Darwin")`
(Linux log lines 96-102, "BIOS _OSI(Darwin) query honored via DMI"), so
Apple's ACPI runs its macOS paths. illumos ACPICA answers the Windows
strings and not "Darwin". Apple firmware is known to set up hardware
differently for Darwin and non-Darwin OSes (Thunderbolt, GPU power).

To be confirmed from the Mac's ACPI tables (acpidump, read-only): which
methods test `_OSI("Darwin")`/OSDW and what they do to the GPUs' slots,
the SMC (SMCD/ SmcDppt SSDT) and GPIOs. Result: section 2.6.

### 2.6 ACPI tables: _OSI/Darwin and ATIF ruled out

Tables from `acpidump -b` on the Mac (con5), `acpi-tables/` (local only),
disassembled with iasl on the VM.

- `PINI` sets OSYS: 0x2710 for `_OSI("Darwin")`, 0x03E8 Linux, 0x07D9/0x07DC
  Windows 2009/2012; `OSDW()` = (OSYS == 0x2710).
- Every OSDW()/OSYS user: Thunderbolt (ssdt5: NHI0-2 `_DSM`/`TRPE`/`EJS4`,
  the bridges' `_RMV`/`_STA`, `RMCR`/`RMCW`), wake (`_PRW` of HDEF, RP01-05,
  ETH0/1, ARPT, EC, XHC1, PRT8; GPE `_L0D`), sleep (`_PTS`, `_WAK`), XRES
  `_OSC`, RP03 `APPD`/`APPU` (Wi-Fi), HPET `_STA`, EC `_REG`.
- The GPU ports `GFXA`/`GFXB` (_ADR 0x00020000/0x00030000) and the GPUs
  `GFX1`/`GFX2` contain **no** OS test: link-training helpers (`PTRN`,
  `PDLA`, `PGPR`, `PSPD`, `RWLL`/`RWLN`, `GXW1`/`GXWA`, `PWRD` = PCIe link
  power-down/up and retrain, called by OSPM power transitions only) and an
  `ATIF` (AF00: notification mask 0x0C = thermal + forced power state,
  functions 0x03; AF01; AF02: pending 0x08 forced power state, FPWR 0).
  The EC sends Notify 0x81 to GFX1/GFX2 on its events.
- Radeon's ATIF handler (`radeon_atif_handler()`) acts only on brightness
  and dGPU display events, never on forced power state or thermal
  requests; so ATIF changes nothing for dpm under Linux either. The SmcDppt
  SSDT is empty (36 bytes, header only).

Result: the Darwin/non-Darwin ACPI paths do not touch the GPUs, their slots
or anything the GPU SMC reads, and ATIF has no dpm role in the working
Linux driver. Not the cause; no _OSI change is needed for dpm.

### 2.7 Summary of the walk

| Layer | radeon (works) vs ours | Can explain "SMC stays at L0, refuses forcing"? |
|---|---|---|
| si_dpm/si_smc code, firmware, tables, flags, thresholds | identical | no |
| init order (dpm before vs after GFX/CG) | differs (upstream amdgpu) | not shown; upstream amdgpu SI dpm works on PCs |
| ASPM / clock muxes | radeon programs, we skip | no: power-saving only, skipped on most desktops |
| UVD/VCE | radeon up, we masked | no: no dpm dependency |
| ACPI _OSI/ATIF | Darwin vs Windows strings | no (2.6) |
| hwmon/sysfs/PCI state/runtime PM/WC | interfaces only | no |
| MMIO mapping | strict order (UC), volatile accessors | no |
| **SMC SRAM contents after upload** | **not proven** | **yes**: a wrong firmware or table word would leave an SMC that answers messages but whose dpm logic does not act |

The 80 fps clue is weaker than stated earlier: 450 MHz is the VBIOS default
engine clock (FirmwareInfo), not a measured boot clock, so "the SMC never
applied even L0" is not established. The register block dump (0022)
measures the real PLL settings.

## 3. dpm6 result (2026-11-14 09:23) and what it rules out

- 0023 read-back, both GPUs: "SMC ucode read back: 0 of 15919 words
  differ (start 0x10000, version 0x10020000)"; "SMC state table at 0x1ecf8
  read back: 0 of 753 words differ; soft regs 0x1de50, sram end 0x20000;
  systemFlags 0x55 extraFlags 0x2 thermalProtectType 0" (GPIO_DC, GDDR5,
  REGULATOR_HOT, REGULATOR_HOT_PROG_GPIO; AC2DC_GPIO5_POLARITY_HIGH;
  internal thermal) = what radeon builds. **The SMC upload is intact.**
- The real engine clock (first idle pass of the register dump):
  CG_SPLL_FUNC_CNTL_3 fb_div 0xbc25e (display GPU) / 0xb1c71 (compute
  GPU), ref 27 MHz, ref_div 0, post div 4 (CG_SPLL_FUNC_CNTL 0x80400000):
  47.04 x 27 / 4 = 317.5 MHz and 44.44 x 27 / 4 = 300 MHz, the uploaded L0
  of each (317/1270 with the 4K rule, 300/150). SPLL_CNTL_MODE 0x3c01001a:
  SW_DIR_CONTROL off, the SMC owns the SPLL. **The SMC switches to our
  state and drives the PLL; it only never goes up.** (The 80 fps doubt of
  2.7 is resolved: the clock really was L0.)
- **Mistake of mine, found and fixed:** the first 0022 allowed every dword
  of 0x180-0x27f; reading the undefined offsets (from about 0x194) made
  the whole CG block read 0 until reboot (dpm6 went back to con5 at once).
  0022 and amdtest d now read only the 39 defined registers; the Linux
  script `si-pm-regdump.py` too.

## 4. The difference found: CP and RLC microcode

The RLC is what tracks GPU activity for dynamic power management on these
chips (radeon_pm_init: "DPM requires the RLC"); the SMC decides on the
RLC's reports. Comparing every Tahiti firmware file in linux-firmware
(fetched 2026-11-14) between `amdgpu/` (what we ship and load) and
`radeon/` (what Linux radeon loaded on this Mac in T2):

| file | amdgpu/ (ours) | radeon/ (Linux T2) |
|---|---|---|
| tahiti_smc.bin | 6fa1ebd7... | **same** |
| tahiti_mc.bin | d33b357c... | **same** |
| tahiti_rlc.bin | 46da3468..., ucode_ver 0xd | d942e506..., ucode_ver 0xd, **1103 of 2048 words differ** |
| tahiti_pfp.bin | aabdb871..., ucode_ver 0x54 | 247f2a45..., 0x4f |
| tahiti_me.bin | 87617358..., 0x91 | bbbfeb6f..., 0x8e |
| tahiti_ce.bin | 04027c3a..., 0x3d | 7614b51e..., 0x39 |

(The old `radeon/TAHITI_rlc.bin` is a third, different program.) The
amdgpu copies of CE/ME/PFP/RLC were replaced with AMD's 18.20 release in
linux-firmware; radeon kept the older set. Same header layout (v1.0,
44/52 bytes, same sizes), so amdgpu loads either.

This is the one input of the whole chain that differs between the
working Linux boot and ours, after the code (2.1), the SMC image and
tables (3), the MC firmware, the VBIOS and ACPI (2.6) were shown equal.
It fits all observations: the SMC runs, takes and applies our states,
answers messages, but the activity-driven step up never comes, and a
forced level is refused while the RLC side does not cooperate.

Not yet proven: that the newer RLC is wrong for SI dpm in general
(upstream amdgpu ships it); it may be the interplay with the CP images or
this board. The test settles it for this Mac.

## 5. The fix and the one test

Fix candidate: load the radeon/ Tahiti CE, ME, PFP and RLC images (the
coherent set Linux radeon ran, LICENSE.radeon) instead of the amdgpu/
ones. If it works, the package ships them (firmware/amdgpu/ names, the
WHENCE/licence noted), no code change.

Test BE `amdgpu-dpm7`, clone of amdgpu-con5, remote only:

1. `beadm create -e amdgpu-con5 amdgpu-dpm7`, mount at <mnt>; `pkg -R <mnt>
   install` the p5p (0001-0023 with the safe 0022: VM
   `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p`, sha256
   147f3c9cd6a11e401dc012e8105c55a0309ea9fbee4370d548b43ab10724b47f,
   illumos-drm local commit dd9da96).
2. Replace the four firmware files in <mnt>/kernel/firmware/amdgpu/ with
   the radeon/ ones (VM `~/src/fw-radeon-tahiti/`, sha256 above: ce
   7614b51e..., me bbbfeb6f..., pfp 247f2a45..., rlc d942e506...); check
   the sha256 on the Mac after copying.
3. In <mnt>/etc/system: remove `amdgpu_dpm = 0`, set `halt_on_panic = 0`
   (con5 has 1); keep the rest. `bootadm update-archive -R <mnt>`, unmount,
   `beadm activate -t amdgpu-dpm7`, `reboot` over ssh.
4. After boot: `grep -E "RLC|rlc|PFP|pfp|ME |CE |illumos: SMC|dpm" 
   /var/adm/messages` (firmware versions printed by amdgpu_ucode_print_*
   at debug level may not show; the read-back lines must still say 0
   words differ); then on each node `./amdtest <node> s 20` during
   `./gpuburn 30 64 <node>`, and `./amdtest <node> d 2` idle and under
   load (defined registers only, amdtest sha256 cbb5de07...).
5. Success: sclk/mclk sensor 725/1270 (L3) under load, gpuburn fps up
   (84.5/79.9 now), Greek Temple above 6.34 fps. Then `reboot` to con5.

If it does not ramp: the CP/RLC microcode is ruled out as well, and the
remaining difference is the init order (2.2), which a patch can make
radeon's (dpm enable before gfx hw_init) as the next and last driver-side
step.

## 6. dpm7 result: the CP/RLC microcode is not it

dpm7 (radeon/ ce/me/pfp/rlc in the test BE, sha-checked; 0023 again 0 of
15919 / 0 of 753): no ramp, 300/150 on both GPUs under gpuburn (79.5 /
79.9 fps), 56-59 C. Microcode ruled out.

## 7. The boot messages fixed (2026-11-14, after dpm7)

The user's order: fix every warning of the dpm boot before the next dpm
hypothesis. One combined p5p, VM `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p`,
sha256 824c0deb5e3110db8f583e26ef9724471bc40e2de8a98c28e546788006e637a3
(illumos-drm local commits 05d3bfa, 3200635 on top of dd9da96; amdgpu
patches 0001-0024, 409/409 TUs, link OK, 636 warnings).

| Message | Fix | Where |
|---|---|---|
| `disabled ip block: 7 <uvd_v3_1>`, `8 <vce_v1_0>` | `amdgpu_ip_block_mask` line removed from /etc/system (test BE); the package now also ships `amdgpu/vce_1_0_0.bin` (the VCE 1 image amdgpu requests for Tahiti; linux-firmware master, WHENCE: amdgpu licence; sha256 7afc816d...); tahiti_uvd.bin was already shipped. Expected: "UVD initialized", "VCE initialized" as radeon on this Mac. | 3200635, test BE /etc/system |
| `Failed to store PCI saved state` | LinuxKPI `pci_save_state()` saves the 64-byte header and the endpoint's PCIe DEVCTL/LNKCTL/DEVCTL2/LNKCTL2; `pci_restore_state()` writes them back top down (command register last, unchanged dwords skipped); `pci_store_saved_state()`, `pci_load_saved_state()`, `pci_load_and_free_saved_state()` copy them. MSI/MSI-X stay with the illumos interrupt framework. | 05d3bfa |
| `Unable to register hwmon device: -19`, `registering pm sysfs failed (-19)` | LinuxKPI `hwmon_device_register_with_groups()` creates the hwmon child device (drvdata, parent) without sysfs files; `amdgpu_pm_sysfs_init()` then runs to the end as on Linux (device attributes through the no-op sysfs, `sysfs_initialized`). Values stay readable through AMDGPU_INFO_SENSOR. | 05d3bfa |
| `Please enable CONFIG_MTRR and CONFIG_X86_PAT ... write-combining` | Patch 0024: the illumos build logs what it does ("GTT buffers are mapped cached and snooped, not write-combined (no write-combining kernel mappings of RAM yet); VRAM is write-combined") and drops the build `#warning`. Real WC GTT needs an illumos kernel interface to make segkpm aliases write-combined (README round 24); not doable in the driver alone. | 0024 |
| `drmach_init: number of logical CPUs (12) in physical processor is not power of 2` (twice) | illumos' dynamic-reconfiguration driver `dr` (bound to `acpidr_sbd`, linked `-Nmisc/drmach_acpi`) loads drmach_acpi, whose init refuses CPU packages with a non-power-of-2 thread count (the Mac's 6-core/12-thread Xeon) and returns ENXIO: no DR, which the Mac (no hotplug boards) does not need. Fix: `exclude: drv/dr` and `exclude: misc/drmach_acpi` in /etc/system (test BE; for keeps an /etc/system.d fragment in a package). | test BE /etc/system |

Not GPU, in the same log, left as is: dhcpagent/nwamd on bge0 (network
config), gnome-session ConsoleKit/orca (desktop), `pcieb1: slot 3: power
fault detected cannot be cleared` (illumos pciehp on the compute GPU's
slot, Linux shows the slot without a fault; separate item).

### Test BE amdgpu-fix1 (remote only)

1. `beadm create -e amdgpu-con5 amdgpu-fix1`, mount at <mnt>; `pkg -R <mnt>
   install` the p5p above (6 packages; linuxkpi changes too).
2. <mnt>/etc/system: remove `amdgpu_dpm = 0` and the
   `amdgpu_ip_block_mask` line; set `halt_on_panic = 0`; add
   `exclude: drv/dr` and `exclude: misc/drmach_acpi`; keep runtime_pm 0
   and aspm 0. `bootadm update-archive -R <mnt>`, unmount, `beadm activate
   -t amdgpu-fix1`, `reboot` over ssh.
3. Check: `egrep -i "amdgpu|drm|linuxkpi|drmach|uvd|vce" /var/adm/messages`
   for this boot: none of the five messages; "UVD initialized" / "VCE
   initialized" (or the exact error if UVD/VCE fail); 0023 lines 0 differ.
   Then per node `./amdtest <node> s 20` during `./gpuburn 30 64 <node>`
   (dpm is on anyway: note the clocks), and `./amdtest <node> d 2` (the
   defined-register amdtest cbb5de07...).
4. `reboot` to con5. Risk: UVD/VCE init is new on illumos (ring and IB
   tests at boot); a hang there needs the power button, a panic reboots
   into con5 (halt_on_panic 0).

## 8. fix1 result; the init-order patch (0025)

fix1 (p5p 824c0deb): all five GPU messages gone; "Found VCE firmware
Version: 50.0 Binary ID: 1", "Found UVD firmware Version: 64.0 Family ID:
13", "UVD initialized successfully.", "VCE initialized successfully." on
both GPUs; 0023 0 differ. Still no ramp (317/1270 at 84.1 fps on 128,
300/150 at 79.9 on 129).

Patch 0025 (illumos-drm local 4a19fd3): on SI the SMU's hw_init
(`si_dpm_setup_asic` + `si_dpm_enable`) runs first in phase 2, before
gfx_v6_0 (RLC with CG/PG init, CP), si_dma, dce_v6_0, uvd_v3_1, vce_v1_0:
radeon's order (`si_init()`: `radeon_pm_init()` before `si_startup()`).
What stays different: amdgpu programs the MC (gmc hw_init runs during
sw_init, for allocations) before dpm; radeon after. `/etc/system`
`amdgpu:amdgpu_illumos_si_smu_first = 0` restores amdgpu's order; the
boot logs "illumos: SI SMU hw_init before GFX/SDMA/DCE/UVD/VCE".

p5p: VM `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p`, sha256
8a959173d821aa58a32a88d293acb2ea14233c1ec87d070ca6614f32ce5ff9eb
(0001-0025 + the LinuxKPI fixes + vce_1_0_0.bin; 409/409, link OK, 636
warnings).

Test BE amdgpu-order1: as fix1 (clone of con5, the p5p, /etc/system
without amdgpu_dpm = 0 and ip_block_mask, halt_on_panic 0, exclude drv/dr
and misc/drmach_acpi), one-time boot over ssh; check the "illumos: SI SMU
hw_init before" line, UVD/VCE initialised, 0023 0 differ, then amdtest s
during gpuburn per node, amdtest d idle/load.

### Console messages (the user's request)

Measured in the dpm6 log: every amdgpu/drm_core/linuxkpi NOTICE line
(255 drm_core, 47 linuxkpi, 1 amdgpu) is kern.info, i.e. cmn_err with
"!" (illumos: "!" drops SL_NOTE and sets SL_LOGONLY, printf.c), so the
kernel already sends them to the log only, not to the console. LinuxKPI
(`lkpi_printk`, `lkpi_dev_printk`) and drm_core (`print::route`) map
KERN_INFO/DEBUG to "!" already; KERN_NOTICE and level-less text would go
to the console, and none occur in that boot. The console-level kernel
lines were the 10 kern.warning ones (fixed in fix1), drmach_acpi (fixed)
and nvidia_modeset's "Loading ... / Unloading" (kern.notice: an NVIDIA
driver package is installed on the Mac and loads without a GPU; not
ours). If the console still shows NOTICE lines, they come from the
routing outside the kernel (syslog.conf to /dev/sysmsg) or a verbose
boot; to be checked on the Mac before changing code.

## 9. order1 result, and what is still not identical to Linux T2

order1 (p5p 8a959173, clone of con6): "illumos: SI SMU hw_init before
GFX/SDMA/DCE/UVD/VCE (radeon's order)" on both GPUs, UVD/VCE up, 0023
0 differ, no GPU warnings, no nvidia lines (nvidia excluded). Still no
ramp: 317/1270 at 84.6 fps (128), 300/150 at 79.9 fps (129), 59-63 C.
Init order ruled out.

Every item below is either ruled out from source/existing logs (with the
evidence), or has a read-only check on con6 (no test boot, nothing
changed). No new hypotheses tested by boot until these are done.

| # | Difference vs Linux T2 | Status | Evidence / read-only check |
|---|---|---|---|
| 1 | si_dpm/si_smc code, SMC firmware, state table, system flags, thresholds, force code | ruled out | 2.1; 0023 read-back 0 differ (dpm6, dpm7, fix1, order1) |
| 2 | CP/RLC microcode | ruled out | dpm7 with radeon's set |
| 3 | init order (SMU vs GFX/RLC/CP/SDMA/DCE/UVD/VCE) | ruled out | order1 |
| 4 | MC programmed before dpm (amdgpu) vs after (radeon) | **ruled out from source** | gmc_v6_0.c writes 45 registers, si_dpm.c reads 63 (30 MC: MC_SEQ_*, MC_ARB_*, MC_PMG_*, MCLK_PWRMGT_CNTL); the two sets do not intersect, so si_dpm's inputs are the same either way |
| 5 | golden registers (si_init_golden_registers) | **ruled out from source** | amdgpu's tables compiled against si.c's headers and compared entry by entry with radeon's: 4 radeon-only and 10 amdgpu-only entries, all outside the CG/SMC block 0x180-0x27f (masks on 0x340c/0x360c/0xc, 0x52c-0x52f, 0x17bc, GB_ADDR_CONFIG 0x263e, a mask on 0x3d2a); the mgcg/cgcg init tables are equal |
| 6 | LinuxKPI MMIO and locking | ruled out | strict-order UC mapping, volatile accessors; SMC SRAM writes verified (0023); the SMC drives the SPLL to our L0 dividers (dpm6), so direct and indirect writes reach the chip |
| 7 | interrupts/IH (SI: thermal sources 230/231 only; the SMC needs no driver interrupt to step up) | ruled out from logs | a thermal interrupt would set thermal_active and make compute_clocks pick the thermal state and force LOW; every logged state in dpm4-6 is the 4-level balanced state, no thermal state, no LOW forcing |
| 8 | ACPI _OSI/Darwin, ATIF | ruled out | 2.6 (tables read) |
| 9 | boot messages (UVD/VCE, PCI state, hwmon/sysfs, WC, drmach) | fixed, no effect | fix1, section 7 |
| 10 | ASPM / clock-source muxes: radeon's si_program_aspm runs (radeon_aspm = -1, no platform check), ours never (amdgpu_aspm 0; with -1 amdgpu would still skip it: pcie_aspm_enabled() is off from the FADT, and LinuxKPI has no root port for the CLKPM branch) | open | read-only on con6: (a) `amdtest <node> d 1` (the safe 0022 is in con6/fix1) gives THM_CLK_CNTL 0x19b, MISC_CLK_CNTL 0x19c, CG_CLKPIN_CNTL 0x198/_2 0x199, MPLL_BYPASSCLK_SEL 0x197, SPLL_CNTL_MODE 0x186 as EFI left them; radeon would set CMON/TMON 1, DEEP_SLEEP/ZCLK 1, clear BCLK_AS_XCLK and FORCE_BIF_REFCLK_EN, MPLL_CLKOUT_SEL 4, SPLL_REFCLK_SEL 0, **only if** the root port has CLKPM; (b) the root ports' LNKCAP CLKPM bit (e04@2, e08@3) with pcieadm (below). If CLKPM is 0, radeon skipped the muxes too and only the GPU-side link-power bits (LC_*, PIF) differ |
| 11 | PCIe link/config state from the illumos PCI framework (DEVCTL MPS/MRRS/ExtTag, LNKCTL ASPM/CLKPM, LNKSTA speed/width) | open | `pcieadm show-cfgspace -d <path>` for both GPUs, their HDMI functions and root ports, read-only. pcieadm is in illumos-gate (usr/src/cmd/pcieadm), not in an OI package: a binary from the gate build behind the Mac's macpro kernel (illumos-applesmc, e1793c2c) is staged at VM `~/src/gpuburn/pcieadm`, sha256 759d63809ccefda13f8978c55a400312004fc408732d6fcb67f3e759374df285, system libraries only; copy to /var/tmp on the Mac, run as root (read-only), delete. Linux T2 reference from its log: link gen 3 ("PCIE gen 3 link speeds already enabled"), Extended Tags enabled, ASPM off (FADT) |
| 12 | Root port / slot: slot power limit sent to the GPU (SLTCAP SPLV/SPLS; the GPU's DEVCAP captured slot power limit), slot power controller and the "slot 3: power fault detected cannot be cleared" of illumos pciehp (compute GPU slot) | open | same pcieadm run: root ports' SLTCAP/SLTCTL/SLTSTA, the GPUs' DEVCAP CSPLV/CSPLS. Both GPUs behave the same, the fault is on one slot only, so it can at most explain part |
| 13 | Apple SMC / fans | equal in behaviour | both OSes only read the SMC (Linux applesmc, our applesmc is read-only by design); read-only check on con6: `smcstat` of the GPU-related keys (names with G/TG/PG prefixes) for any power-limit key; nothing to compare against from Linux T2 |
| 14 | Linux amdgpu itself on this Mac | unknown | the only way to tell amdgpu-SI-dpm from our port is Linux with `radeon.si_support=0 amdgpu.si_support=1` at GRUB; ruled out by the user (no Linux boots); everything comparable from source says amdgpu = radeon on this path |

What a dpm-on read would add (needs a test boot, so only after 10-12):
with the safe amdtest d in a dpm-on boot under load, CG_FTV, CG_FFCT_0,
CG_AT, CG_BSP, TARGET_AND_CURRENT_PROFILE_INDEX and the SPLL registers
show whether the SMC's activity measurement moves at all; dpm6 lost them
to the wedged block.

## 10. The section-9 reads on con6 (2026-11-14 11:43), compared

Files: `con6-reads/` (pcieadm 759d6380 show-cfgspace of both root ports,
both GPUs and their audio functions; amdtest cbb5de07 `d 1` on both
nodes; smcstat). con6 = clone of fix1, so dpm is on: the dumps are a dpm
idle state.

- **#10 ASPM / clock muxes: same as Linux.** Both root ports (0/2/0,
  0/3/0): LNKCAP "Clock Power Management: unsupported", LNKCTL ASPM None.
  radeon's si_program_aspm() takes the clock-mux branch only with CLKPM,
  so on Linux it was skipped too; THM_CLK_CNTL 0x00004020, MISC_CLK_CNTL
  0x01202001, CG_CLKPIN_CNTL 0x1 / _2 0x00c64009, MPLL_BYPASSCLK_SEL 0x200,
  SPLL_CNTL_MODE 0x3c01001a are the values Linux radeon also ran with (it
  does not touch them otherwise). What radeon still did and we do not: the
  GPU-side link-power timers (LC_N_FTS, LC_L0S/L1 inactivity, PIF PHY
  power-down in L1, PCIE_CNTL2 memory light sleep); with ASPM off in
  LNKCTL on both ends (FADT) the link never enters L0s/L1, so these have no
  effect on either OS.
- **#11 PCIe config: equivalent.** Both GPUs: gen 3 (8 GT/s) x16, MPS 256,
  MRRS 512 (amdgpu's si_fix_pci_max_read_req_size), ASPM none, CLKPM
  unsupported. Extended Tag enabled on 6:0.0 and its port, disabled on
  2:0.0 and its port (Linux logs "enabling Extended Tags" for both); a
  transaction-tag width does not reach the SMC.
- **#12 Slot: same as Linux.** Both root ports advertise Slot Power Limit
  value 0 / scale 0 with Auto Slot Power Limit enabled, and both GPUs show
  Captured Slot Power Limit 0. Neither illumos nor Linux writes these
  (HwInit fields, set by Apple's firmware; no writer in illumos pciex or in
  Linux drivers/pci), so Linux T2 ran with the same 0 W. Slot 3 (0/2/0,
  the compute GPU) has Power Fault Detected latched (illumos pciehp cannot
  clear it; Linux pciehp clears SLTSTA at init); slot 4 has none, and both
  GPUs behave the same.
- **#13 Apple SMC: nothing GPU-power related readable.** smcstat lists
  the temperature keys (GPU 0/1 die 57/60 C, proximity, memory) and the
  one main fan (F0 837 RPM, target 834, 790-1900). No power-limit key is
  exposed by the read-only driver.
- **Checked on the way (the dump suggested it): the GPU SMC's fan
  control.** CG_TACH_STATUS (0x1dd) reads 0 (no tachometer on the GPU
  modules, the Apple SMC runs the fan) while the FDO fan logic is set up
  ("Internal thermal controller with fan control", StartFanControl in the
  11-12 trace). Same under Linux: radeon and amdgpu both set
  `ucode_fan_control` from the PowerPlay fan table (r600_dpm.c /
  legacy_dpm.c, identical) and start the SMC fan loop identically
  (`si_thermal_start_thermal_controller`), so radeon ramped with the same
  dead tachometer. Not the difference.
- The CG block itself (dpm on, idle) on both GPUs: GENERAL_PWRMGT
  0x00800403, SCLK_PWRMGT_CNTL 0x000f3802, CG_SPLL at L0 (317.5 / 300
  MHz), CG_AT = our L0 thresholds (0x96c / 0x9ca), CG_BSP 0x6978, CG_SSP
  0xc8, CG_THERMAL_CTRL DIG_THERM_DPM 120 C, CG_THERMAL_INT 90/120 C,
  current temperature 62 / 58 C (CG_MULT_THERMAL_STATUS), CG_ULV_CONTROL
  0x1400e070 with ULV off (si_dpm keeps it off on Tahiti, as radeon).
  Nothing here contradicts what the Linux code programs.

**Result:** every difference on the list is now either equal to the
working Linux boot or shown not to reach the SMC. The comparison has run
out: the cause is not among the items two drivers and two OSes visibly
do differently. The two remaining unknowns, in the order I would take
them:

1. **What the SMC itself sees and does under load** (measurement, not a
   change): a debug patch that snapshots the SMC's SRAM data area (soft
   registers, state tables, the firmware's variables; 0x1d000-0x20000)
   at idle and under load and logs which words change. If the SMC's
   activity counters move and its target level stays 0, the decision
   logic is blocked (and the changing words point to the reason); if
   nothing moves, the SMC does not see the GPU's activity. One test boot,
   no behaviour change.
2. Linux amdgpu (not radeon) on this Mac, to split "amdgpu's SI path" from
   "our port": one Linux boot with `radeon.si_support=0
   amdgpu.si_support=1`. Declined by the user; noted, not proposed.

## 11. The SMC SRAM measurement (patch 0026, the user's yes)

Branch `amdgpu/sramdiff` = 3200635 (the con6/fix1 sources) + patch 0026
only (illumos-drm local 6e0efb2; not 0025). Built in the original tree
path: VM `~/src/drm/illumos-drm-0.0.2-amdgpu-sramdiff.p5p`, sha256
320792fd8857885dd04ee4450f918464f6b24a415187d2b02a723d5622124273. Only
the amdgpu module differs from con6 (f241cf95...); linuxkpi 9e80bb26...,
drm_core 563ec129..., drm_helpers 97ac5581..., drm_kms 597d699d... are
byte-identical to con6.

Trigger: every GFX_SCLK sensor read (AMDGPU_INFO_SENSOR; `amdtest <node>
s N` reads it once a second), under adev->pm.mutex, reads SMC SRAM
0x1d000-0x20000 (3072 words) and logs the words that changed since the
previous read (at most 64 lines per read, "illumos: SMC SRAM #<n>
0x<addr>: old -> new"), then a summary line "#<n> diff: K of 3072 words
changed; level index L, GRBM_STATUS 0x...". The first read per GPU is the
baseline. `amdgpu_illumos_si_sram_diff = 0` turns it off.

Run on each node (renderD128, then renderD129):
1. idle: `./amdtest <node> s 10`
2. load: `./gpuburn 40 64 <node> &`, wait 5 s, `./amdtest <node> s 20`
3. idle again: after gpuburn ends, `./amdtest <node> s 10`
then `grep "illumos: SMC SRAM" /var/adm/messages` for that boot to a file.
Reading: words that change in step 2 but not 1/3 are what the SMC
updates with load (activity counters, filtered averages); if the level
index stays 0 while they move, the decision logic is blocked; if nothing
in step 2 differs from step 1, the SMC does not see the load.

## 12. sram1 result (2026-11-14 11:34, patch 0026)

Files: `sram1/` (1492 log lines, amdtest/gpuburn outputs). Per GPU: reads
2-10 idle, 11-30 under gpuburn (GRBM_STATUS 0x00003028 -> 0xe5c00028),
31-40 idle again. Level index 0 in every read.

Where the changing words are (from the firmware header at 0x10000 of
tahiti_smc.bin, big-endian pointer table):

| Area | Header slot | What moves with load (amdgpu1; amdgpu0 alike) |
|---|---|---|
| 0x1da88-0x1dab4 | +0x44, just before DteConfiguration 0x1dab8 | 0x1da88 0 -> 1 during load, stays 1; 0x1da90-0x1da98 rise ~10% (0x3acf3c -> 0x41fd8e), 0x1daa0 falls ~8%; 0x1daa4 0xe8c -> 0x1054 under load only; a rolling pair 0x1dab0/0x1dab4 (index 0..8 in the top byte) |
| 0x1dc0c-0x1dc1c | +0x4c, before PAPMParameters 0x1dc20 = PP_SIslands_PAPMStatus | EstimatedDGPU_T 0x3e -> 0x41 (62 -> 65 C); EstimatedDGPU_P 0xe8b6 -> 0x10587 (59574 -> 66951); dGPU_T_Limit_Exceeded (0x1dc1c) never changes (value not in the log) |
| 0x1de2c-0x1de4c | +0x3c, just before the soft registers 0x1de50 | change only under load: 0x1de30 0x1562 -> 0x3787, 0x1de34/0x1de40 = the 59574 -> 66951 quantity, 0x1de44 0x3a2d80 -> 0x414fd8 (+12%), 0x1de4c 0x23a1 -> 0x2854 (+13%); 0x1de2c 0xd354 -> 0xce00 |
| 0x1fe80-0x1ffec | above the firmware image (ends 0x1f8bc): SMC runtime data | 0x1fe80 the 59574 -> 66951 quantity again; **0x1fec8 0x77aac0 -> 0x13b402e (7.8 M -> 20.7 M, x2.6), back to 7.8 M at idle**; 0x1ff10-0x1ff34 a small rotating set (0x80010000 / 0x000103b4 swapping between 0x1ff24 and 0x1ff30, 0x1ff34 13 -> 9) |

PAPMParameters (0x1dc20) keeps the firmware defaults on both OSes (the
driver writes it only with a VBIOS PPM table; this VBIOS has none):
limits 0xffffffff, T warning 95, hysteresis 5. Nothing restrictive there.

**Result:** the SMC does see the load: its activity-like accumulator at
0x1fec8 more than doubles, its estimated temperature and power rise, a
block next to the DTE configuration and one next to the soft registers
follow the load and fall back at idle. Its level stays 0 in every read.
So the decision to step up is blocked inside the SMC; it is not missing
the activity. Linux T2 ran the same firmware, tables and parameters (all
compared) and stepped up, so the blocking input must be a value the diff
does not show: a word that is constant during the run (a flag, a limit, a
mask), since the diff logs only changes.

**Next measurement (no behaviour change):** one complete dump of
0x1d000-0x20000 (3072 words, 8 per log line = 384 lines) at the first
sensor read and one under load, instead of the diff; then every constant
word can be read and compared with the firmware image's defaults and with
what the driver code writes (soft registers 0x1de50.., the state table
0x1ecf8.., PAPMStatus' limit-exceeded flag, the DTE/CAC areas). A word
that differs from both is the lead.

## 13. The full SRAM dumps (patch 0026b, the user's yes)

Branch `amdgpu/sramdiff` = 6e0efb2 + d4fb739 (patch 0026 extended; still
the con6/fix1 sources, not 0025). Built in the original tree path: VM
`~/src/drm/illumos-drm-0.0.2-amdgpu-sramdump.p5p`, sha256
7da2f45ea0154c4b73ea85a2e73eca4822aca8d2b9d12ef6290558c65f12fc71. Only
amdgpu differs from con6 (f9f34c63...); linuxkpi 9e80bb26..., drm_core
563ec129..., drm_helpers 97ac5581..., drm_kms 597d699d... byte-identical.
Same compiler warnings as sramdiff (636).

What it adds to the diff of section 11 (which stays as it was): two
complete snapshots of 0x1d000-0x20000 per GPU, each taken in one pass
and logged 8 words a line, 48 lines per sensor read (384 lines, so 8
reads):
- a header line "illumos: SMC SRAM dump <n> taken at #<seq> (first read |
  under load): level index L, GRBM_STATUS 0x..., 384 lines follow";
- lines "illumos: SMC SRAM dump <n> 0x<addr>: w0 ... w7".
Dump 1 = the first read (idle), dump 2 = the 10th read that finds
GRBM_STATUS GUI_ACTIVE (bit 31) set, once dump 1 is printed.

Test procedure: exactly that of section 11 (idle 10 reads, gpuburn 40 s
with 20 reads, idle 10 reads, per node). With it, dump 1 prints during
reads 1-8; dump 2 is taken around read 20 and prints during reads 20-27,
while the load still runs.

Evaluation: `sram-dump-parse.py <log> tahiti_smc.bin` checks both dumps
per GPU are complete, lists dump 1 -> dump 2 changes, every word inside
the firmware image (0x1d000-0x1f8bc) that differs from the image (what
the driver or the firmware wrote after upload), and the two GPUs' dump 1
where they differ.

## 14. sram2 result (2026-11-14 13:38, patch 0026b)

Files: `sram2/` (sram-lines.txt 3071 lines, 1540 dump lines; amdtest and
gpuburn outputs). BE amdgpu-sram2 = clone of con7 (con6 + applesmc fan
presets) + the sramdump p5p. The Mac's tahiti_smc.bin = linux-firmware's
(sha256 6fa1ebd7...). Both dumps of both GPUs complete (3072 of 3072
words); dump 1 at read #1 (GRBM_STATUS 0x00003028), dump 2 at read #20
(0xe5c00028, under gpuburn); level index 0 in both.

Note on the image: amdgpu's tahiti_smc.bin has a 256-byte little-endian
header (ucode_size 0xf8bc, ucode_array_offset 0x100, ucode_start_addr
0x10000); sram-dump-parse.py reads it. The image ends at 0x1f8bc, exactly
the end of the 16 dpmLevels of the state table.

Firmware header (0x10000, big-endian): softRegisters 0x1de50, stateTable
0x1ecf8, fanTable 0x1dd40, CacConfigTable 0x1d178, mcRegisterTable
0x1e7b4, mcArbTable 0x1dd64, spllTable 0x1df94, DteConfiguration 0x1dab8,
PAPMParameters 0x1dc20; unnamed slots +0x20 0x1dcac, +0x2c 0x1fa38, +0x34
0x1df7c, +0x3c 0x1de2c, +0x44 0x1da88, +0x4c 0x1dc0c (PAPMStatus).

Every word that differs from the image lies in a table the driver
uploads (CAC/leakage LUT 0x1d178-0x1d594, DTE, fan, MC arb, soft
registers, SPLL table 0x1df98-0x1e790, MC register table, state table)
or in the runtime areas found in sram1. Nothing outside those.

State table (both GPUs, identical in dumps 1 and 2):
- systemFlags 0x55 = GPIO_DC | GDDR5 | REGULATOR_HOT |
  REGULATOR_HOT_PROG_GPIO; extraFlags 0x02 = AC2DC action
  GOTODPMLOWSTATE (0) | AC2DC_GPIO5_POLARITY_HIGH; thermalProtectType 0;
  maxVDDCIndex 20 (amdgpu1) / 15 (amdgpu0).
- dpm2Params TDP 129000, NearTDP 87000, SafePower 69000.
- driverState levelCount 4, the levels as in the dpm logs: sclk 317.5
  (amdgpu1) / 300 (amdgpu0), 450, 650, 725 MHz; aT (CG_R low half, CG_L
  high half) 0x0000096c, 0x04d00fe4, 0x093512c8, 0x16bbffff; bSP 0x6978,
  top level pbSP 0x4a4cb; L0 stateFlags 0x40 (DEEPSLEEP_BYPASS); dpm2 MaxPS
  0x4b/0x50/0x1c. Consistent with si_populate_smc_t().
Soft registers: as the driver writes them (delay_vreg 0x6978, delay_acpi
0x62e08, mclk_chg_timeout 0x2932e0, mc_block_delay 0xaa, ticks_per_us 27,
watermark_threshold 0x8d, non_ulv_pcie_link_width 16, vr_hot_gpio 6;
crtc_index and mclk_change_block_cp_min/max 0).

**What this leaves:** nothing in the SMC's memory is out of place, and
the only inputs to the level decision that are not in its memory are two
board signals the state table tells it to watch:
- GPIO_DC with AC2DC action GOTODPMLOWSTATE and GPIO5 polarity high
  (from VBIOS platform caps HARDWAREDC): if GPIO 5 says DC, the SMC goes
  to its lowest DPM level and stays there. That is exactly the symptom
  (L0 under load, a forced level refused).
- REGULATOR_HOT on the programmable GPIO 6 (vr_hot_gpio 6, caps
  REGULATOR_HOT): if GPIO 6 says the regulator is hot, the SMC throttles
  to its lowest level.
Linux radeon programs the same flags (from the same VBIOS) and never
sends PPSMC_MSG_RunningOnAC (its si_notify_hw_of_powersource is #if 0),
and it stepped up, so there the SMC read both pins as "AC, not hot".
amdgpu sends RunningOnAC only from amdgpu_pm_acpi_event_handler (an ACPI
AC event), never on illumos; with HARDWAREDC the comment in
si_notify_hw_of_powersource says "SMU automatically notices DC, but needs
to be notified of AC".

**Next measurement (read-only, decides it without Linux):** read the
GPU's GPIO pad registers on both GPUs: GPIOPAD_STRENGTH/MASK/A/EN/Y
(mmGPIOPAD_* 0x05e5-0x05e9, byte offsets 0x1794-0x17a4), idle and under
load. Bit 5 of GPIOPAD_Y high (polarity high) = the SMC sees DC; bit 6 =
what it sees for regulator hot. If bit 5 reads DC: the cause is the AC/DC
input, and the fix is to tell the SMC "AC" (RunningOnAC at dpm enable,
as amdgpu does on an ACPI AC event) or to find why the pin reads DC on
illumos. If both read "AC, not hot": this lead is ruled out.

## 15. The GPIO readout and the RunningOnAC switch (patch 0027, the user's yes)

Branch `amdgpu/gpio` = 3200635 (the con6/con7 sources) + 0f1bb4b (patch
0027 only; no SRAM logging, not 0025). Built in the original tree path:
VM `~/src/drm/illumos-drm-0.0.2-amdgpu-gpio.p5p`, sha256
3313a4312795d75014796ff5a9b27bba110f34fae6195c3f830e139de152c5c8. Only
amdgpu differs from con6 (19807f9e...); linuxkpi, drm_core, drm_helpers,
drm_kms byte-identical. amdtest with mode g: sha256 266fc4b5...
(notes/illumos-drm-amdgpu/amdtest.c).

Patch 0027:
- GPIOPAD_STRENGTH/MASK/A/EN/Y (0x5e5-0x5e9) in si_allowed_read_registers.
- One log line after si_start_dpm: "illumos: si_dpm GPIOPAD STRENGTH ..
  MASK .. A .. EN .. Y ..; HARDWAREDC d, ac_power d".
- `amdgpu_illumos_si_running_on_ac` (default 0). Set to 1, at that point
  with ac_power and the HARDWAREDC cap, PPSMC_MSG_RunningOnAC (0x17,
  ppsmc.h) is sent once and the answer logged ("illumos: si_dpm sent
  RunningOnAC, SMC answer 0x.."). Linux amdgpu sends the same message
  from si_notify_hw_of_powersource() (si_dpm.c, .notify_ac_dc) via
  amdgpu_pm_acpi_event_handler() on an ACPI AC event only; radeon's
  si_notify_hw_of_powersource() is #if 0.

Procedure:
- Boot 1 (switch off, as installed): per node (renderD128, then
  renderD129) `./amdtest <node> g 10` idle; `./gpuburn 40 64 <node> &`,
  5 s, `./amdtest <node> g 20`; then the "si_dpm GPIOPAD" boot lines.
- Boot 2, only if GPIO 5 reads "battery" (Y bit 5 = 1): the same BE with
  `set amdgpu:amdgpu_illumos_si_running_on_ac = 1` in /etc/system; check
  the "sent RunningOnAC" line, then `./amdtest <node> s 10` idle and under
  gpuburn as in dpm6 (sclk, level), and `g` once more.
