# illumos: a CPU that stops during startup hangs the boot without a word

Seen 2026-11-15 on the MacBookPro14,3 (i7-7700HQ, 4 cores / 8 threads,
OI 2026.10 = c893fd03ad):
- the all-CPU boot hangs while the application processors (APs) start,
  and the fans rise;
- the hang was at cpu3, and at cpu7 after `idle_cpu_no_deep_c=1`;
- `boot-ncpus=2` boots.

`notes/macbookpro-2017-drivers.md` 5d/5e has the history.

## 1. What the source says (mp_startup.c, timestamp.c, c893fd03ad)

The control CPU (cpu0, `start_other_cpus()` → `start_cpu()` →
`mp_start_cpu_common()`) and the new CPU (`mp_startup_common()`) go
through five handshakes:

| # | control CPU waits in | target CPU meanwhile | timeout / message |
|---|---|---|---|
| 1 | delay loop on `procset_slave` | real mode → kernel, cpuid prelude/ident | **yes**: "started, but not running in the kernel yet" after 5 s, gives up after 20 s |
| 2 | `tsc_sync_master()`, **interrupts off**, spins on `tsc_sync_go` | `tsc_sync_slave()`, interrupts off | none |
| 3 | `mp_startup_wait(&procset_slave)` (SMT_PAUSE spin) | `ucode_read_rev()` | none |
| 4 | (control CPU: `ucode_locate()`), then `mp_startup_wait(&procset_slave)` | `ucode_apply()`, cpuid basic, ap_mlsetup, PAT, syscall MSRs, `sti`, errata, PCID/XSAVE, cpuid extended/dynamic/resolve, fixups, cpu events, pghw | none |
| 5 | `cpupm_init`, `pg_cpu_init` | `pg_cmt_cpu_startup`, online | — |

So:
- an AP that never reaches the kernel would be reported and skipped;
- an AP that stops anywhere after step 1 leaves cpu0 spinning for ever
  with no message. That is exactly the "silent hang, fans up" seen.

The window holds the TSC sync, the AP's microcode update, and a dozen
setup steps. Which one stops on this Mac cannot be read from the source.
Guessing a fix (deep C-states off, MWAIT off, no microcode, pcplusmp)
would only hide it, so the first patch makes the hang say where it is.

## 2. Patch 0001: report where a starting CPU stops

`0001-mp_startup-report-a-CPU-that-stops-during-startup.patch`
(c893fd03ad; mp_startup.c and timestamp.c are the same in 586678ac1b):
- `mp_startup_common()` records its current step in
  `mp_startup_step[cpuid]`. The steps: cpuid ident, TSC sync, microcode
  revision, microcode update, cpuid basic, mlsetup, interrupts on, errata,
  PCID/XSAVE, cpuid late passes, fixups/cpu events, processor groups.
- The control CPU's handshake waits (new `mp_startup_wait_target()`)
  report after `mp_startup_report` (5 s) with
  `WARNING: cpuN: no progress for 5 s in startup step "…"`, then go on
  waiting (and repeat every 30 s).
- `tsc_sync_master()` spins with interrupts off, so it reports through
  `prom_printf`: `WARNING: cpuN: no answer in TSC sync (GO|DONE) …`.
- `mp_startup_report = 0` (kmdb or /etc/system) turns all of it off.
  Nothing else changes; upstreamable as is.
- Checked: gcc 10 `-fsyntax-only -Wall -Werror` against the full
  illumos-usbroot tree (both files). Not built into a unix yet (the OI VM
  was running a Chromium build).

## 3. Test on the MacBook (user's OK; one boot)

1. Build a unix with the patch (illumos-usbroot, incremental
   `uts/i86pc/unix`, as for the console fix). Put it into a new BE on
   the MacBook (`beadm create oi-apstart`, mount, copy
   platform/i86pc/kernel/amd64/unix, `bootadm update-archive -R`). The
   BE choice and the reboot are the user's.
2. At the loader, choose that BE, `unset boot-ncpus`, `boot -v`.
3. At the hang, wait 10 s and photograph the console. The WARNING line
   names the CPU and the step.

## 4. The fix by step (to be written once the step is known)

| step reported | likely cause | fix |
|---|---|---|
| TSC sync (GO/DONE) | the slave or master stalls in the interrupts-off ping-pong. Possible on a hyper-thread pair, or with an SMI storm from Apple's firmware | bound the loops: give up the sync for that CPU after a timeout and mark the TSC as not synced (`tsc_gethrtime_enable`-style fallback to delta mode) instead of hanging |
| microcode update | late load of 0xf8 (2023-09) on a core whose sibling is already running. Intel requires the sibling to be idle; Linux rendezvous the siblings for late loads | apply the update only on the first thread of a core, and on the sibling only check the revision; or skip AP loads when the BSP got none (`ucode_force_update` semantics) |
| interrupts on / errata / cpuid late | an interrupt or MSR on that core (x2APIC, TSC deadline, PCID) | depends; then retest with `apix_enable/W 0` (kmdb) to split apix from the rest |
| none reported (cpu0 keeps printing nothing) | step 1 (the "started, but not running" path already reports), or cpu0 itself stuck elsewhere | F1-A into kmdb (boot -kdv): `::cpuinfo -v`, `$c` |

The per-boot kmdb switches from 5e stay valid as quick cross-checks:
`tsc_master_slave_sync_needed/W 0`, `idle_cpu_prefer_mwait/W 0`,
`apix_enable/W 0`. They are tests, not the fix.
