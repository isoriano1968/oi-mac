# MacBook Pro 2017 on OpenIndiana: which drivers are missing (2026-11-15)

User report, 2026-11-15: the plain OI 2026.10 text installer **boots on the
MacBook Pro from the USB stick, but the keyboard does not work during the
install**.

**The machine is the 15" with Touch Bar (user, 2026-11-15) = MacBookPro14,3**
(confirm with `smbios`). Its parts:
- Kaby Lake-H CPU, 200-series PCH: the SPI controller is most likely
  0xa2a9/0xa2aa, FreeBSD's "Kaby Lake-H" entry;
- Intel HD 630 and Radeon Pro 555/560 (Polaris, amdgpu VI), behind the
  Apple gmux;
- T1 iBridge with the Touch Bar: no physical Esc or F-keys;
- applespi's touchpad table lists 14,3 as model 0x06;
- no Ethernet port, Wi-Fi BCM43602.

The 13" models (14,1/14,2) are left in the table for reference. Facts marked
**(check)** must be read on the machine itself (section 4).

What the installer run already shows on this machine:
- the loader and kernel start from the stick through a USB-C adapter, so
  `xhci` on the Alpine Ridge works for devices connected at boot;
- the installer screen appears, so **the console on the firmware framebuffer
  works here**. The console fix (issue C) that the Mac Pro needs is not
  needed on this machine for the install.

Sources:
- Linux `drivers/input/keyboard/applespi.c` and
  `drivers/nvme/host/pci.c` (torvalds/linux master);
- FreeBSD `sys/dev/atopcase/atopcase.c` and `atopcase_acpi.c`,
  `sys/dev/intel/spi_pci.c`, atopcase(4), bcm5974(4);
- OpenBSD bwfm(4);
- the Touch Bar patch series on LKML (Ronald Tschalär 2021, Aditya Garg
  2023);
- illumos-gate 1a97ad9a71 (2026-11-09), read locally.

## 1. Why the keyboard is dead

- On every MacBook Pro 2016-2017 (13,x and 14,x) the **internal keyboard and
  trackpad are not USB and not PS/2**. They are the Apple "topcase" on an
  **SPI bus**: ACPI device `APP000D`, behind the PCH's Intel LPSS SPI
  controller.
- In the boot loader the keyboard works, because Apple's EFI firmware has
  its own SPI driver. Once the illumos kernel runs, nothing drives the SPI
  controller: illumos has kb8042 (PS/2) and usbkbm (USB HID) only.
- **illumos has no SPI framework and no LPSS SPI driver at all.** Since 2025
  it has an I2C framework (`i2c/nexus`, ismt, pchsmbus, Mustacchi), but
  nothing for SPI. So the installer sees no keyboard.
- With a Touch Bar (14,2 and 14,3) there is a second problem. Esc and F1-F12
  are on the Touch Bar, which the T1 chip ("iBridge", a USB device) drives.
  Without a driver it shows nothing, and the text installer needs F2/F3
  (continue/back).

**For the install today, use a USB keyboard.** Connect it through a USB-C
adapter or hub (the MacBook has only Thunderbolt 3/USB-C ports), and plug it
in **before power-on**, as with the stick.
- The keyboard will work: the installer already reached its screen. That
  means illumos's xHCI on the Thunderbolt controller (Alpine Ridge) works, at
  least for devices that are connected at boot. The stick and the keyboard go
  through the same xHCI.
- Hot-plug on the USB-C ports is not expected to work. The Alpine Ridge
  powers down when nothing is attached, and illumos has no Thunderbolt
  driver (section 3).

## 2. What the Linux and BSD drivers do for the keyboard and trackpad

| | Linux | FreeBSD | OpenBSD |
|---|---|---|---|
| SPI controller | `spi-pxa2xx` (LPSS) | `intelspi(4)`: `spi_pci.c` has Sunrise Point-LP 0x9d29/0x9d2a (13", Kaby Lake-U), Sunrise Point-H 0xa129/0xa12a, Kaby Lake-H/200-series 0xa2a9/0xa2aa (15") **(check which one the Mac has)**; BSD-2, Val Packett 2021, ~230 lines for the PCI part plus the core | - |
| Topcase protocol (keyboard, trackpad, backlight) | `applespi` (Federico Lorenzi, Ronald Tschalär), ~1,200 lines, **GPL-2.0**: may serve as documentation only, not as code for illumos | `atopcase(4)` (Val Packett, Vladimir Kondratyev, 2023, FreeBSD 14.0): `atopcase.c` ~640 + `atopcase_acpi.c` ~450 lines, **BSD-2-Clause**. Speaks the packet protocol (CRC16, multi-packet messages), fetches the HID report descriptors of the keyboard and the trackpad from the firmware, and passes HID reports to `hidbus`, so the keyboard goes to `hkbd`. Keyboard backlight through SET_REPORT | only `aplhidev` for Apple Silicon; nothing for Intel Macs |
| Trackpad | inside applespi (multitouch input) | `bcm5974(4)` with SPI attachment (2023) on top of atopcase's hidbus child | - |
| Interrupt | ACPI GPE only (`_GPE` → `acpi_install_gpe_handler`) | normal IRQ resource. If the IRQ storms or never fires, it falls back to polling; GPE only when there is no IRQ resource. Comment: interrupts only arrive on MacBookPro12,1 and 14,1 | |
| ACPI methods | `UIST` (USB mode active → leave), `SIST`, `SIEN` (enable SPI), `_GPE`; properties `spiCSDelay`, `resetA2RUsec`, `resetRecUsec` | `_STA`, `UIST`, `SIST`, `SIEN`, `_GPE`; CS delay 10 if unset | |
| `_OSI("Darwin")` | Linux answers Darwin on Apple, so it gets Apple's SPI properties and GPE path | does not answer Darwin; works without it. Optionally `hw.acpi.install_interface="Darwin"`, with side effects | |

Known FreeBSD problems: an IRQ storm on MacBookPro13,1 (bug 283706);
re-attach after an `intelspi` reload; a MacBook10,1 needed extra work. The
driver chain exists and is in use, but it is not polished.

**illumos's ACPICA answers only the Windows `_OSI` strings, not "Darwin"**
(`usr/src/common/acpica/utilities/utosi.c`). Like FreeBSD, we would see the
Windows view of Apple's DSDT. FreeBSD shows that this view is enough.

## 3. All MacBook Pro 2017 parts: Linux/BSD driver, illumos today, work needed

