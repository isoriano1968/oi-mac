# amdgpu on illumos-drm: development log

Goal: Linux's `amdgpu` (Southern Islands / GCN 1.0 first: the Mac Pro
2013's two FirePro D500, PCI 1002:679e, Tahiti LE: CHIP_TAHITI, tahiti_* firmware)
as an illumos-drm driver next to i915n.
Background and why: `../radeon-on-illumos.md`. No hardware tests for now: compile and link work only.

## The hardware (Linux live DVD, 2026-11-11; `../macpro-2013/README.md`)

- 02:00.0, subsystem 106b:0126: **compute GPU**, no outputs. First
  bring-up target (headless, like i915n's start).
- 06:00.0, subsystem 106b:0125: **display GPU**, efifb at its BAR0
  0x90000000 (1600x900x32), 6 DP connectors. Display last; console
  takeover must find the efifb there.
- Each: BAR0 256 MB prefetchable (VRAM aperture of 3 GB), BAR2 256 KB
  MMIO, I/O BAR, ROM BAR 128 KB (disabled at boot), PCIe 3.0 x16,
  HDMI/DP audio at function .1, MSI limited to 32-bit addresses.
- Linux binds radeon (amdgpu leaves SI to it by default): tahiti
  microcode, VCE, UVD, dpm on both, all ring and IB tests pass. The
  display path there is legacy DCE6; amdgpu's equivalent is `dce_v6_0`
  (not DC), so `kernel_fpu_begin` for DC is not needed for this card.
- apple_gmux is present but there is nothing to switch: no port needed.

## Where the work is

- OI VM, `~/src/drm/illumos-drm-amdgpu`: git worktree of
  `~/src/drm/illumos-drm`, branch `amdgpu/si-bringup`, based on
  `origin/i915n/all-display-paths` (e50b0ce; has misc/drm_kms). Nothing
  committed yet, nothing pushed. The iMac branch `imac21-test` in
  `~/src/drm/illumos-drm` is untouched.
- Linux tree: `~/src/drm/linux-amdgpu`, git worktree of `~/src/drm/linux`
  at v7.2 8d3ae592 (same PIN as i915n), prepared by
  `drivers/amdgpu/fetch-and-patch.sh` (drm_kms' 3 patches + amdgpu's).
- Build output: `~/src/drm/out-amdgpu` (summary.txt, log/).
- `~/src/drm/one.sh <tree path> [n]`: one TU with the build's flags, all
  errors (faster than the full build while fixing headers).
- Copy of the whole work as a patch against e50b0ce (151 files, refreshed after the xarray fix):
  `amdgpu-si-bringup-wip.patch` (here), in case the VM tree is lost.
  It leaves out the firmware binaries (`firmware/amdgpu/`, from Debian's
  firmware-amd-graphics, see `first-boot-test.md`).
- First hardware test: `first-boot-test.md` (plan, Stage 0 done: the p5p
  `~/src/drm/illumos-drm-0.0.2-amdgpu.p5p` from `~/src/drm/package-amdgpu.sh`)
  and `macpro-stage1-check.sh` (Stage 1, read-only, for the Mac).

Commands (OI VM):

    cd ~/src/drm/illumos-drm-amdgpu
    drivers/amdgpu/gen-tus.sh ~/src/drm/linux-amdgpu > drivers/amdgpu/tus.txt
    drivers/amdgpu/fetch-and-patch.sh ~/src/drm/linux-amdgpu
    LINUX=~/src/drm/linux-amdgpu OUT=~/src/drm/out-amdgpu JOBS=12 \
        bash drivers/amdgpu/build.sh compile

A full compile takes under a minute at -j12. Check that no other heavy build
runs on the VM first (one at a time across both VMs).

## drivers/amdgpu (new, modelled on drivers/i915)

| File | What |
|---|---|
| `PIN` | Linux v7.2 8d3ae592, as i915n |
| `gen-tus.mk`, `gen-tus.sh` | evaluate Linux's own amdgpu Kbuild Makefiles with GNU make for the illumos configuration (`CONFIG_DRM_AMDGPU_SI=y`; no CIK, DC, KFD, ACP, ISP, HMM, compat, ACPI, switcheroo) and write `tus.txt`; plus the carried TTM (10 TUs: Linux's 12 minus `ttm_bo_vm.c`, `ttm_backup.c`), GPU scheduler (4), `drm_exec`, `drm_suballoc`, `drm_buddy` + `drivers/gpu/buddy.c` |
| `tus.txt` | generated: 403 TUs (amdgpu incl. pm/ and ras/, 18 carried, 1 illumos) |
| `config.h` | CONFIG_* by hand for now (to be generated like i915's) |
| `fetch-and-patch.sh` | i915n's script cut down: pinned tree, series, MIT replacements, licence check, mit-include |
| `mit-headers.txt` | 90 MIT Linux headers copied into the tree's `.illumos/mit-include` (i915n's list minus Intel, plus TTM, scheduler, amdgpu uapi, kfd uapi, ...) |
| `build.sh` | compile (-k style, summary.txt, licence check of every Linux file read) and link (`-z defs`, writes unresolved.txt) |
| `illumos/amdgpu_vkms.h`, `amdgpu_vkms_stub.c` | MIT replacement of the GPL-2.0+ virtual display: the IP block fails early_init |
| `illumos/amdgpu_dma_buf_illumos.c`, `amdgpu_illumos_kms.c`, `ttm_backup_illumos.c` | PRIME through the core, fb helpers + amdxcp stubs, no TTM backup (rounds 16-17) |
| `illumos/amdgpu_illumos.c` | the module glue: `_init/_fini/_info`, attach/detach (round 18) |
| `private.mapfile` | kernel symbols reached through layer macros |
| `patches/0001` | `drm_suballoc.h`: include `<linux/wait.h>` |
| `patches/0002` | `amdgpu_ras.h`: include `<linux/workqueue.h>` |
| `patches/0003`-`0008` | see "amdgpu patches" below |

## Licences (measured)

amdgpu, pm/, ras/, TTM, the scheduler, drm_exec, drm_suballoc, the buddy
allocator: MIT. Not MIT, so not built:

- `amdgpu_vkms.c/.h` (GPL-2.0+): stub above;
- `amdgpu_atpx_handler.c` (switcheroo, laptops): config off;
- `drm_gem_ttm_helper.c/.h` (GPL-2.0-or-later): MIT header in
  `linuxkpi/include/drm/drm_gem_ttm_helper.h`, the four functions
  (vmap, vunmap, mmap, print_info) still to be written on the facade;
- `drm_gem_framebuffer_helper.c/.h`: MIT header, functions to do;
- TTM's and the scheduler's kunit tests (not needed).

`check-licences.py` (shared with i915n) now parses top-level SPDX `OR`
alternatives: `(GPL-2.0 WITH Linux-syscall-note) OR MIT` (kfd_ioctl.h,
kfd_sysfs.h) offers MIT and is accepted; `AND` still refuses. i915n's 490
files still pass.

## LinuxKPI additions so far (all MIT, our own)

New headers: `asm/div64.h`, `kunit/test-bug.h`, `drm/drm_gem_ttm_helper.h`,
`drm/drm_gem_framebuffer_helper.h`, and `linux/`: `acpi_amd_wbrf.h`,
`apple-gmux.h`, `console.h`, `dev_printk.h`, `devcoredump.h`,
`dma-fence-unwrap.h`, `efi.h`, `gcd.h`, `hmm.h`, `hwmon.h`,
`hwmon-sysfs.h`, `interval_tree.h`, `ioctl.h`, `iommu.h`, `irqdomain.h`,
`kfifo.h` (real implementation), `kmemleak.h`, `mempool.h` (real),
`memremap.h`, `mfd/core.h`, `pci-p2pdma.h`, `pid.h`, `power_supply.h`,
`reboot.h`, `sched/task.h`, `semaphore.h` (real, on kmutex/kcondvar),
`syscalls.h`, `units.h`, `utsname.h`.