| Part | Hardware | Linux | BSD (licence) | illumos today | Needed | Priority |
|---|---|---|---|---|---|---|
| **Keyboard + trackpad** | Apple topcase on SPI, `APP000D`; LPSS SPI 9d29/9d2a (13") or a129/a2a9 (15") | spi-pxa2xx + applespi (GPL) | FreeBSD intelspi + atopcase + bcm5974 (BSD-2) | **nothing** | see section 5: LPSS SPI driver + topcase driver feeding usbkbm/usbms through a hid-like stream | **1** (without it no usable laptop; for now a USB keyboard) |
| **Touch Bar** (14,2, 14,3) | T1 "iBridge", USB (Apple 05ac:8600 **(check)**), HID; Touch Bar in function-key mode by a HID report | out-of-tree for years (`apple-ibridge` + `apple-touchbar`, Tschalär 2021, Garg 2023); mainline today has `hid-appletb-kbd`/`hid-appletb-bl`, whether these cover T1 is **(check)** | none | the iBridge's HID interfaces should attach to `hid` (USB), the Touch Bar stays dark | a small USB HID client (or a hid quirk) that sets the Touch Bar to the F-key mode, with Fn switching to media keys later | 2 (Esc/F-keys) |
| **Internal SSD** | Apple NVMe **106b:2003** (MBP 2016/17) **(check the ID)** | `nvme`, entry `PCI_DEVICE(APPLE, 0x2003)` by ID, **no quirks** (2001: single vector + QD 1; 2005/T2: 128-byte SQEs etc.) | FreeBSD nvme | `nvme` binds only `pciclass,010802`. If Apple's class differs (that would be why Linux lists the ID), illumos never attaches it, and the installer shows no internal disk **(check: disk list in the installer, `prtconf -v`)** | at most an alias `pciex106b,2003` in `driver/storage/nvme` (or `update_drv -a`), no code if no quirk is needed | **1** for installing to the internal SSD; none for an install to external USB (hubd fix) |
| Wi-Fi | Broadcom **BCM43602** (14e4:43ba, FullMAC, 3x3 ac) **(check)** | brcmfmac + `brcmfmac43602-pcie.bin` (linux-firmware, redistributable) | **OpenBSD/NetBSD `bwfm(4)` PCI**, lists the BCM43602 (ISC licence) | none (no FullMAC driver; net80211 old) | port of bwfm (PCIe msgbuf + firmware load + net80211/mac glue), a project of its own; plus the NVRAM `.txt` (not in OpenBSD's firmware set) | 3 (USB/Thunderbolt Ethernet until then) |
| Ethernet | none built in | - | - | - | USB-C Ethernet with CDC-ECM (`usbecm`) or Thunderbolt Ethernet (BCM57762 = `bge`, connected at boot) **(check which adapter is at hand)** | 1 for "online" |
| Thunderbolt 3 / USB-C | Alpine Ridge (TB3 controller + own xHCI behind PCIe hotplug) | `thunderbolt` driver; on Macs Linux runs the controller itself (no ICM) | none usable | `xhci` works for devices connected at boot (the stick did); no Thunderbolt, no hot-plug when the controller sleeps | for the start: "plug in before power-on". A Thunderbolt driver is a large project | later |
| iGPU | Kaby Lake: Iris Plus 640 (14,1), 650 (14,2), HD 630 (14,3) | i915 | FreeBSD drm-kmod | firmware framebuffer (installer works); `i915n` headless; the display gate admits Ivy Bridge/Haswell only | plan step E: Kaby Lake (gen9.5) in i915n's display gate, eDP, DMC firmware (`system/firmware/i915`, kbl_dmc); Mesa iris | after the unified drm |
| dGPU (15") | Radeon Pro 555/560, Polaris 11 | amdgpu | drm-kmod | - | plan step D (amdgpu VI) | late |
| gmux (15") | panel mux + backlight | `apple-gmux` | none | - | small driver: backlight first (plan step E) | with the 15" |
| Backlight (13") | via the iGPU's PWM | i915 `intel_backlight` | | none without i915n display | with i915n | with i915n |
| Keyboard backlight, ALS | over SPI (topcase) on 2016/17 | applespi; ALS in the iBridge series | atopcase (backlight) | - | in the topcase driver | with priority 1 |
| Sound | Cirrus CS8409 bridge + CS42L83 **(check: codec dump, `audiohd` messages)** | mainline `patch_cs8409` covers Dell models; for MBP 13,x/14,x the out-of-tree `snd_hda_macbookpro` | none | audiohd attaches to the HDA controller; whether the codec plays is unknown | codec dump first; then decide whether it is like our CS4206 GPIO fix or a real CS8409 driver | 3 |
| SMC | same port-I/O SMC (no T2) | applesmc | asmc | **our applesmc** should attach | key list (`smcstat -l`), newer key types (`flt `), Mac Sensors cards (plan section 7) | 2 |
| Battery / AC / lid | ACPI: control-method battery (PNP0C0A) or Smart Battery (SBS) **(check DSDT)** | `battery` / `sbs` | acpi_cmbat / acpi_smbat | `acpi_drv` knows PNP0C0A, ACPI0003 (AC), PNP0C0D (lid) only, no SBS | if SBS: an SBS client over the new I2C/SMBus framework or ACPI SMBus methods | 2 |
| Bluetooth | Broadcom, USB | btusb + firmware | | no Bluetooth stack | - | out of scope |
| Camera | FaceTime HD (Broadcom 1570, PCIe) | out-of-tree `facetimehd` | none | - | - | out of scope |
| Touch ID / T1 security | T1 | none | none | - | - | out of scope |

## 4. To read on the MacBook (installer Shell, with a USB keyboard)

Read-only, from the installer's "Shell" (or after an install). The output
goes to the stick's spare space or a second stick:

```
smbios -t SMB_TYPE_SYSTEM          # model: MacBookPro14,1 / 14,2 / 14,3
prtconf -v > prtconf-v.txt         # all PCI IDs + class codes: SPI (9d29/a129/a2a9?), NVMe 106b:2003 class, Wi-Fi 14e4:43ba, iBridge
prtconf -D | grep -i -B1 -A1 "nvme\|spi\|APP000D"
acpidump -o acpi.dat               # DSDT/SSDTs: APP000D (_CRS: SPI + IRQ/GPE, UIST/SIST/SIEN), battery PNP0C0A vs ACPI0002, gmux
modinfo | grep -i "nvme\|xhci\|audiohd"
format < /dev/null                 # does the internal SSD show up as a disk?
cat /dev/sndstat                   # audio devices seen by audiohd
grep -i "audiohd\|nvme\|xhci" /var/adm/messages   # codec IDs, attach messages
```

These settle the **(check)** items above. The SPI controller's ID and the
`APP000D` `_CRS` (SPI bus speed, mode, IRQ vs GPE) decide the driver design
in section 5.

## 5. Keyboard and trackpad on illumos: a design sketch

Model: FreeBSD's BSD-2 code, with Linux applespi only as documentation of
the protocol.

1. **`lpss_spi`**: a driver for the Intel LPSS SPI controller (PXA2xx-SSP
   compatible registers plus the LPSS private registers: reset, clock
   divider). It attaches to the PCI ID or to the ACPI node, and offers a
   small transfer interface (full-duplex transfer, chip select, CS delay).
   There is no illumos SPI framework to plug into. Either:
   - a private interface between two modules (the way `gfx_private` serves
     its consumers), or
   - a minimal `spi` nexus in the style of the new I2C framework, which
     could also be an upstream topic (**U**).
2. **`atopcase`** (name kept, or `appletopcase`): binds to ACPI `APP000D`.
   - `UIST`/`SIST`/`SIEN` as in FreeBSD;
   - interrupt from `_CRS`, else `_GPE` through ACPICA
     (`AcpiInstallGpeHandler`, available in the kernel), else polling;
   - the packet/CRC16 protocol;
   - fetching the HID descriptors.
3. **Integration into the illumos input stack, the cheapest way**: the
   driver offers one STREAMS minor node per child (keyboard, trackpad) that
   speaks what `usbkbm` and `usbms` expect from `hid(4D)`:
   - input reports as M_DATA;
   - `HIDIOCKMGDIRECT`, `HID_GET_PARSER_HANDLE`, `HID_GET_VID_PID`;
   - SET_REPORT for the caps-lock LED.

   `hidparser` (already a misc module) parses Apple's descriptors. Then
   `consconfig`/`conskbd` treat it as a keyboard like any USB keyboard, with
   console, VTs and X.

   Fn: Apple's Fn bit sits in a vendor field of the report. F-key/media
   switching as in Linux hid-apple `fnmode` is a later step (our open topic
   from the iMac media keys).
4. **Trackpad**: first as a plain relative mouse with click (usbms on the
   report). Multitouch and gestures (bcm5974-style) later, if wanted.
5. **Keyboard backlight**: SET_REPORT from the driver, a small ioctl or
   kstat for a GNOME slider, later.
6. Packaging: a new port `ports/apple-topcase` (`driver/input/apple-topcase`
   + `driver/bus/lpss-spi`), on oi-local, built against the osnet build of
   the medium (c893fd03ad/23344, section 1.5 of oi-apple-installer-eval.md).
   For the installer, the driver must be **in the boot archive of the respun
   medium**, because the keyboard is needed before the "Install Additional
   Drivers" menu can be used. So it is an r1/r2 item of the respin, like
   the console fix.

Effort, rough:
- LPSS SPI: 1-2 days (register model is documented and small, FreeBSD as
  the model);
- topcase protocol + hid-like stream: 3-5 days;
- tests on the machine: 1-2 days.

The riskiest part is the interrupt path (FreeBSD's IRQ storms; GPE without
Darwin).

## 5a. The port: `ports/atopcase` (started 2026-11-15)

**State: written, compiles, links, `gmake check` clean on the OI VM. Not
run on the MacBook yet.**
- Built from four files with `-Wall -Werror` against the pristine
  c893fd03ad headers (osnet 23344, the installer medium's kernel).
- `gmake check`: all 93 undefined symbols are in unix, genunix,
  `misc/kbtrans` or `misc/acpica`, checked against the VM's 23395 kernel.
  NEEDED: misc/kbtrans, misc/acpica.
- Module sha256 `617f7150de14e108fe1ed9a3785f3964eabfe6bdf0f577f6a4a38af7d989101e`
  (with CTF).
- The build directory on the VM was cleaned afterwards; `gmake` rebuilds it
  in seconds. Nothing published, nothing committed.

One module, `atopcase`, no SPI framework (U 3 left open: this is the "fast"
variant; the SPI part is its own file, so it can become a nexus later):

| File | What | Licence |
|---|---|---|
| `src/atopcase_spi.c` | Intel LPSS SPI, Sunrise Point family: reset release, software CS (priv +0x24), SSCR0/1, clock divider from the 50 MHz FreeBSD assumes, **polled full-duplex FIFO transfers** (no controller interrupt; also usable from kmdb). Sets D0 and memory decode if needed | BSD-2 (intelspi) |
| `src/atopcase_proto.c` | the topcase protocol from FreeBSD atopcase.c: 256-byte packets, CRC-16/ARC, two-packet messages, write + status `ac 27 68 d5` + reply, boot packet, device/interface/HID descriptor requests, SET_REPORT, keyboard backlight | BSD-2 |
| `src/atopcase.c` | DDI glue: binds to the LPSS SPI PCI IDs and finds `APP000D` with `AcpiGetDevices`, accepting it only if its parent's `_ADR` is this PCI function (other controller, other machine: quiet attach failure). `UIST` (USB mode → leave), `_CRS` SpiSerialBus (speed, CPOL/CPHA; default 8 MHz), `SIST`/`SIEN` restart. **Events: `_GPE` handler (wakes the worker through a soft interrupt, `AcpiFinishGpe` after reading) if GPEs arrive while the topcase boots, else polling** (8 ms while keys come, 20 ms idle; tunables `atopcase_busy_poll_ms`, `atopcase_idle_poll_ms`, `atopcase_force_poll`). One worker thread does all SPI I/O. HID descriptors are kept as devinfo properties `keyboard-hid-descriptor` / `trackpad-hid-descriptor` (`prtconf -vp` shows them for checking) | BSD-2 (atopcase_acpi) |
| `src/atopcase_kbd.c` | STREAMS keyboard, minor `keyboard`, **DDI_NT_KEYBOARD**: `/etc/dacf.conf`'s `minor-nodetype="ddi_keyboard"` rule links it under conskbd as for kb8042, **no dacf.conf change**. kbtrans with the **USB keymap** (`KB_USB`), PC scancodes when conskbd asks for `KB_PC` (as usbkbm). Report unpacking as `usbkbm_unpack_usb_packet`; caps-lock LED via SET_REPORT; CONSOPENPOLLEDIO for kmdb. **Fn layer**: Fn+1…0,-,= = F1-F12, Fn+` = Esc (no physical Esc/F-keys on Touch Bar models), Fn+Backspace = Delete, Fn+Return = Insert, Fn+arrows = Home/End/PgUp/PgDn (`atopcase_fn_layer=0` turns it off) | CDDL (kb8042/usbkbm) |
| `atopcase.conf` | `ddi-forceattach=1`, so the keyboard is there at boot | |
| `atopcase.p5m` | `driver/input/atopcase@0.5.11-2026.0.0.1`, new package (no OS file replaced), aliases `pci(ex)8086,9d29/9d2a/a129/a12a/a2a9/a2aa`, incorporate osnet 23344 | |

Not done yet: the trackpad (reports are received and counted, not passed
on; a mouse stream comes next, first as a relative mouse); the keyboard
backlight control from userland (tunable `atopcase_backlight` at attach
only); suspend/resume; the Touch Bar (USB, separate driver).

Assumptions that only the machine can confirm:
- the SSP input clock (FreeBSD's 50 MHz; the divider gives about 7 MHz
  for the 8 MHz Apple asks for);
- the report layout (Linux applespi's; the HID descriptor property shows
  the real one);
- GPE or polling on the 14,3 without `_OSI("Darwin")`: FreeBSD says
  interrupts only arrive on 12,1 and 14,1.

### Test plan (each run only with the user's OK)

**A. In the live installer, no install, nothing written to any disk.**
The live root is the RAM disk, so the driver can be added for one session.
Needs a USB keyboard and a second FAT stick with the module and the
`.conf`. In the installer's Shell:

```
mkdir /tmp/stick                                 # not /mnt: /mnt/misc is the live /opt etc.
mount -F pcfs /dev/dsk/c?t0d0p0:c /tmp/stick     # the FAT stick (rmformat -l shows c?)
cp /tmp/stick/atopcase /kernel/drv/amd64/atopcase
cp /tmp/stick/atopcase.conf /kernel/drv/atopcase.conf
digest -a sha256 /kernel/drv/amd64/atopcase      # 617f7150…101e
modload /kernel/drv/amd64/atopcase
echo 'atopcase_debug/W 2' | mdb -kw              # log details
add_drv -i '"pciex8086,a2a9" "pciex8086,a2aa" "pciex8086,a129" "pciex8086,a12a" "pciex8086,9d29" "pciex8086,9d2a"' atopcase
grep -i atopcase /var/adm/messages
prtconf -vp | grep -i -A3 hid-descriptor         # keyboard/trackpad HID descriptors
```

Then type on the internal keyboard. The DACF rule should link it into the
console at once. Bring back the messages and `prtconf -v` (copy them to
the FAT stick). A panic only ends the live session; a reboot gives the
stock installer back.

**B. In an installed system**: a test BE, the package from oi-local,
`beadm activate -t` (one boot, falls back by itself), as with
audiohd/bge.

**C. Then** into the respun installer's boot archive (oi-apple-installer-eval
section 5), so the MacBook installs with its own keyboard.

## 5b. USB network adapter for the MacBook Pro (recommendation)

The MacBook Pro 14,3 has no Ethernet port, its BCM43602 Wi-Fi has no
illumos driver, and Thunderbolt Ethernet needs Thunderbolt path setup
that illumos does not do (Apple firmware leaves it to the OS). So: **a USB
Ethernet adapter whose chip has a driver in OI**, on a USB-C to USB-A
adapter, plugged in **before power-on** (like the stick and the keyboard,
because the Alpine Ridge xHCI does not hot-plug under illumos).

| Adapter | Chip / USB ID | Driver in OI | Speed | Verdict |
|---|---|---|---|---|
| **Apple USB Ethernet Adapter** (MC704, USB-A) | ASIX AX88772A, **05ac:1402** | **`axf`**: the alias `usb5ac,1402` is in OI's driver_aliases (checked on the VM, osnet 23395) | 10/100 | **recommended**: exact ID listed, an Apple part, common second-hand |
| Other AX88772 / AX88772A adapters | 0b95:7720, 0b95:772a | `axf` (aliases listed) | 10/100 | good, but check the ID: most current ASIX adapters are AX88772B/C (0b95:772b), which `axf` does not list (adding the alias is possible but untested) |
| Adapters that offer a CDC-ECM configuration by default | class 2.6 | `usbecm` | up to 1 Gbit | works only if ECM is the **first** configuration. RTL8153 adapters (most USB-C gigabit dongles) default to a vendor configuration, so not reliable |
| AX88179 / RTL8152/8153 gigabit adapters in their native mode | 0b95:1790, 0bda:8153 | **none** | | not usable |
| Apple Thunderbolt Gigabit Ethernet (BCM57762) + TB3-to-TB2 adapter | 14e4:1682 | `bge`, but behind Thunderbolt | 1 Gbit | not usable: no Thunderbolt driver on illumos |

To check on the stick: whether `driver/network/axf` is on the installer
medium. The stick was moved before that check. If it is missing, the
installed system lacks it until a `pkg install`, which needs the network,
so it then belongs in the OI-Apple p5p or the respin.

## 5c. First install (user, 2026-11-15)

- Stock OI 2026.10 text installer from the INTENSO stick, with an external
  Apple USB keyboard: **the install worked and found the internal disk**
  (Apple NVMe). So `nvme` attaches to it, and the 106b:2003 class question
  from section 3 is settled.
- After the reboot it does not start from the internal disk, and the
  **Option menu does not list it**. Most likely Apple's Startup Manager
  hides an internal ESP that holds no macOS. On the Mac Pro the fix was
  `bless` from macOS; here it goes through macOS Recovery (⌘R / ⌥⌘R):
  `diskutil mount disk0s1`, then
  `bless --mount /Volumes/EFI --setBoot --file /Volumes/EFI/EFI/Boot/bootx64.efi`.
  installboot's file is `/EFI/Boot/bootx64.efi`, mixed case.
- Stopgap without macOS: boot the stick, escape to the loader prompt,
  `lsdev -v`, then `set currdev=zfs:rpool/ROOT/openindiana:` and `boot`.
- If `diskutil mount` fails on the ESP, the FAT itself is the fault
  (4K-sector NVMe suspected): then `prtvtoc`/`fstyp` from OI, and the fix
  goes into the installer.
- For the OI-Apple installer: bless is a manual macOS step on every Mac
  with an internal-disk install. An illumos tool that sets Apple's
  `efi-boot-device` NVRAM variable would remove it (todo).

## 5d. Evening 2026-11-15: what the boot attempts showed

**Disk and pool** (loader `lsdev -v`, photo):
- disk0 = the NVMe, **61,279,344 × 4096-byte sectors** (4Kn, about 251 GB);
- GPT: `p1` EFI 256 MB, `p2` illumos ZFS 233 GB, `p9` reserved 8 MB;
- pool **`rpool1`** (not rpool), bootfs `rpool1/ROOT/openindiana`, ONLINE.

**ESP not readable**: the stick's loader fails `ls disk0p1:/` with
`open 'disk0p1' failed: no such file or directory`. That loader (2026.10 =
c893fd03ad) contains illumos 16797 "loader: dosfs should be able to work
with different sector sizes" (2024-09), so 4K FAT as such is supported.
The partition most likely holds **no valid FAT**. The Option menu does
not show the disk either.

**Cause (found 2026-11-15 night, reproduced on the OI VM; details,
patches and test in `notes/illumos-esp-4kn/README.md`):** the ESP holds
no FAT at all.
- The text installer runs `zpool create -f -B` (slim_source libti
  ti_zfm.c), which makes a 256 MB ESP: on 4Kn that is only **65,536
  sectors**.
- installboot runs `mkfs -F pcfs -o fat=32` on it. mkfs refuses ("Requested
  size is too small for FAT32", exit 4): FAT32 needs more than 65,524
  clusters.
- installboot only checks that mkfs was started, not its exit status, so
  it carries on, the pcfs mount fails silently, and no `bootx64.efi` is
  written. The installer sees no error.
- (My earlier suspicion of a 512-byte buffer in `write_fat32_bootstuff` was
  wrong: `boot_sector_t` is 4096 bytes, `buf[MAXBPS]`.)

**Fixed on the MacBook (user, 2026-11-16)** with the recipe in
`notes/illumos-esp-4kn/README.md` section 4: FAT16 on the 256 MB ESP,
then `bootadm install-bootloader`, plus `boot-ncpus="2"` in
/boot/conf.d.
- OI now boots from the NVMe on its own, and the 2-CPU setting from
  conf.d is used.
- The Option boot picker lists the disk.
- So Apple's firmware boots from a FAT16 ESP with 4096-byte sectors.

**Earlier workaround, corrected**: with only `set
currdev=zfs:rpool1/ROOT/openindiana:` and `boot` at the stick's loader, the
machine booted **the stick's** system, not the NVMe's (user, 2026-11-16).
This note said before that root came from the NVMe; that was wrong. A
loader that has already loaded its kernel and boot archive from the stick
keeps them after a currdev change.

**Kept for later: CPU start hang** (MacBookPro14,3, i7-7700HQ, 4C/8T):
- all-CPU boot hangs while starting the application processors, with the
  fans rising (the CPUs already up spin). First at cpu3; after `set
  idle_cpu_no_deep_c = 1` in /etc/system, at cpu7. A timing race when the
  cores start; deep C-states make it more likely.
- `-B boot-ncpus=2` boots.
- Next to try: `set idle_cpu_prefer_mwait = 0` (HLT idle), then the
  interrupt mode (x2APIC/apix) and the microcode load. Check with
  `echo 'idle_cpu_no_deep_c/D' | mdb -k`.
- Tunables exist in this gate: `idle_cpu_no_deep_c`,
  `idle_cpu_prefer_mwait` (mp_machdep.c); `boot-ncpus` (fakebop.c).

**USB keyboard**: the Magic Keyboard (05ac:0267) attached on the PCH xHCI
(`pci8086,7270@14`, port 2), then dropped out again with `Connecting device
on port 2 failed`, in a loop. The cause was a USB-A to USB-C adapter; with
a direct USB-C cable the keyboard works.

## 5e. Running on its own: SMC, CPUs, console font (2026-11-16)

The user's list, in this order: (1) SMC/fans, (2) all 8 CPU threads,
(3) keyboard/trackpad, (4) Touch Bar, (5) font size on the Retina panel.

**Access**: the MacBook is not reachable yet. The LAN sweep on 2026-11-16
found the Mac Pro, the iMac, the VMs and one unknown Apple
host (OpenSSH 10.3) that refuses our key. Needed: network under OI
(USB Ethernet; the internal BCM43602 has no OI driver) and ssh access.

### 1. SMC and fans (prepared, not built: the OI VM is running a clang-23 build)

- The 2016+ SMCs may keep the fan keys (F<n>Ac/Mn/Mx/Tg) as `flt `
  (IEEE single in rpm, little endian, 4 bytes) instead of `fpe2`. Our
  driver and `smcfan` accepted only fpe2. With flt keys, smcfan would find
  "no fan the driver can set", and applesmc-fancurve would do nothing
  without saying so (`smcfan -m … || true`). `smcstat` and Mac Sensors
  already decode flt.
  - No heat risk from that: the SMC keeps its own fan control and thermal
    protection either way, and the curve only raises the floor.
- `ports/applesmc/patches/0003-18700-applesmc-flt-fan-keys.patch` (rev
  0.5.11-2026.0.0.4):
  - the driver keeps fan values as rpm × 4 and converts flt with integer
    arithmetic (no FPU in the kernel);
  - writes must use the key's own type and length;
  - restore at detach works in either type;
  - `APPLESMC_IOC_FANMIN` still answers in fpe2;
  - smcfan reads and writes both types;
  - applesmc(4D) and the header updated.
- Unit test `files/applesmc_fan_test.c`: flt cases added, including an
  exhaustive check that the integer conversion matches the FPU for all
  65,536 values. 50 tests, 0 failures (run with gcc on the Linux workstation against the
  policy block cut out of the patched applesmc.c).
- Not yet done: module build and `gmake check` on the VM. It waits until
  the clang-23 build is done.
- `applesmc-fancurve` 1.1:
  - one speed for all fans, between the lowest minimum and the highest
    maximum; the driver clamps each fan to its own range. Before, the
    first fan's range was used for all;
  - CPU = the hottest of TCXc/TCXC/TC0P/TC0E/TC0F/TC0D/TC<n>C;
  - GPU also TG?P.
  - Without these the MacBook (no TM*, maybe no TCXc) might match no CPU
    sensor, and then the script runs the fans at maximum.
  - Checked with sample MacBook values in awk (2 fans 2000-6550 rpm, CPU
    79.5 °C → floor 4401 rpm).
- MIN_OSNET of applesmc is 23344, so it installs on the MacBook as
  installed (2026.10).
- On the machine, once reachable:
  1. `smcstat -a` → key list and types; check `FNum`, `F0Mn`/`F0Mx`
     (fpe2 or flt), the CPU, GPU and PCH keys;
  2. install applesmc (+ fancurve, mac-sensors) from oi-local, on the
     user's OK;
  3. `smcfan` shows both fans;
  4. `smcfan -s cool`: the fan floors rise; then `-s auto`;
  5. start the fancurve service with `LOG=` set, then load: build or a
     `stress`-like loop at 2 CPUs. The floor must follow TC*; record the
     temperatures and rpm.

### 2. CPU start hang (test plan; one boot per test, nothing persistent)

From mp_startup.c (c893fd03ad): if an AP never reached the kernel, the
boot CPU would print `cpuN: started, but not running in the kernel yet`
after 5 s and `timed out` after 20 s, then go on. A silent hang with the
fans rising therefore points to a spin later in the handshake, with
interrupts off on both CPUs:
- `tsc_sync_master()` / `tsc_sync_slave()` (timestamp.c, spin loops on
  `tsc_sync_go`, both CPUs with interrupts off);
- or the AP's microcode load (`ucode_apply`).

The OI microcode for this CPU (000906E9-02/08/20) is revision **0xf8**
(2023-09-28; system/microcode/intel 20260925). Whether it is loaded at all
depends on the revision Apple's firmware leaves in the CPU.

Read first (2-CPU boot, root, read only):
- `ucodeadm -v`
- `psrinfo -vp`
- `echo 'apic_mode/D' | mdb -k`
- `prtconf -v | grep -i x2apic` (may print nothing)

Then one boot each. At the loader: `unset boot-ncpus` (the conf.d file
sets it to 2), then `boot -kdv`. At the `[0]>` prompt, type the line for
the test, then `:c`:
| test | kmdb line | cuts out |
|---|---|---|
| T0 baseline | (none) | — wait 30 s at the hang: does `started, but not running` or `timed out` appear? then F1-A; if kmdb answers: `::cpuinfo -v`, `$c`, and for the hung CPU `::cpuregs -c N` (photo) |
| T1 | `tsc_master_slave_sync_needed/W 0` | TSC sync handshake |
| T2 | `idle_cpu_prefer_mwait/W 0` | MWAIT idle (HLT instead) |
| T3 | `apix_enable/W 0` | apix (pcplusmp instead) |
| T4 | `::bp ucode_apply` (then `:c` at each stop; note which CPU hangs right after) | shows whether the microcode load hangs |

All these variables are in unix, so kmdb can set them right after `-kd`.
(`x2apic_enable` is in the apix module and would need /etc/system.) The
test that boots with 8 CPUs names the cause, and the fix goes into the
code, not into a permanent tunable.

### 5. Font size on the Retina panel (2880 × 1800, 220 dpi)

- **Console** (loader and kernel): the font is chosen in
  usr/src/common/font/font.c `set_font()`: the largest font that still
  fits 80 × 25. The largest shipped font is 16x32 (/boot/fonts), and that
  is what this panel already gets: 180 columns, physically tiny. So a
  bigger font is needed.
  - Plan: package a 32x64 console font. Spleen (BSD-2, made for HiDPI
    consoles; OpenBSD uses it) as upstream BDF, converted at build with
    `/usr/bin/vtfontcvt` (on OI) into `/boot/fonts/…fnt`.
  - Load it from a `/boot/conf.d/` snippet:
    `exec="loadfont /boot/fonts/spleen-32x64.fnt"` (support.4th knows
    `exec`). `screen-font="32x64"` then picks it; to be tested. The
    loader passes its font to the kernel console (`console-font`).
  - Install it only on Retina Macs (per-model, in the updater).
  - Alternative: `framebuffer set <GOP mode>` with a lower mode, if
    Apple's GOP offers one (`framebuffer list`). It would also halve X on
    a framebuffer driver, but it is blurrier.
- **Desktop**: GNOME on X11 picks scale 2 by itself only when the X
  server reports the real DPI from EDID. On a framebuffer or vesa driver
  it sees 96 dpi and keeps scale 1. Plan:
  - a gsettings override (`org.gnome.desktop.interface scaling-factor=2`;
    it applies to GDM too), in a package for Retina models;
  - `text-scaling-factor` stays 1.0;
  - with a KMS driver (i915n) check first whether scale 0 (auto) already
    gets it right.
- To read on the machine: `framebuffer list` at the loader; `xdpyinfo |
  grep -i dimensions` and the X driver in use (`/var/log/Xorg.0.log`).

### 4. Touch Bar (research only)

- The T1 (iBridge, USB 05ac:8600) only shows Esc/F-keys once a driver
  sets its mode (Linux apple-ib-tb, GPL, reference only: a HID feature
  report for the mode and one for the display).
- Next: `prtconf -v`/`cfgadm -lv` on the machine to see the iBridge's USB
  configurations and HID interfaces.
- Then decide: a small HID client (kernel or userland over ugen) that
  switches it to F-key mode.
- Idea (user, 2026-11-16): the OpenIndiana logo on the Touch Bar, as a
  gimmick. Our appletb only uses the iBridge's HID configuration, where
  the bar shows Apple's predefined key sets, so it cannot draw. Drawing
  needs the other USB configuration, where the host sends pixels (Linux
  appletbdrm, mainline since 2025, plus Asahi's tiny-dfr daemon). That
  driver states T2 Macs only; whether the T1 in the 14,3 has the same
  display configuration is open. To check on the machine: the iBridge's
  USB configurations (`prtconf -v` of 05ac:8600, number of configs and
  their interfaces). If a display interface exists: a small userland
  tool over ugen that switches the configuration and sends one logo
  bitmap (the Touch Bar is 2170 × 60), with the F-keys drawn beside it
  later. If not: no logo on the T1.

## 5f. Offline preparation 2026-11-16 (MacBook not on the network yet)

Nothing here is built on OI or tested on the machine yet. The OI VM was
running a Chromium build, so all checks were gcc syntax checks against
OI's headers, plus unit tests run with gcc on the Linux workstation.

**Trackpad (ports/atopcase 0.5.11-2026.0.0.2)**:
- Until a driver switches it to raw mode, the topcase trackpad is an
  ordinary HID mouse (FreeBSD bcm5974: feature report 2 switches it).
- New `src/atopcase_tpad.c` (CDDL):
  - parses the trackpad's own report descriptor for the input report
    with relative X/Y, plus the wheel and buttons 1-3;
  - turns REPORT(TPAD) packets into VUID Firm_events (as usbms: LOC_X/Y
    deltas, Y up, MS_LEFT/MIDDLE/RIGHT, wheel) on a new minor node `mouse`.
- The stream handles VUIDSFORMAT/GFORMAT, VUIDS/GADDR, MSIOBUTTONS,
  MSIOGET/SETPARMS, VUIDGWHEELCOUNT/INFO/STATE and VUIDSWHEELSTATE, both
  as I_STR and transparent (mcopyin/mcopyout), so Xorg can open the node
  itself.
- It is not linked under consms: dacf.conf has no rule for it, and a
  package must not edit that file.
- `files/atopcase_link.c`: devfsadm module for `/dev/atopcase/keyboard`
  and `/dev/atopcase/mouse`.
- `files/60-atopcase-trackpad.conf`: an xorg.conf.d InputDevice (mouse
  driver, Protocol VUID, AutoServerLayout).
- `files/atopcase_tpad_test.c`: unit test of the parser (boot mouse,
  report id + 12-bit + wheel, absolute pointer rejected, truncated
  descriptors). 28/28 pass. It is part of `gmake check`.
- Multitouch later: raw mode, Wellspring9 model 6 finger records (FreeBSD
  bcm5974 TYPE4: button offset 31, fingers at 46, 30 + 2 bytes each,
  x -7456..7976, y -163..9283).
- Test C, after A/B in 5a: the dmesg line `trackpad: report id …` and
  `ls -l /dev/atopcase`. Then X with the snippet: the pointer moves,
  click works, two-finger scroll gives the wheel if the HID mode reports
  one.

**Touch Bar (ports/appletb, new, CDDL)**:
- Facts from the Linux T1 driver, used as a reference only:
  - the iBridge (05ac:8600) has 3 configurations;
  - configuration 1 has two camera interfaces and two HID interfaces
    (Touch Bar, light sensor);
  - the keys are ordinary HID keyboard reports, which hid and usbkbm
    already handle;
  - the mode switch is a *vendor device* request: 0x40/0x09, wValue
    (type << 8) | report id of usage 0x00ff0004, wIndex the TB
    interface, 1 byte: 0 Esc, 1 F-keys, 2 special, 3 off;
  - the display needs a class SET_REPORT for usage 0xff120021:
    `{ rid, 1, state }`, 11 bytes, state 1 on, 2 dim, 4 off.
- illumos: hid accepts SET_REPORT only as an M_CTL from a module above
  it. usb_mid gives no ugen for multi-configuration devices with bound
  children. So no userland tool can do the switch.
- The driver binds to the light-sensor interface (alias
  `usbif5ac,8600.config1.$(ALS_IF)`, assumed 3; **confirm with prtconf
  -v**) and finds the TB interface by reading the HID report descriptors.
  - USBA does not restrict wIndex or the recipient, so this works from a
    sibling interface.
  - It sends display on + F-key mode at attach, DDI_RESUME and reconnect.
  - Tunables: appletb_mode, appletb_disp.
- `files/appletb_hid_test.c`: 10/10. Syntax-clean with -Wall -Wextra
  -Werror.
- Fn → special keys later: atopcase knows the Fn state and would call
  appletb.

**Console font**:
- illumos `vtfontcvt` stops at 16 pixels wide ("Unsupported wbytes 4").
  The loader and tem render any width (`howmany(width, 8)` bytes per
  row).
- packages/fonts/spleen-console (system/font/console/spleen 2.2.0,
  BSD-2) builds its own vtfontcvt from the pristine gate archive
  586678ac1b with `files/0001-vtfontcvt-glyphs-up-to-32-pixels-wide.patch`.
  The patch is meant for illumos upstream too.
  - Checked on the Linux workstation: output byte-identical to the stock tool for
    Spleen 12x24 and 16x32.
  - The 32x64: all 977 mapped glyphs match the BDF bit for bit.
- packages/system/apple-hidpi (system/apple/hidpi 1.0):
  - `/boot/conf.d/apple-hidpi-font` with `exec="loadfont
    /boot/fonts/spleen-32x64.fnt"`. loadfont marks the font manual, and
    the loader passes it to the kernel as `console-font`.
  - `40_apple-hidpi.gschema.override` (scaling-factor=2, also GDM).
  - Installed per model by the updater.
- To test on the VM's loader first: font size, menu, then the kernel
  console.
- **Tested 2026-11-16 in QEMU on the Linux workstation (OI 2026.10 ISO, 2880x1800):
  the kernel panics with the 32x64 font** (boot_fb.c glyph buffer is
  sized for 16x32 and overflows). The loader also ignored the `exec` line
  alone (conf.d is read twice; loadfont of a known size drops the manual
  flag), now worked around with `screen-font="32x64"`. Spleen 16x32 boots
  fine. Do not install apple-hidpi before a kernel with
  `notes/illumos-console-font-large/0001` is in place; details and both
  patches are in that folder.

**CPU start hang**: `notes/illumos-ap-start/` has the analysis and patch
0001.
- A CPU that stops after reaching the kernel leaves cpu0 spinning in
  `mp_startup_wait()` / `tsc_sync_master()` with no timeout and no
  message, which matches what we saw.
- The patch makes the hang name the CPU and the startup step after 5 s.
  The fix by step is tabled there.
- One boot with a patched unix (in a BE, on the user's OK) tells which
  step it is.

## 5g. X and GNOME on the 14,3: two GPUs behind the gmux (plan)

From the notes (mac-unified-stack-plan-2026-11-14.md, radeon-on-illumos.md,
illumos-drm-amdgpu/):
- our amdgpu covers **SI (Tahiti) only**; Polaris is plan step D,
  headless → SMU7 → **Display Core** (Linux has no dce_v11_0 any more);
- i915n's display gate admits **Ivy Bridge/Haswell only**; Kaby Lake
  (gen9.5) is plan step E;
- Mesa 26 has no iris/crocus yet (step B);
- there is no Polaris or kbl_dmc firmware package (step C), and no gmux
  code.

**Which GPU has the panel at boot**: to read on the machine, it decides
the order.
- On the 2016-2017 15" models Apple's firmware hands the eDP panel and
  the GOP framebuffer to the **Radeon**.
- It hides or powers off the Intel GPU unless the boot loader identifies
  as macOS through the Apple `set_os` EFI protocol (as Linux's
  apple_set_os / gpu-switch do).
- Reads:
  - `prtconf -D | grep -i display` and `/usr/bin/scanpci`: is 8086:591b
    on the bus at all?
  - which device the GOP framebuffer belongs to (`prtconf -v` of the
    console device; loader `framebuffer get`);
  - acpidump of APP000B (gmux) and its I/O range.

**Now (works without DRM)**:
- X on the firmware framebuffer (illumosfb/vesa) + system/apple/hidpi for
  scale 2;
- the trackpad via atopcase;
- GNOME as on the other Macs.
- No 3D (llvmpipe) and no brightness control.

**Step G1, gmux backlight (small, first real need)**:
- A `gmux` driver for ACPI APP000B: the backlight through the gmux
  (indexed gmux v4 as on the Mac Pro; ports and protocol from the Linux
  apple-gmux driver as reference). Exported as a /dev node plus a tiny
  CLI, then GNOME brightness keys.
- Read and log the mux state; no switching yet.
- The Mac Pro's gmux showed "version 4.0.11 [indexed]".
- **Written 2026-11-16: `ports/applegmux`** (driver/graphics/applegmux
  0.5.11-2026.0.0.1, CDDL, our own code; Linux apple-gmux only for the
  register map and protocols):
  - a pseudo device (as applesmc) that finds APP000B with AcpiGetDevices,
    takes the first I/O or memory range from _CRS, and tells the three
    kinds apart as Linux does: I/O window >= 0x78 with version bytes =
    classic; version 0xff but probe bytes 0xcc/0xcd read back 0x55aa =
    indexed (the 14,3); otherwise memory, command byte != 0xff = T2
    (mapped with AcpiOsMapMemory, 16 bytes);
  - indexed handshake with a 20 ms timeout per step (EIO instead of a
    hang); the brightness is 24 bits, max from register 0x70 (0xffff on
    T2);
  - `/dev/applegmux` (devfsadm linkmod), mode 0644 root sys: INFO (type,
    version, max, brightness, raw display/DDC/external mux registers) and
    GET_BRIGHTNESS for everyone, SET_BRIGHTNESS for an fd opened for
    writing (root, or the console user with an /etc/logindevperm line,
    which the package does not add);
  - suspend saves the brightness, resume writes it back; no interrupts,
    no switching, no discrete-GPU power (the gmux has no documented
    read-back for that, so it is not reported);
  - `gmuxctl` (show; `-b` percent; `-s N | N% | +N% | -N%`), header
    installed as /usr/include/sys/applegmux.h for a GNOME backlight helper
    later.
- Checked on the Linux workstation: driver gcc 14 `-fsyntax-only -Wall -Wextra -Werror`
  against the 586678ac tree with kernel flags; gmuxctl syntax-clean;
  `files/applegmux_test.c` runs the driver's protocol section against
  simulated classic, indexed (lagging and instant) and T2 gmuxes, an
  absent one and stuck ones: all pass; a mutation (no acknowledge after
  an indexed transfer) is caught. Not built on OI, not on the machine.
- On the 14,3: the attach line `gmux 4.x.y (indexed) at 0x…: backlight
  … panel on the … GPU`, then `gmuxctl`, `gmuxctl -s 50%` (user's OK).
  The same driver should attach on the Mac Pro 6,1 (indexed 4.0.11); its
  max brightness there tells whether it has a backlight at all.

**Path R, Radeon owns the panel (the expected default)**:
- needs amdgpu Polaris = plan step D, Display Core included;
- firmware system/firmware/amdgpu-polaris (polaris11_*);
- Mesa radeonsi (already built in Mesa 26).
- The same work serves the Polaris iMacs. The plan's order (Polaris iMac
  first, then iMac19,1, then this MacBook) still fits: the iMac has no
  mux and no battery.
- Coordinate with the Radeon session (`Analyze: why no Radeon…`).

**Path I, Intel owns the panel**:
- the loader calls Apple `set_os`, a small illumos loader patch
  (EFI-only, Mac-only);
- the gmux switches the panel to the iGPU before boot;
- needs i915n Kaby Lake display (step E: gen9 display engine, eDP, DMC
  firmware kbl_dmc), Mesa iris (step B).
- Lower power and battery life than the Radeon. Also big, but it is
  i915 work, which our tree already does for gen7.

**Recommendation**:
1. Now: framebuffer + hidpi + gmux backlight (G1).
2. Then whichever DRM path lands first for another Mac: Polaris for the
   iMac 555/560 (path R), or i915n gen9 (path I).
3. Path R first, since amdgpu is where our DRM work is, unless the read
   shows the firmware boots this MacBook on the iGPU.

Switching GPUs at run time (vga_switcheroo-like) is not planned.

**Coordination with the graphics session** (owner of amdgpu/drm, reply
2026-11-16):
- Step D (Polaris) has not started. Its order stays: 555/560 iMac →
  iMac19,1 → this MacBook. The code is the same Polaris 11 / DCE 11.2.
- Sizing (graphics session, 2026-11-16, from Linux 7.2 Kbuild): our
  amdgpu already compiles the Polaris core (vi, gmc_v8, gfx_v8, sdma_v3,
  uvd_v6, vce_v3) and SMU7 powerplay. A headless attach needs PCI IDs,
  firmware and testing. The display needs DC: +228 MIT translation units,
  plus one FPU stub (DSC, DCN only). Details are in
  notes/mac-unified-stack-plan-2026-11-14.md section 5.
  The mux makes the MacBook harder to debug, so the iMac goes first if
  one is on hand. Which Polaris Mac comes first is the user's decision
  (plan section 9).
- It agrees: path R first for the 15".
- **Do not start** the polaris firmware package
  (system/firmware/amdgpu-polaris): it belongs to the graphics session,
  together with the amdgpu loader paths.
- The gmux backlight driver is ours: a separate module outside
  illumos-drm, with Linux apple-gmux.c as a reference. The ACPI
  resources of the gmux device decide between the indexed and MMIO
  interface.
- Reads it needs from the MacBook (under illumos; no Linux boot):
  1. PCI IDs **and subsystem IDs** of both GPUs (`prtconf -pv`,
     `scanpci`);
  2. the ACPI table list, and whether VFCT or ATRM carries the Radeon
     VBIOS (acpidump / the loader's tables). The Mac Pro's came from
     VFCT;
  3. whether the HD 630 (8086:591b) is on PCI at all without Apple
     set_os. That decides path I.

## 6. Order

1. Now: install with a **USB keyboard** (and USB Ethernet), to the target
   that works:
   - the internal SSD only if it shows up (106b:2003 class, section 3);
   - otherwise an external USB SSD. That needs the hubd fix, so it waits
     for the respin, or for the manual Shell step from
     oi-apple-installer-eval.md 3c.
2. Read the machine (section 4) and record it in a notes file per model.
3. NVMe alias, if needed: smallest fix, into the respin (`driver/storage/nvme`
   republished like bge/usb).
4. LPSS SPI + topcase driver: **written, compiles** (`ports/atopcase`,
   section 5a). Next: test A in the live installer, then a BE, then a
   package on oi-local and the respun installer. Then the trackpad stream.
5. Touch Bar F-key mode (14,2/14,3).
6. SMC keys and battery; sound (codec dump); Kaby Lake display in i915n;
   Wi-Fi (bwfm port) last.

## 7. Decisions for the user (U)

1. ~~Which MacBook Pro 2017~~: **15" with Touch Bar = MacBookPro14,3**
   (user, 2026-11-15). So the Touch Bar (priority 2) and the 15" items
   (gmux, Polaris dGPU) apply.
2. Is a USB keyboard and USB/Thunderbolt Ethernet acceptable for the first
   install and tests?
3. SPI on illumos: a private LPSS-SPI ↔ topcase interface (fast), or a
   small SPI framework in the style of the I2C framework (more work,
   upstreamable)?
4. Wi-Fi: start the bwfm port (a project of its own), or wired adapters
   only for now?