Changed: `list_lru.h` (real list_lru for TTM's pools), `bitmap.h`
(bitmap_read/write), `mm.h` (si_meminfo, fault-retry flags, vmstat no-ops,
page_to_nid, vmalloc), `mmzone.h` (one NUMA node), `sched.h` +
`src/lkpi_sched.c` (group_leader, exit_code), `pgtable.h`
(boot_cpu_data), `gfp.h` (split_page declared), `dmi.h` (dmi_header),
`drm/drm_device.h` (drm_wedge_task_info), `drm/drm_file.h`
(drm_memory_stats).

Stubs that need a decision later (they make features absent, not wrong):
irq domains (no ACP/ISP), hwmon (no sensors in sysfs; a ksensor bridge
would be the illumos way), devcoredump (hang dumps dropped), IOMMU (none
seen), apple-gmux (none detected: check on the Mac Pro), orderly_poweroff
(logged only), EFI (off), HMM/userptr (off).

## Compile progress (2026-11-11)

| Round | OK / TUs | Main blocker |
|---|---|---|
| 1 | 10 / 405 | `linux/irqdomain.h` (301 TUs), `drm/amd_asic_type.h` |
| 2 | 15 / 405 | `linux/units.h` |
| 5 | 19 / 403 | `drm/task_barrier.h` -> `linux/semaphore.h` |
| 10 | 19 / 402 | `linux/kfd_sysfs.h` (licence rule) |
| 12 | 19 / 403 | **no missing headers left**; everything stops in `amdgpu.h` |
| 13 | **349 / 403** | PCIe layer and PM types done (below); 54 left |

## Done: PCI Express layer and PM types (round 13)

`linuxkpi/include/linux/pci.h`, `pci_regs.h`, `pci_ids.h`,
`src/lkpi_pci.c` (all MIT, own code):

- Real, from the device's own PCIe capability: `pci_pcie_type()`,
  `pcie_get_speed_cap()` / `pcie_get_width_cap()` (Link Capabilities 2's
  speed vector, else Link Capabilities; were macros returning 0),
  `pcie_bandwidth_available()` (Link Status, per-lane payload after line
  encoding), `pcie_aspm_enabled()` (Link Control ASPM bits),
  `pci_wait_for_pending_transaction()` (Device Status TRPND, 60 ms as
  Linux).
- Refused, because the layer does not know the upstream path:
  `pci_enable_atomic_ops_to_root()` (-EINVAL; SI does not need atomics,
  KFD does), `pci_reset_function()` (-ENOTTY; a reset would have to go
  through the nexus), `pci_rebar_get_max_size()` (-ENOTSUPP),
  `pci_resize_resource()` (now Linux 7.2's 4 arguments; -ENOTSUPP),
  `pci_store_saved_state()` (NULL), `pci_pr3_present()` (false).
  `pci_bus_for_each_resource()` sees no host-bridge windows, so amdgpu
  keeps the BAR the firmware sized.
- Types: `enum pcie_link_width`, `enum pci_bus_speed`, PCI error recovery
  (`pci_ers_result_t`, `PCI_ERS_RESULT_*`, `pci_channel_io_*`,
  `struct pci_error_handlers`; illumos reports PCIe errors through FMA, the
  handlers are never called), `PCI_ERROR_RESPONSE` / `PCI_POSSIBLE_ERROR`,
  `PCI_EXP_TYPE_*`, `PCI_EXP_DEVCAP2_ATOMIC_*`, `PCI_VENDOR_ID_DELL`,
  `PCI_VENDOR_ID_APPLE`.
- PM: `struct dev_pm_domain`, `suspend_state_t` moved to `pm.h`,
  `PM_HIBERNATION_PREPARE` etc. in `suspend.h` (which now includes
  `syscalls.h`, as Linux's), `TAINT_SOFTLOCKUP`, `emergency_restart()`
  (logged only, as `orderly_poweroff()`; reached through `efi.h` as in
  Linux), `cpu_data(cpu)` (the boot CPU's data, an lvalue),
  `device_add_group()` / `devm_device_add_group()` (no effect, as sysfs),
  `dev_is_removable()` (false).
- Fixed an earlier slip: `drm/drm_device.h` (drm_wedge_task_info) needs
  `<linux/sched.h>` for TASK_COMM_LEN; linuxkpi's own build broke on it.

Checks: linuxkpi compiles and links (`-z defs`, 711 exported symbols) from
this worktree; i915n compiled against it: 292 / 292, as the main build.

## Rounds 14-17 (2026-11-11): every TU compiles, the module links

| Round | OK / TUs | What |
|---|---|---|
| 14 | 385 / 403 | small names, XArray marks, ksets, memory node, i2c, hwmon, mutex arg types |
| 15 | 394 / 403 | PCIe link registers, `struct tm`, fence dedup, memory stats, cgroup region |
| 16 | **403 / 403** | the DRM-core boundary: amdgpu patches 0004-0007, illumos PRIME file |
| 17 | 405 / 405, **link OK** | `-z defs`, 0 unresolved: carried module entry points, `private.mapfile`, 3 drm_helpers exports, illumos files |

`out-amdgpu/amdgpu`: 7.4 MB kmod object. Not a loadable driver yet: no
`_init`/`_fini`/`_info`, no attach glue, no user mappings (see Next).

Round 18 (2026-11-11): the module glue (`illumos/amdgpu_illumos.c`) and
the VBIOS from ACPI VFCT (linuxkpi `acpi_get_table()` on ACPICA, patch
0008). amdgpu 406/406 compile, links with 0 unresolved and now has
`_init`/`_fini`/`_info`; linuxkpi 740 exported symbols, depends on
misc/acpica; i915n 292/292 and links with 0 unresolved; host tests
43/43 (1507 checks); series 11 patches, licence check 0 refused (1439
Linux inputs). Still 709 warnings. Not loaded: no user mappings yet.

Round 19 (2026-11-11): **user mappings of buffer objects**, first stage
(system pages only), and **mmap offsets for TTM objects** (before this,
every user GEM_CREATE would have failed: TTM's `drm_vma_offset_add()` is
-ENODEV in the layer). linuxkpi user-mapping windows, patches 0009 (TTM)
and 0010 (amdgpu), `illumos/amdgpu_illumos_mmap.c`; details below.
amdgpu 407/407 compile, links with 0 unresolved; linuxkpi 746 exported
symbols, depends on misc/drm_core, misc/acpica, misc/gfx_private; series
13 patches, licence check 0 refused.

Checks after round 17, all from this worktree: linuxkpi compiles and
links (738 exported symbols); host tests 43/43; misc/drm_helpers builds
and passes its symbol checks; drm_kms builds; **i915n compiles 292/292
and links with 0 unresolved** against the changed linuxkpi, as the main
build. Backup: `amdgpu-si-bringup-wip.patch` (124 files, against e50b0ce).

### linuxkpi (all MIT, own code), beyond round 13

- **Real implementations:** XArray marks (per-entry mark bits, marked
  searches, `xa_extract()`, an `xa_state` subset: `XA_STATE()`,
  `xas_find_marked()` incl. the first free index of an allocating array),
  radix-tree tags on top; **ksets** (member list, a kset kobject as default
  parent, join on add and leave on delete, `kset_register()`,
  `kobject_set_name()`); `time64_to_tm()`; `dma_fence_dedup_array()`,
  `__dma_fence_unwrap_merge()`; `pcie_get/set_readrq()`, `pcie_get_mps()`,
  `pci_device_is_present()`; `page_is_ram()` (`pf_is_memory()`, added to
  `private.d/mem.txt`); `devm_i2c_add_adapter()`;
  `drm_gem_objects_lookup()`, `drm_prime_sg_to_dma_addr_array()`;
  `memdup_array_user()` / `vmemdup_array_user()`;
  `ratelimit_state_reset_interval()`.
- **One memory node, one zone** of all of physmem (`NODE_DATA`,
  `zone_managed_pages`, `for_each_node`) for TTM's pool sizing; the zone
  type is `struct lkpi_mm_zone` because illumos has a `struct zone`.
  `split_page()`: nothing to split (alloc_pages() has no order > 0).
- **Absent and said so:** HMM pfns (WARN), MMU notifiers (nothing to
  wait for), in-kernel DRM clients (`drm_client_init()` -ENODEV), DMEM
  cgroup regions (NULL), dynamic dma-buf importers (false), I2C client
  devices (-ENODEV), irq chips (never installed), fd lookups (empty),
  WBRF (types only), `pci_dev.rom` (0: no EFI ROM copy), PCI MSI restore,
  wake from D3 (-EINVAL), `topology_num_cores_per_package()` (all CPUs).
- **Header fixes:** `hwmon_temp_label` was a macro and broke amdgpu's own
  `struct hwmon_temp_label` (now Linux's enum); `__ATTR_RO`/`__ATTR_WO`
  no longer set the other callback (amdgpu's attributes have only one);
  mutex lock macros take a `void *` as Linux's functions do (RAS core);
  `i2c.h` includes `acpi.h` and `stat.h` includes `time.h`, as Linux's
  (that is how amdgpu gets `TRUE`/`FALSE` and `struct tm`);
  `module_init()` names can be set per carried module (below).
- **Host tests** (`tests/host`, run on the VM with `SANITIZE=0`: illumos
  clang has no sanitizers, and the harness does not build on Linux even
  at e50b0ce): new `xarray_marks`, `kset_members`, `time64_to_tm`; 43
  tests, 1507 checks, all pass. The harness renames the layer's `struct
  tm`, which clashes with the C library's on the host.

### amdgpu patches and illumos files

| Patch | What |
|---|---|
| 0003 | `amdgpu_virt.c`: no `THIS_MODULE->version` (NULL on illumos) |
| 0004 | the DRM core owns the device file: no Linux fops/release/ioctl/flush; `amdgpu_flush()` and `amdgpu_drm_release()`'s teardown run at the start of `amdgpu_driver_postclose_kms()` in Linux's order; facade version in both driver tables; no fbdev hook, no `show_fdinfo` |
| 0005 | GEM: no `.export/.vmap/.vunmap/.mmap/.vm_ops` (core's), Linux fault path not built; `AMDGPU_GEM_LIST_HANDLES` via `drm_illumos_gem_for_each_handle()`; `amdgpu_gem_force_release()` only warns |
| 0006 | `AMDGPU_FENCE_TO_HANDLE`: sync files via `drm_sync_file_install()`; `GET_SYNCOBJ` and `GET_SYNCOBJ_FD` are Linux's code (the core's `drm_syncobj_get_handle()`/`drm_syncobj_get_fd()`, round 25) |
| 0007 | `amdgpu_display.c`: no fbdev framebuffer |
| 0008 | `amdgpu_bios.c`: the ACPI VFCT reader builds on illumos (ATRM stays off); entries match on the exact bus first, by identity only if none did (otherwise both D500 would load the bus-2 image; meant for upstream too) |
| 0009 | `ttm_bo.c`: the core's mmap offset (`drm_gem_create_mmap_offset()`) instead of `drm_vma_offset_add()`, no `drm_vma_offset_remove()`, and `ttm_bo_unmap_virtual()` calls the driver's `ttm_bo_illumos_unmap_virtual()` |
| 0010 | amdgpu: the object's mapping window and view (`struct amdgpu_bo`: `illumos_window`, `illumos_view_vram`, `illumos_bar_off`, `illumos_remap_pending`), the window freed in `amdgpu_bo_destroy()`; `illumos_umem/mapped/access/movable` in the GEM functions; GTT pages always cached and snooped (the core maps write-back), even for `CPU_GTT_USWC` |
| 0011 | TTM: one DMA binding per tt (`ttm_tt.illumos_dma`, LinuxKPI's `lkpi_dma_map_pages()`) instead of a `dma_map_page()` per page; `ttm_pool_free()`/`ttm_pool_backup()` unmap the tt before freeing pages |
| 0012 | objects registered with the core: `amdgpu_bo_create()` sets `funcs` before `drm_gem_private_object_init()` and checks its result; both driver tables `illumos_max_object_size = U64_MAX`; `ttm_bo_init_reserved()`'s error path drops the core's reference (`drm_gem_object_put()`) instead of `ttm_bo_put()` |
| 0013 | `amdgpu_pci_probe()` gives LinuxKPI amdgpu's PRIME export callbacks (`amdgpu_illumos_prime_init()`) once the DRM device exists |
| 0014 | `amdgpu_kms_driver.illumos_lkms = &drm_kms_illumos_funcs`: mode setting through misc/drm_kms (facade 2.0), as i915n's patch 0012; no console takeover yet |

`fetch-and-patch.sh` re-applies the whole series to the tree cleanly
(17 patches with misc/drm_kms' own, licence check 0 refused).

- **Foreign imports, parts B to D, round 29** (2026-11-11):
  - **B, LinuxKPI** (`lkpi_drm.c`, `linux/dma-buf.h`, new `lkpi/prime.h`):
    - `dma_buf_attach()`/`dma_buf_dynamic_attach()` make a static
      attachment (`struct lkpi_dma_buf_attachment`: a magic word before
      the facade's `struct dma_buf_attachment`, the importing device and
      DRM device from `driver_of_parent()`, the map and sg table). A
      dynamic importer is treated as static (pinned while mapped);
    - `dma_buf_map_attachment()` (with `dmabuf->resv` held, as on Linux)
      calls `drm_illumos_prime_map()` with the device's
      `lkpi_dma_attr` and builds an `sg_table` of DMA segments only (no
      pages), which `drm_prime_sg_to_dma_addr_array()` turns into the
      tt's per-page addresses; `dma_buf_unmap_attachment()` unmaps and
      frees it; the `_unlocked` variants take `dmabuf->resv`;
      `dma_buf_detach()` frees the attachment; `dma_buf_pin()` is 0 (a
      mapping pins already);
    - `drm_prime_gem_destroy()` is Linux's for the layer's attachments
      (unmap, detach, `dma_buf_put()`) and still does nothing for the
      core's own imports;
    - export side: the layer gives the core trampolines
      (`drm_dev_illumos_set_prime_export()`) in `devm_drm_dev_alloc()`,
      before its early registration; they call the callbacks the driver
      sets with `lkpi_drm_set_prime_export(dev, funcs)` (-ENOTSUP until
      then). `lkpi_dma_pages_kva()` gives a tt's kernel mapping (the
      round 20 binding's vmap).
  - **C, amdgpu exporter** (`illumos/amdgpu_dma_buf_illumos.c`, patch
    0013): `amdgpu_illumos_prime_pin()` refuses what Linux's export
    refuses (userptr, `VM_ALWAYS_VALID`) and imports (an sg object has no
    pages), pins the object in GTT (`amdgpu_bo_pin(bo,
    AMDGPU_GEM_DOMAIN_GTT)`, under the reservation the importer holds)
    and returns the tt's kernel mapping; unpin is `amdgpu_bo_unpin()`.
    Patch 0013 registers them from `amdgpu_pci_probe()`.
  - **D, amdgpu importer**: `amdgpu_gem_prime_import()` makes another
    device's buffer a `ttm_bo_type_sg` object on the buffer's reservation
    (Linux's `amdgpu_dma_buf_create_obj()`, GTT only, caching flags from
    another amdgpu object) and attaches with `dma_buf_attach()`;
    amdgpu_ttm.c maps the attachment at bind and unmaps at unbind,
    unchanged. `amdgpu_illumos_gem_access()` refuses CPU faults on sg
    objects (no pages; Linux gives SIGBUS there too).
  - not done: re-exporting an import (its dma-buf would be a new one of
    the import object, which has no pages: -ENOTSUP at map; Linux
    re-exports the original), VRAM peer-to-peer between the two cards,
    move notification.
  - checks: linuxkpi links, 751 exports (`lkpi_drm_set_prime_export`,
    `lkpi_dma_pages_kva` new; "41 compiled" was the `results.txt` race,
    all 42 objects built); amdgpu 16 patches, 407/407 after one fix (the
    glue needed `<drm/ttm/ttm_tt.h>`), 643 warnings as before, links with
    0 unresolved; i915n 292/292, links; host tests 43/43, 1507 checks.
    None of it has run.

- **Foreign imports, part A: the core, round 28** (2026-11-11; DRM core
  change on this branch, for upstream review):
  - `crates/drm-core` (`gem.rs`): `KernelBinding`, a DMA binding of kernel
    memory the GEM state did not allocate, against the device's own node
    (unbound on drop), and `GemCore::bind_kernel(kaddr, size, attr, ctx)`
    (unsafe: the range must stay mapped and locked), with the caller's
    attributes or the device's own (`ENOTSUP` without either). `SgIter`
    now serves both bindings.
  - `modules/drm_core`: `FObj::kernel_memory()` (the core's memory if it
    is one backing covering the object); `Setup.prime_export` and
    `drm_dev_illumos_set_prime_export(dev, funcs)` (before registration;
    `pin` and `unpin` both required); in `ffi/prime.rs`
    `drm_illumos_prime_map(dev, dmabuf, attr, &map)`,
    `drm_illumos_prime_map_nsegs()`, `drm_illumos_prime_map_segs(map, n,
    addr[], len[])` and `drm_illumos_prime_unmap()`. A map holds the
    exporting object (an import's exporter, if the buffer is itself an
    import), pins through the exporter's callbacks when the object has no
    core memory, binds for the importer, and unbinds before it unpins.
    The caller holds `dmabuf->resv` for map and unmap, as Linux's
    `map_dma_buf` rule: the exporter's `pin` runs under it.
  - why the importer passes its DMA attributes: LinuxKPI drivers never
    call `drm_dev_illumos_set_dma_attr()` (their objects are private), so
    the core has none for them; LinuxKPI has them per device.
  - facade: `dma_buf.h` (the five functions and
    `struct drm_illumos_prime_export_funcs`), both mapfiles, and
    `bindings.rs` by hand in bindgen's style (struct, layout asserts,
    declarations). Facade version not bumped, as in round 25.
  - checks: `cargo check`, `cargo clippy -D warnings`, `cargo fmt
    --check` clean; `cargo xtask build drm-module` and the `drm_core` link
    clean, all five symbols exported; amdgpu 407/407, links; linuxkpi
    42/42, links; i915n links (291 of 292 TUs reported: the `results.txt`
    race in i915's `build.sh`, Next item 7); host tests 43/43, 1507 checks.
    Not exercised: nothing calls the new functions yet (B to D).

- **Foreign dma-buf imports, round 27** (2026-11-11): design only.
  - today: the core resolves a dma-buf of this device to its own object
    and gives the driver's `gem_prime_import` only other devices' buffers.
    amdgpu's (`illumos/amdgpu_dma_buf_illumos.c`) refuses those with
    -EOPNOTSUPP; LinuxKPI's `dma_buf_attach()`, `dma_buf_dynamic_attach()`
    and `dma_buf_map_attachment()` are -ENODEV. The core's own import
    object (`fobj.rs` `import_object()`, `IMPORT_FUNCS`) keeps the
    exporter and shows the memory the core allocated for it, so it works
    only for exporters with core memory (`drm_gem_object_init()`, such
    as the core's dumb buffers). Private objects (amdgpu's TTM objects,
    and i915n's, whose memory its `illumos_umem` names) have none, and no
    importer gets DMA addresses for its own device.
  - where it matters on the Mac Pro: two D500s, so PRIME between two
    amdgpu instances (`DRI_PRIME`, render on one card and scan out on the
    other), and any compositor that imports a client buffer from the
    other card. Between the two, Linux uses peer-to-peer over PCIe or,
    without it, moves the buffer to GTT for the export.
  - the design, in four parts:
    - **A. Core: exporter memory and importer DMA** (Rust, new facade
      calls, facade 2.1). An exporting object describes its pages: the
      core's own memory as now, or a new optional
      `drm_gem_object_funcs.illumos_export_pin(obj, &pages, &npages)` /
      `illumos_export_unpin(obj)` that a TTM driver implements. An
      importing driver attaches and maps:
      `drm_illumos_prime_attach(dev, dmabuf, &att)`,
      `drm_illumos_prime_map(att, &addrs, &n)` (the core binds the pages
      for the importer's node with the device's DMA attributes, one handle
      per mapping as round 20's TTM binding does),
      `drm_illumos_prime_unmap()`, `drm_illumos_prime_detach()`.
      Attachments pin (Linux's static attachments): no move
      notification.
    - **B. LinuxKPI** (`lkpi_drm.c`, `linux/dma-buf.h`):
      `dma_buf_attach()`/`dynamic_attach()` on A's attach (a dynamic
      importer is treated as static: pinned), `dma_buf_map_attachment()`
      builds an `sg_table` with A's DMA addresses (no CPU pages; amdgpu
      only reads `sg_dma_address()` through
      `drm_prime_sg_to_dma_addr_array()`), unmap and detach.
    - **C. amdgpu as exporter** (`illumos_export_pin`): pin the BO in GTT
      (`amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT)`, as Linux's
      `amdgpu_dma_buf_map()` does for an importer without peer-to-peer),
      populate the tt and hand out its pages; unpin on the last detach.
      VRAM peer-to-peer between the two cards: not in this design.
    - **D. amdgpu as importer**: the import half of Linux's
      `amdgpu_dma_buf.c` (`amdgpu_dma_buf_create_obj()`, a
      `ttm_bo_type_sg` object, the tt taking the sg table at bind),
      built from the Linux file or carried in our glue; the export half
      stays the core's.
  - order and size: A first (the core's interface; the illumos-drm maintainer's design
    decisions: names, facade version, pin semantics), then B, then C and
    D together. A is the largest; each part can be built and linked on
    its own, but none of it can be tested without hardware.

- **Task references, round 26** (2026-11-11; LinuxKPI):
  - the problem: a LinuxKPI task is one illumos thread's (thread-specific
    data, freed by the tsd destructor when the thread exits), and
    `get_task_struct()`/`put_task_struct()` counted nothing.
    `amdgpu_ttm_tt_set_userptr()` keeps `current->group_leader` (here
    the calling thread's own task) in `gtt->usertask` for the object's
    life and later reads `usertask->mm`, which after that thread's exit
    would be freed memory;
  - today it is not reachable: userptr is off (no
    `CONFIG_DRM_AMDGPU_USERPTR`), so `AMDGPU_GEM_USERPTR` fails at
    `amdgpu_hmm_register()` (-ENODEV) and the object is released with
    only `put_task_struct()`, which never dereferenced the task;
  - the change (`linux/sched.h`, `lkpi_sched.c`, `lkpi_kthread.c`):
    `task_struct.lkpi_refs` (Linux's usage count) and `lkpi_release`.
    The owner holds the first reference: the thread's tsd slot (the
    destructor drops it at thread exit) or the kthread record
    (`kthread_stop()` drops it after destroying the task queue). The
    last `lkpi_task_put()` (`put_task_struct()`) frees the task, or the
    whole kthread record. The interrupt placeholder has no release
    (its reference is never dropped; a last put would warn).
    `get_task_struct()` is an atomic increment;
  - checks: linuxkpi 42/42, 749 exports (`lkpi_task_put` new), selftest
    links; amdgpu 407/407, 643 warnings as before, links with 0
    unresolved (references `lkpi_task_put`); i915n 292/292, links; host
    tests 43/43, 1507 checks.

- **Sync objects for drivers, round 25** (2026-11-11; DRM core change):
  - what amdgpu needs: its submission path (`amdgpu_cs.c` syncobj in/out
    chunks, timeline points; `amdgpu_gem.c` VA timeline) uses
    `drm_syncobj_find/find_fence/replace_fence/add_point/put/create`,
    which the facade already had (1.1). Missing were the two that give a
    driver-made sync object to user space, `drm_syncobj_get_handle()`
    and `drm_syncobj_get_fd()`, used by `AMDGPU_FENCE_TO_HANDLE`
    (`GET_SYNCOBJ`, `GET_SYNCOBJ_FD`; radeonsi's fence export). Patch
    0006 refused those two with -EOPNOTSUPP;
  - core (`crates/drm-core`): `SyncobjInner::priority()`; in
    `syncobj_ioctl.rs`, `SyncCore::syncobj_install()` (the file's handle
    table, as `SYNCOBJ_CREATE`) and `SyncCore::export_syncobj()` (an
    anonymous sync object file, close-on-exec, as `HANDLE_TO_FD`). Both
    refuse (`ENOTSUP`) a sync object whose fence domain has another
    interrupt priority than the device's, the rule `SyncCore::lookup()`
    applies to imports: its waiters' locks are created at the domain's
    priority. `drm_syncobj_create()` makes objects in the module's
    domain, which has no priority, and LinuxKPI drivers set none for
    their devices (`lkpi_drm.c`), so the two agree for amdgpu;
  - facade (`modules/drm_core/src/ffi/syncobj.rs`, `drm_syncobj.h`, both
    mapfiles): `drm_syncobj_get_handle(file, syncobj, &handle)` and
    `drm_syncobj_get_fd(syncobj, &fd)` with Linux's prototypes; the
    descriptor comes from the ioctl's frame (`ctxt::with_current`), as
    `drm_sync_file_install()`'s does. `bindings.rs` got the two
    declarations by hand in bindgen's style (no bindgen on the VM;
    rerunning `tools/facade/generate.sh` should give the same). Facade
    version not bumped (function additions only, no layout change; a
    driver that uses them fails to load on an older core with an
    unresolved symbol). Whether that wants 2.1 is the illumos-drm maintainer's call;
  - patch 0006 keeps only the sync-file case as an illumos variant;
  - checks: `cargo xtask build drm-module`, the `drm_core` link (both
    symbols exported), `cargo check`, `cargo clippy -D warnings` and
    `cargo fmt --check` clean. amdgpu 407/407, links with 0 unresolved
    and references both. i915n 292/292, links. LinuxKPI host tests
    43/43. `cargo test -p drm-core -p drm-facade` does not link on
    illumos (the `ddi` crate then wants the kernel's `getf`, `nvpair_*`,
    ...; it is meant for another host) and was not run: the Linux workstation has no
    Rust.

- **Write-combined GTT, round 24** (2026-11-11): looked into, not done;
  GTT stays write-back for the CPU and snooped for the GPU (patch 0010).
  - Linux maps a `CPU_GTT_USWC` object write-combined for the CPU and
    unsnooped for the GPU, and first makes the kernel's own direct-map
    alias of each page WC too (`set_pages_wc()`), because x86 does not
    allow one page with two memory types (a write-back alias can pull
    lines into the cache by speculation). Without `CONFIG_X86_PAT`,
    `amdgpu_bo_support_uswc()` is false, and Linux runs amdgpu exactly as
    we do: every GTT page cached and snooped. That is the `#warning` in
    `amdgpu_object.c`, and it is correct, only slower for large CPU
    uploads through GTT. CPU-visible VRAM is already WC (round 21).
  - What WC GTT would need on illumos, in order:
    1. **The kernel's aliases.** amd64 illumos maps all memory in segkpm
       write-back, and our user-mapping window (round 19) and
       `lkpi_dma_map_pages()`'s vmap are write-back kernel mappings too.
       There is no DDI interface to change the type of a kpm page
       (`asm/set_memory.h`: `set_pages_*()` do nothing). Without one, a WC
       user mapping of a RAM page is a mixed-type alias.
    2. **The core's setup.** `GemCore::setup()` (`gem.rs`) calls
       `devmap_umem_setup()` with flags `DEVMAP_ALLOW_REMAP` or
       `DEVMAP_DEFAULTS`, so the cache type is always the default
       (cached). `devmap_umem_setup()` itself accepts
       `IOMEM_DATA_UC_WR_COMBINE` (`i_ddi_cacheattr_to_hatacc()`), and the
       x86 HAT makes `HAT_MERGING_OK` a PAT write-combining PTE for RAM
       too (`hati_mkpte()`: `PT_NOCACHE` plus the PAT bit). So the core
       would need a per-object cache type, e.g. a facade callback or flag
       next to `illumos_umem` (a facade minor version).
    3. **Remaps.** `devmap_umem_remap()` refuses every flag ("No flags
       supported for remap yet", `seg_dev.c`) and remaps cached. Round
       21 switches a piece between VRAM and the RAM window with it, so a
       WC object would come back cached after it was in VRAM. That needs
       an illumos change, or no remap for WC objects.
  - For the bring-up, write-back and snooped is coherent and needs none
    of this. WC GTT is a performance step to measure on the Mac Pro first:
    radeonsi's streaming uploads (staging and upload buffers) are the GTT
    buffers that Linux maps WC, so those are where it would show.

- **Compiler warnings sorted, round 23** (2026-11-11): of 710 warnings
  (262 different), none showed a bug of ours or one that Linux does not
  have too.
  - **Width only, illumos types (190 different, 228 lines):** every `-Wformat` warning is `%ll[xudX]` against `u64`,
    `uint64_t`, `__u64`, `unsigned long` or `s64`. On illumos these are
    `(unsigned) long`, on Linux `long long`; both are 64 bits on amd64,
    so the printing (and the 14 `sscanf` `u64 *` cases) is correct. Not
    fixable in the layer: `uint64_t` is illumos's own type, and Linux code
    mixes it freely with `u64`, so `u64` must stay the same type as
    `uint64_t`.
  - **Same cause:** 7 `-Wchar-subscripts` (illumos's `int8_t` is plain
    `char`, Linux's is `signed char`; `xcp_id` is checked against -1 first).
  - **Linux's own build switches these off** (`scripts/Makefile.warn`,
    W=0 with GCC): 33 `-Wmaybe-uninitialized`, 8
    `-Wunused-const-variable`, 2 `-Wunused-but-set-variable`, 2
    `-Warray-bounds`, 1 `-Wdangling-pointer`. Checked anyway where they
    point at our headers or at code on the SI path. The three in
    `amdgpu_gem.c`/`amdgpu_vm.c` are inside `drm_exec_until_all_locked()`,
    whose body always runs once (`drm_exec_init()` sets the dummy
    contention, so the first `drm_exec_cleanup()` returns true). The
    `bug.h`/`io.h`/`device.h`/`drm_facade_types.h` ones are upstream
    variables in our macros' expansions (`WARN_ON`, `writel`, `dev_err`,
    `IS_ERR`). The array-bounds one is NPS code for chips with IP
    discovery (not SI). The unused-const ones are debugfs, PM and
    switcheroo tables whose users are not built, plus
    `amdgpu_device.c`'s tentative `amdgpu_kms_driver`. The unused `i` in
    `amdgpu_device_resize_fb_bar()` is our `pci_bus_for_each_resource()`
    stub, which makes the function return before resizing (SI has no
    resizable BAR).
  - **Our stub macros (12 `-Wunused-value`):** LinuxKPI value macros used
    as statements (`set_pages_wb`, `dma_set_max_seg_size`,
    `pci_wake_from_d3`, `pm_runtime_resume`/`autosuspend`,
    `unregister_pm_notifier`, `sysfs_update_group`/`add_file_to_group`,
    `vga_switcheroo_register_client`/`init_domain_pm_ops`,
    `get_task_struct`). Now static inline functions with Linux's
    prototypes (the arguments are type-checked as on Linux);
    `dma_set_max_seg_size()`/`dma_set_seg_boundary()` return void, as in
    Linux since 6.11.
  - **Left as they are:** 3 `-Wunused-variable` (our debugfs macros drop
    their arguments, so the `minor`/`eeprom` locals of the debugfs
    functions go unused); 2 `-Wparentheses` in illumos's `sys/ioccom.h`;
    DC's `ASSERT` redefining illumos's `sys/debug.h` one (384 lines, one
    per DC TU; DC code gets DC's, as on Linux); the `#warning` asking for `CONFIG_MTRR`/`X86_PAT`
    (`amdgpu_bo_support_uswc()` is false, which matches patch 0010's
    cached GTT).
  - **Found, not a compiler issue:** `get_task_struct()`/`put_task_struct()`
    count nothing, and `amdgpu_ttm_tt_set_userptr()` keeps the task (Next,
    item 8).
  - `drivers/amdgpu/build.sh`: Linux's W=0 set (`-Wno-maybe-uninitialized
    -Wno-unused-but-set-variable -Wno-unused-const-variable
    -Wno-dangling-pointer -Wno-array-bounds`) for the Linux files only;
    our `illumos/*` glue keeps those warnings. `results.txt` is written
    with `>>` after one truncation (O_APPEND: no lost lines). The summary
    now says `warnings: N (M different)` and lists the kinds.
  - Result: **643 warnings (204 different)**, all of the kinds above:
    384 lines of the ASSERT redefinition, 228 format (190 different),
    20 parentheses (2 different), 7 char-subscripts, 3 unused-variable
    and the `#warning`. Any other kind in the
    summary is new and worth reading. amdgpu 407/407, links with 0
    unresolved; linuxkpi 42/42, 748 exports, selftest links; i915n
    292/292, links with 0 unresolved (its own build still reports 14
    "statement with no effect" from stubs i915 uses that amdgpu does not);
    host tests 43/43, 1507 checks.

- **Objects and the core's limits, round 22** (patch 0012):
  - the limits: `illumos_max_object_size` (default 256 MiB) is checked
    for every GEM object, private ones too (`ffi/gem.rs` `init()`, before
    it looks at the memory); `illumos_max_memory` (default 1 GiB) is the
    budget of the memory the core allocates (`GemCore::reserve`), and a
    private object's `FObj` has 0 bytes, so TTM's objects never count.
    amdgpu's dumb buffers are its own (`amdgpu_mode_dumb_create`), not
    the core's `dumb_create_default`. So only the object size limit
    applied: lifted to `U64_MAX` in both tables;
    `amdgpu_bo_validate_size()` limits an object to the size of its
    domains, as on Linux. The other ceiling is the core's mmap offset
    space (64 GiB, `offsets.rs`), far above 3 GB of VRAM plus GTT;
  - **found on the way, worse than the limit:** `amdgpu_bo_create()`
    called `drm_gem_private_object_init()` before setting
    `bo->tbo.base.funcs` and ignored its result (void on Linux). The core
    refuses an object without `funcs` (EINVAL), so every amdgpu object
    would have had no core state: user objects would fail at
    `drm_gem_create_mmap_offset()`, and kernel objects would go on with an
    unregistered `dma_resv` and could never be freed (`drm_gem_object_put()`
    ignores objects that are not the core's). Now `funcs` comes first and
    a refused object is freed (`kvfree`) with the error returned; the core
    calls no `free` for an object it never armed (`FObj::release`);
  - `ttm_bo_init_reserved()`'s error path called `ttm_bo_put()`, which
    ends in `amdgpu_bo_destroy()` and `kvfree()` while the core still
    holds the object. It now drops the core's reference; the core calls
    `amdgpu_gem_object_free()`, which calls `ttm_bo_fini()` →
    `ttm_bo_put()` as before. Every TTM object in this build is
    amdgpu's and registered with the core first;
  - not changed: TTM's ghost objects (`ttm_buffer_object_transfer()`)
    copy the GEM structure, `__core` included, and get their own
    `dma_resv_init()`; they are only `ttm_bo_put()` (never a GEM
    function), so the copied core pointers stay unused. Worth a look in
    the first run with pipelined moves.

- **TTM pages and DMA, round 20** (patch 0011, linuxkpi `<lkpi/dma_pages.h>`):
  - amdgpu's pool is the plain one: `need_swiotlb` is false (`drm_need_swiotlb()`
    sees no I/O memory above 2^40 in the layer's `iomem_resource`) and
    `dma_addressing_limited()` is false, so no `dma_alloc_coherent()` pool
    and no DMA32; SI's 40-bit mask covers the Mac Pro's RAM (at most 128 GB);
  - LinuxKPI's `alloc_pages()` fails every order above 0, so TTM falls
    back order by order (10 failed calls per tt, cheap) and gets order-0
    kmem pages; `set_pages_array_*()` are no-ops (CONFIG_X86 is set);
  - **the problem:** TTM then `dma_map_page()`s every page, and every
    layer mapping is one DDI handle; rootnex's `rootnex_dmahdl` buffers are
    **2600 bytes** (65 preallocated cookies, windows, copy-buffer state;
    `::kmem_cache` on the VM), so 63% of a GTT object's size again, plus a
    record in the layer's single-mapping tree;
  - **the fix:** the pool maps no page itself; when a tt is fully
    allocated, `lkpi_dma_map_pages()` vmap()s its pages into one heap range
    (write-back), binds it with one handle (DDI_DMA_SLEEP, no copy buffer,
    the reachability check as for every streaming mapping) and fills
    `dma_address[]` page by page from the cookies; `ttm_pool_free()` and
    `ttm_pool_backup()` unmap it before any page is freed. Cost: one handle,
    a cookie array (16 bytes per discontiguous page at most) and 8 bytes of
    page table per page;
  - still per page: amdgpu's dma-buf exports and userptr objects go through
    `dma_map_sgtable()`, one handle per virtually contiguous run (a page,
    for kmem pages). Not needed for the headless bring-up.
- linuxkpi (round 20): `lkpi_dma_map_pages()`/`lkpi_dma_unmap_pages()` in
  `src/lkpi_dma.c` (released at the layer's fini like other leftovers);
  748 exported symbols.

- **VRAM through BAR 0, round 21** (`illumos/amdgpu_illumos_mmap.c`,
  patch 0010 extended, `devmap_devmem_remap`/`devmap_umem_remap` in
  `private.mapfile`):
  - every user mapping piece of an object shows the object's **view**:
    the RAM window (umem, cached) or **BAR 0 at the object's VRAM offset,
    write-combined** (`devmap_devmem_remap()`, `DDI_MERGING_OK_ACC`,
    register set `pdev->lkpi_regnum[0]`; BAR 0 is the VRAM aperture);
  - why not BAR frames in the umem window: `devmap_umem_remap()` refuses
    any cache flags (seg_dev.c: "No flags supported for remap yet"), and
    the core sets up umem with flags 0 = `IOMEM_DATA_CACHED` =
    `HAT_UNORDERED_OK` (its `DDI_STRICTORDER_ACC` does nothing on x86:
    `i_ddi_devacc_to_hatacc()` is empty). A BAR frame would get a
    write-back PTE, and its real type would be whatever the firmware's
    MTRRs say for the BAR (uncached if they cover it, write-back if not);
  - a devmem piece maps one contiguous range, so only objects in
    **contiguous, CPU-visible VRAM** show VRAM. A fault on an object in
    VRAM that is not (`res->placement & TTM_PL_FLAG_CONTIGUOUS`,
    `amdgpu_res_cpu_visible()`) validates it with VRAM contiguous below
    the visible limit (`TTM_PL_FLAG_DESIRED`: no eviction, as Linux's
    `amdgpu_bo_fault_reserve_notify()`) or else GTT, and sets
    `CPU_ACCESS_REQUIRED` as Linux does. TTM compares the contiguous flag
    (`ttm_resource_compatible()`), so a scattered object really moves.
    Pinned objects in such VRAM: EFAULT (Linux: SIGBUS); pinned objects
    in contiguous visible VRAM are now mapped;
  - the view changes only in `illumos_access`, with the reservation held,
    inside `drm_illumos_gem_foreach_mapping()` (the core's mapping lock),
    in the walk that remaps every piece. The core's `mapped` hook runs
    for every new piece (mmap, fork, split at its first fault) with the
    mapping lock held: fork and split pieces copy a piece that shows the
    view; an mmap piece shows the window, so while the view is VRAM
    `mapped` sets `illumos_remap_pending` and the next fault remaps
    (it cannot remap itself: the VRAM offset is only stable under the
    reservation). Every fault passes through `illumos_access` before a
    translation is loaded (segdev calls `devmap_access` for every fault,
    and drops the handle lock first, so remapping there is allowed);
  - moves unload all pieces and the window as before; the view stays and
    the next fault compares it with where the object is (offset too).
  - Not done: write-combined GTT (the core's umem attributes), and the
    core's 256 MiB object / 1 GiB device limits for 3 GB of VRAM
    (done in round 22).
- **User mappings, round 19** (`illumos/amdgpu_illumos_mmap.c`): the
  core maps only umem cookies, write-back, and calls `illumos_access`
  for every fault of a movable object (without its mapping lock;
  `illumos_umem`/`illumos_mapped` run with it). So:
  - each mapped object gets one **user-mapping window** at its first
    mmap(2): kernel heap VA with a gfx_private umem cookie
    (`gfxp_umem_cookie_init()`, KMEM_NON_PAGEABLE: at each fault the
    devmap framework maps the frame the kernel VA maps then). All pieces
    of all mappings map that cookie; none is ever remapped;
  - a **fault** reserves the object, moves it to GTT if it is elsewhere
    (VRAM; pinned objects fail with EFAULT), waits for the kernel fences
    (the move), populates the TT pages, loads the window with them if
    needed and loads the faulting range, all under the reservation;
  - **TTM's `ttm_bo_unmap_virtual()`** (moves, swap-out, purge; patch
    0009) unloads every mapping piece (`drm_illumos_mapping_unload()`),
    then the window. Lock order: reservation, then the core's mapping
    lock.
  - Cost of this first stage (lifted for contiguous visible VRAM in
    round 21): a CPU-mapped object the GPU wants in VRAM
    moves between VRAM and GTT; GTT is cached and snooped, never WC.
    **Next stage (round 21): VRAM mapped directly**, `devmap_devmem_remap()` of BAR 0
    write-combined in `illumos_access`; that needs per-piece state (which
    piece points where), because a remap unloads the whole piece.
- linuxkpi (round 19): **user-mapping windows** `<lkpi/umem_window.h>`
  (in `src/lkpi_page.c`, reusing vmap's frame loader: create, cookie,
  load write-back, unload, destroy); `private.d/gfx.txt`
  (`gfxp_umem_cookie_init/destroy`); the layer links `-N misc/gfx_private`
  (746 exported symbols).

- `illumos/amdgpu_illumos.c` (round 18): the module glue, modelled on
  i915n's. `_init` checks the facade and the layer, runs the buddy,
  scheduler and amdgpu module_inits in Linux's load order (unwinding on
  failure), looks up the `amdgpu` pci_driver and installs the module;
  attach/detach are `lkpi_pci_attach/detach`, cb_ops `DRM_CB_OPS`, fast
  reboot refused; `_fini` unwinds in reverse and quiesces. No DMA
  attributes or fence priority for the core (i915n sets neither).
- linuxkpi (round 18): `acpi_get_table()`/`acpi_put_table()` on
  ACPICA's `AcpiGetTable`/`AcpiPutTable` (`src/lkpi_acpi.c`; declared
  there because OI installs no ACPICA headers, the header layout checked
  by static_assert), with `acpi_status`, `acpi_size`, `ACPI_SUCCESS` and
  `struct acpi_table_header` in `<linux/acpi.h>`; `private.d/acpi.txt`;
  the layer now links `-N misc/acpica` (740 exported symbols).

- `illumos/amdgpu_dma_buf_illumos.c` replaces `amdgpu_dma_buf.c`: the
  core exports; it calls `gem_prime_import` only for another device's
  buffer, which is refused (-EOPNOTSUPP) so that no core object (not an
  `amdgpu_bo`) enters amdgpu's handle table; a dma-buf this device
  exported resolves to its object; `amdgpu_dmabuf_ops` is empty.
- `illumos/amdgpu_illumos_kms.c`: `drm_gem_fb_destroy()`,
  `drm_gem_fb_create_handle()` (Linux's helper file has no licence
  statement), amdxcp stubs (no compute-partition devices).
- `illumos/ttm_backup_illumos.c`: no TTM backup store (0 bytes
  available, no shmem file), in place of `ttm_backup.c`.
- `private.mapfile`: `_pageshift`, `memcmp`, `ncpus`, `rw_tryenter`,
  `servicing_interrupt` (as i915n's).
- `build.sh`: `buddy.c` and `sched_fence.c` get their own
  `lkpi_module_init_gpu_buddy` / `_drm_sched` entry names (they are
  separate modules in Linux); the glue must call them before amdgpu's.
- `carried/drm_helpers` (MPL-2.0 mapfiles): `drm_mm_print`,
  `drm_memcpy_from_wc`, `drm_need_swiotlb` exported (both mapfiles; the
  module's symbol checks pass). Note: `drm_memcpy_init_early()` is not
  called by drm_helpers, so `drm_memcpy_from_wc()` takes its non-SSE4.1
  path (correct, slower).

## Next: the gaps that are real work

1. ~~Module glue~~: done (round 18); the core's limits in round 22
   (object size lifted; the memory budget does not apply to TTM's
   private objects).
2. **User mappings of TTM objects**: done for the bring-up (round 19:
   system pages through a kernel window; round 21: contiguous visible
   VRAM through BAR 0 write-combined, scattered VRAM made contiguous or
   moved to GTT on a CPU fault). GTT stays write-back and snooped
   (round 24: what Linux does without `CONFIG_X86_PAT`; WC GTT needs a
   core flag, a kernel change to `devmap_umem_remap()`, and a way to
   change the cache type of the kernel's own mappings first). The size
   limits are settled from the core's code (round 22).
3. ~~TTM pages and DMA~~: done (round 20, one DMA binding per tt).
   Still per page: dma-buf exports and userptr (`dma_map_sgtable()`).
4. **VBIOS: needs ACPI VFCT.** On the Mac Pro the VBIOS comes from the
   ACPI **VFCT** table (2026-11-11, live DVD: tables APIC DMAR DSDT FACP
   FACS HPET MCFG SSDT1-8 VFCT; reading the PCI ROM through sysfs gives
   an I/O error on 02:00.0 and 0 bytes on 06:00.0, so no usable ROM BAR
   image; `../macpro-2013/linux-acpi-tables-rom-check.txt`).
   amdgpu's dGPU order (`amdgpu_get_bios_dgpu`): ATRM, VFCT, VRAM copy
   (only if posted), `pci_map_rom`/platform ROM, `si_read_bios_from_rom`
   (MMIO `ROM_INDEX`/`ROM_DATA`), `si_read_disabled_bios`. The last two
   read the same flash as the ROM BAR, so they are unlikely to help here.
   Our build has no `CONFIG_ACPI`, so `amdgpu_acpi_vfct_bios()`
   (`amdgpu_bios.c`, under `#ifdef CONFIG_ACPI`) is the `return false`
   stub. **Done in round 18:** linuxkpi `acpi_get_table()` on illumos
   ACPICA and patch 0008, which builds only the VFCT reader on
   `__illumos__` (ATRM needs ACPI methods: left out).
   **The VFCT holds one 64 KB ATOM image per GPU** (02:00.0 first, then
   06:00.0; subsystem fields 0000:0000; decoded in `../macpro-2013/README.md`,
   the dump `VFCT.bin` stays local, git-ignored). The two images differ in
   31 bytes.
   **Trap in Linux 7.2's `amdgpu_acpi_vfct_match()`:** it accepts an entry
   on vendor+device+slot+function when the bus differs (PCI renumbering),
   and the loop takes the first match. Both D500s are 1002:679e at slot 0
   function 0, so the display GPU (bus 6) gets the compute GPU's image
   (bus 2, first in the table). Patch 0008 tries an exact bus match over
   all entries first and falls back to identity only if none matched.
   A candidate fix to send upstream (amd-gfx) too, the user's decision.
5. ~~Syncobj handles for drivers~~: done in round 25 (core change on
   this branch; for upstream review). Still open: **foreign dma-buf
   imports**: designed in round 27, the core (A) built in round 28,
   LinuxKPI and amdgpu (B to D) in round 29. Not tested (needs two GPUs or
   a second DRM device); re-exporting an import and VRAM peer-to-peer are
   not done.
6. ~~The 709 compiler warnings~~: sorted in round 23, none real; the
   643 left are listed by kind there.
7. `drivers/i915/build.sh` (not ours to change in this round): the
   parallel compile writes `results.txt` through one `>` redirection
   shared by 12 jobs, so lines can be lost (round 20: 291 of 292 TUs
   reported, `gt/uc/intel_gsc_uc.c` missing, though it compiled and the
   module linked). `>>` (O_APPEND) would fix it. `drivers/amdgpu/build.sh`
   had the same pattern (round 20's "406 TUs" was one lost line); fixed
   there in round 23. `linuxkpi/build.sh` has it too (round 29: "41
   compiled" with all 42 objects built).
8. ~~Userptr task references~~: done in round 26 (tasks are counted).
   Userptr itself stays off: it needs `CONFIG_DRM_AMDGPU_USERPTR` (HMM
   and MMU notifiers, which illumos lacks; `pin_user_pages_fast()` with
   `ddi_umem_lock()` exists) and a `task_struct.mm` that names the
   process (always NULL now).

Older list (round 13), now done except where noted above:

- small names: `I2C_AQ_COMB` / `I2C_AQ_NO_ZERO_LEN`,
  `PCI_CLASS_ACCELERATOR_PROCESSING`, `PCI_CLASS_SERIAL_USB_XHCI`,
  `PCI_MSIX_FLAGS`, `PCI_EXT_CAP_ID_ERR`, `HW_ERR`, `FALSE`,
  `memdup_array_user` / `vmemdup_array_user`, `pcie_capability_set_word`,
  `devm_i2c_add_adapter`, `num_possible_nodes`, `d_inode`, `struct tm`,
  `topology_num_cores_per_package`, `struct class_fd_t` (cleanup.h),
  `mutex.h`/`hwmon.h` macro clashes, `acpi_amd_wbrf` types
  (`freq_band_range`);
- sysfs internals: `kobject.sd` (IP-block attribute dirs), `struct kset`
  (`amdgpu_discovery.c`);
- radix-tree tags, `XA_STATE` (RAS bookkeeping, user queues: newer than
  SI, but compiled in);
- real API gaps: dma-buf importer ops (`dma_buf_attachment.importer_ops`,
  `dma_buf_invalidate_mappings`), `drm_file.table_lock` (GEM handles: the
  facade), `drm_device.fb_helper`, `pci_dev.rom` (the VBIOS read:
  `amdgpu_bios.c` needs a real ROM BAR mapping), `hmm_pfn_to_page`,
  `drmm_cgroup_register_region`, `task_struct.tgid`, and:

1. **TTM's memory model**: `pg_data_t`, `NODE_DATA`, `MAX_NR_ZONES`,
   `zone_managed_pages`, `for_each_node` in `ttm_pool.c` (pool sizing per
   node), `split_page()`; then the illumos versions of the two TUs left out:
   `ttm_bo_vm.c` (CPU faults on BOs: devmap(9E), using the R1 refault
   result: `devmap_umem_remap` + `devmap_unload`) and `ttm_backup.c`
   (swap-out; can stay off at first).
2. **GEM on the facade**: amdgpu's BOs are `ttm_buffer_object`s that embed
   Linux's `drm_gem_object`; the facade owns GEM objects (handles, refcount,
   mmap offsets). The i915n patches 0003/0005 show how i915 went through the
   core; amdgpu needs the same for `amdgpu_gem.c`, `amdgpu_object.c` and
   TTM's `ttm_bo` base object. This is the main design question for
   the illumos-drm maintainer (TTM = R5d).
3. Then the link: `build.sh link` lists every symbol neither the DDI, the
   facade, linuxkpi, drm_helpers nor drm_kms provides; that list is the
   remaining linuxkpi/facade work, as R5c measured for i915.

Later: DC for display (SI has DCE 6 in DC since Linux 6.19), firmware
(`amdgpu/tahiti_*.bin` etc. from linux-firmware through `firmload(9F)`,
illumos-drm's `firmware/` directory has the i915 pattern), the module glue
(`_init`/attach like `drivers/i915/illumos/i915_illumos.c`), packaging and
PCI aliases, then hardware.
