# Network on the iMac 27" Retina 5K, 2019 (iMac19,1) under OpenIndiana

Side session, night of 2026-11-08/09. Outcome: **wired Ethernet with illumos'
own `bge`**. It already knows the chip. `ports/bge-57766` adds three small
BCM57766 fixes from Linux tg3 and FreeBSD, and it builds on the OI VM. Wi-Fi is
not feasible. Nothing has run on real hardware yet.

**Update 2026-11-09: the user's iMac is an iMac13,2** (27", Late 2012;
`smbios -t SMB_TYPE_SYSTEM`), not an iMac19,1, with OI 2026.10 text-installed.
- Wired: also a **BCM57766** (14e4:1686). The 2012 iMac/Mac mini were the
  first Macs with it. The Linux tg3 placeholder-MAC patch (e4c00ba7274b) was
  tested on an iMac13,2 and reports that this model has working
  NVRAM/mailbox MAC paths. So the driver, the patches and the test plan
  below apply unchanged. Patch 0003 should stay silent, and patch 0001 still
  matters (the iMac13,2 takes up to 32 GB RAM).
- Wi-Fi: **BCM4331** (14e4:4331, 802.11n 3x3, an Apple AirPort card, not
  soldered). It is a SoftMAC chip: Linux `b43` with firmware cut from
  Broadcom's proprietary driver (b43-fwcutter), FreeBSD `bwn` only partly.
  illumos only has `bwi` (old 802.11b/g BCM43xx), which does not cover the
  N-PHY 4331. Still not feasible as a quick job. Wired stays the target.
- Confirmed on the iMac (2026-11-09): `prtconf -dD` shows BCM57766 and
  BCM4331. `uname -v` gives illumos-c893fd03ad, the VM's kernel, so the
  module built on the VM fits. The **stock bge attaches**: `dladm show-phys`
  lists bge0 (state down, no cable yet). Link and load test (step A) still
  to do.

## 1. The two NICs

| | Wired | Wi-Fi |
|---|---|---|
| Chip | Broadcom **BCM57766** (A1), 10/100/1000BASE-T, integrated PHY | Broadcom **BCM4364** (Apple-only module, 802.11ac 3x3 + BT 5.0), soldered |
| PCI ID | **14e4:1686** (subsystem 106b:xxxx, Apple) | 14e4:4464 (expected, check on the iMac) |
| Linux | `tg3` | `brcmfmac` only with the out-of-tree t2linux patches, plus Apple firmware taken from macOS |
| FreeBSD / OpenBSD | `bge(4)` (57766 since 2012) | none |
| illumos | `bge` (57765 family since illumos 12450, 2020) | none |

The 57766 has been Apple's on-board gigabit chip since the 2012 iMac and Mac
mini (the Thunderbolt adapter is the 57762, same family). The iMac19,1 has no
T2 chip, so the T2 trouble described below (no NVRAM, placeholder MAC) most
likely does not apply to it. The board itself must confirm both IDs (see 4.A).

## 2. Choice: wired, via bge

- **Wi-Fi is out of reach.** BCM4364 is a FullMAC chip. It needs a firmware
  image plus a board-specific NVRAM/clm blob that Apple ships only in macOS
  ("hawaii" family files), loaded by a Broadcom-protocol host driver (msgbuf
  over PCIe). illumos has no such driver: no brcmfmac port, and its net80211
  is old (no 802.11ac, no modern WPA3). It would mean porting brcmfmac
  (~40k lines) and a new firmware loader, with firmware we could not
  redistribute. Even Linux mainline does not support this module without
  the t2linux patches.
- **Wired is nearly done already.** illumos-gate's `bge` has known the 57766
  since 12450 "Add support for BCM57765 family devices to bge" (2020): device
  ID 0x1686, chip label, NVRAM type, the PCIe FTS fix (CPMU_PADRNG_CTL_RDIV2,
  as in FreeBSD f8bb33c3310), and it leaves EEE off on this family (so tg3's
  57766 EEE firmware patch is not needed). OpenIndiana's driver_aliases
  already map `pci14e4,1686` and `pciex14e4,1686` to bge (checked on the VM).
  **So the stock OI 2026.10 kernel should already bring the port up**: the
  first hardware test needs no patch at all.

## 3. What is missing compared with tg3 / FreeBSD (the patches)

Source: illumos-gate c893fd03ad (the OI VM's kernel: `uname -v`
illumos-c893fd03ad). bge is unchanged from there to master b1c424a261
(2026-11-08), so the patches also apply to master.

| Patch | What | From |
|---|---|---|
| 0001 | BCM57766: keep the DDI_DMA_CONSISTENT areas (status block, statistics, descriptor rings) below 2 GB (dma_attr_addr_hi 0x7fffffff). Broadcom found that the chip flags a false DMA address overflow and resets itself on some status block addresses (bits 16-31 all set after an update with the low bits all set). bge allowed 64-bit addresses for these areas, and an iMac with 8-128 GB RAM can place them anywhere. Packet buffers are not affected. | Linux tg3 614f4d166eee (Pavan Chebbi, Broadcom, Nov 2024) |
| 0002 | 57765 family: leave the reserved read-DMA priority bits 31:30 clear (bge already does this for the 5717/5725 families; tg3 and the BSDs never set them), using a local value, not the shared global. BCM57766 at MTU 1500: set RDMAC_MODE_JMB_2K_MMRR (0x00800000), as tg3 does. bge already meets tg3's matching 2K TX BD limit (no LSO, one BD per frame). | Linux tg3 (since the 57766 support, 2012) |
| 0003 | Warn when the chip reports Broadcom's placeholder MAC 00:10:18:00:00:00 (NVRAM-less boards: T2 Macs such as the iMac20,1 or Macmini8,1), and say to set `local-mac-address` in bge.conf. bge already honours that property (an array of 6 ints). | Linux tg3 e4c00ba7274b (2026), FreeBSD 142cba958b7 (2026) |

Left out on purpose:
- tg3's 57766 EEE service-patch firmware (`tigon/tg357766.bin`): only loaded
  when the board has no NVRAM, and only needed for EEE, which bge keeps off.
- FreeBSD's "tolerate EEPROM read failure on 57766": that failure path does
  not exist in illumos. bge reads hwcfg from NIC SRAM, and an NVRAM failure
  only leaves the MAC unset (bge_get_nvmac returns 0, then the bge.conf
  property or the system address is used).
- Jumbo frames / reset-PHY-on-MTU-change (tg3 resets the PHY on 57766 MTU
  changes): only matters when the MTU is changed. Untested, listed for later.

## 4. The component: `ports/bge-57766`

- `Makefile` (GNU make, runs on OpenIndiana). It downloads the pristine
  illumos-gate tarball at c893fd03ad (GitHub archive, sha256 50ba97fa…49f6,
  166 MB, kept in ~/src/oi-userland/archives on the VM), extracts only
  `usr/src/uts/common/io/bge`, applies `patches/` with gpatch, then compiles
  with the kernel flags of Makefile.master/Makefile.uts/Makefile.intel
  (gcc 10, -mcmodel=kernel, -msave-args, retpolines, -Werror with the
  release CERRWARN list), links as uts/intel/bge/Makefile does
  (`ld -r -dy -N misc/mac`), and runs ctfconvert. `gmake check` checks that
  every undefined symbol of the module is defined in unix, genunix or misc/mac.
- `patches/0001..0003`: git format-patch against illumos-gate, ready for
  upstream review (illumos issue + RTI).
- `bge-57766.license`: CDDL 1.0, plus the BSD licence of the bge 5906 code.
- No IPS package. The file is /kernel/drv/amd64/bge, which
  driver/network/bge already delivers, and IPS will not let two packages
  deliver one file. The proper route is upstream illumos-gate, then OI's
  next driver/network/bge. Until then the module is tested in its own boot
  environment (4.B), so the installed system stays untouched.

Compile test on the OI VM (~/src/ports/bge-57766), 2026-11-09:
clean build from the pristine archive in 26 s, 0 warnings, all 141 undefined
symbols resolve (the stock module has 140; the extra one, `ether_sprintf`,
comes from patch 0003 and is in genunix), NEEDED misc/mac, CTF present.
Module sha256 `5b670e067befa1901d874659474bec467146ec561c29b7e6c11e9565c57424e4`
(reproducible). Not loaded on the VM: it has no Broadcom NIC (VirtualBox
emulates e1000/virtio only), and loading a test kernel module was not
cleared for tonight.

## 5. Hardware test plan (on the iMac)

Booting OI: OpenIndiana 2026.10 live USB (the text or GUI image, written
with dd). On the iMac, hold Option at the chime and pick "EFI Boot".

### A. Stock OI first (live USB, no install needed)

Run as root (or with pfexec) in the live session, and save the output to the USB stick or send it over:

```
uname -v
prtconf -dD | grep -i -B1 -A1 '14e4'          # both Broadcom functions, bound driver
scanpci -v > scanpci.txt                       # vendor/device/subsystem IDs, revisions
prtconf -v > prtconf-v.txt                     # full device properties
grep -n '1686\|4464' /etc/driver_aliases
modinfo | grep -w bge
dladm show-phys ; dladm show-ether -x
kstat -m bge -n chipid                         # asic_rev, nvtype, hw_mac_addr, businfo
grep -i bge /var/adm/messages                  # attach messages, errors
```

Expected: the 14e4,1686 node is bound to bge and `bge0` shows in show-phys.
- If bge did not attach although the alias is there: keep the messages,
  then try `update_drv -a -i '"pciex14e4,1686"' bge`. That alias is
  already in OI, so this should not be needed.
- Wi-Fi: only record the 14e4,4464 (or other) node from prtconf/scanpci.
  There is no driver to bind.

Then a link test:

```
ipadm create-if bge0 && ipadm create-addr -T dhcp bge0/v4
dladm show-ether bge0                          # 1000 full, link up
ping -s <router> 56 5
# load: a long transfer, both directions, >= 30 min (the 0001 fault is intermittent)
#   e.g. from the Linux workstation: ssh oi 'cat /dev/zero' | pv > /dev/null, and the reverse
kstat -m bge -n statistics | egrep -i 'err|drop|overflow|reset'
fmdump -e                                       # any bge ereports
grep -i bge /var/adm/messages                   # "msi error attention", resets
```

If stock bge works with no resets: the patches are hardening only. Still
run B, since the 0001 fault depends on where the status block lands in
memory.

### B. The patched module (installed OI, own boot environment)

The module comes from the VM (`~/src/ports/bge-57766/build/bge`, sha256
above), copied to the iMac on a USB stick. On the iMac:

```
digest -a sha256 bge                           # must match 5b670e06…24e4
uname -v                                       # must be illumos-c893fd03ad, else rebuild
                                               #   the component against that kernel's commit
beadm create bge57766
beadm mount bge57766 /mnt
cp bge /mnt/kernel/drv/amd64/bge.new
digest -a sha256 /mnt/kernel/drv/amd64/bge.new
mv /mnt/kernel/drv/amd64/bge.new /mnt/kernel/drv/amd64/bge
bootadm update-archive -R /mnt
beadm umount bge57766
```

Boot it once from the loader menu (Boot Environments → bge57766). Use
`beadm activate bge57766` only once the test passes. The old BE stays as it
was. No add_drv/update_drv is needed: the driver name and aliases do not
change.

Checks in the bge57766 BE:

```
modinfo | grep -w bge                         # loaded from the new BE
grep -i bge /var/adm/messages                  # no "placeholder MAC" warning
dladm show-phys ; dladm show-ether -x bge0
# 0001 at work: all consistent areas below 2 GB
echo '::devbindings -q bge' | mdb -k            # -> dev_info address(es)
echo '<devinfo>::print struct dev_info devi_driver_data | ::print bge_t \
  tx_desc.cookie.dmac_laddress rx_desc[0].cookie.dmac_laddress' | mdb -k   # both < 0x80000000
# 0002 has no direct check (reading device registers with mdb is not worth
# the risk): it passes when the link and load test as in A gives no errors,
# resets or "msi error attention"; compare the counts with A
```

If the placeholder warning appears (only expected on T2 Macs), add to
`/kernel/drv/bge.conf` in that BE the iMac's real address (from macOS:
`ifconfig en0`), as six ints, for example
`local-mac-address=0xf0,0x18,0x98,0x12,0x34,0x56;`, then reboot.

To go back: boot the old BE (loader menu or `beadm activate <old>`), then
`beadm destroy bge57766`.

### What to bring back

The A outputs (prtconf, scanpci, kstat chipid, messages), the B mdb lines,
and error counts after the load runs in A and B. That is what an
illumos-gate submission needs ("tested on iMac19,1, BCM57766 A1").

## 6. Sound on the iMac13,2: silent until a patched audiohd (2026-11-09)

- Hardware: `audiohd#0` is the Intel HDA controller (8086:1e20,
  PCI subsystem 8086:7270) with the Cirrus CS4206 codec (speakers, headphones);
  `audiohd#1` is the GTX 675MX's HDMI audio (codec 0x10de0040). audiotest
  reported OK on both, and the /dev links are fine (/dev/audio, /dev/dsp ->
  audiohd#0), **but nothing was heard**.
- Cause: audiohd sets every codec's GPIO direction to 0xf1, which leaves
  GPIO1-3 as inputs. Apple's CS4206 boards switch the speaker amplifier
  with GPIO3 and the headphone amplifier with GPIO1 or GPIO2 (Linux
  sound/hda cs420x: the `gpio_13` / `gpio_23` fixups, iMac12,x-14,x,
  MacBook Pro 8-10).
- Fix: `ports/audiohd-apple` (same method as bge-57766: illumos-gate
  08e59b676b = the iMac's kernel, `patches/0001`, built on the VM, `gmake
  check` clean, sha256 fe0e1b11…37f8). A new flag GPIO_ALL_OUT on the CS4206
  entry drives all GPIO pins as outputs, and data 0xff turns both
  amplifiers on. **Tested on the iMac: the speakers work** (user, 08:55).
- v2 for upstream (ports/audiohd-apple/upstream/, now also in patches/):
  only for a CS4206/CS4207 whose codec subsystem vendor is Apple's (0x106b,
  read with verb 0xf20, as Linux matches it); everything else keeps 0xf1.
  sha256 9831202b…4847, installed in BE `gnome45` only (BE `audiohd-test`
  still has v1). The iMac13,2's codec subsystem ID and v2 itself are still
  to be checked there (upstream/testing.md).
- Installed as a boot environment, not a package: BE `audiohd-test` = the
  installed system + the patched /kernel/drv/amd64/audiohd, now the active
  BE. A `pkg update` that brings a new driver/audio/audiohd would put the
  stock (silent) driver back: rebuild against the new kernel and copy again,
  until the patch is in illumos-gate. To upstream: illumos issue + this patch.
- `modunload` of audiohd fails with "Device busy" even with no user, so a
  live test is not possible: use a one-boot BE (`beadm activate -t`).
- No upstream submission for now (user, 2026-11-09): upstream/ stays as
  prepared. Instead **a package of our own** (`gmake publish` in
  ports/audiohd-apple, manifest audiohd-apple.p5m):
  - same name as OI's, `driver/audio/audiohd`, same files (module,
    audiohd.conf, man page audiohd.4d) and the same `driver` action and
    aliases, at **0.5.11-2026.0.0.23381.1** (OI's build + .1) in our
    publisher oi-gnome. pkg owns the file, `pkg verify` is clean. Chosen
    over a renamed module, which would need OI's driver_aliases changed.
  - installing needs `pkg change-facet
    facet.version-lock.driver/audio/audiohd=false` (osnet-incorporation pins
    it) and the publisher-qualified FMRI. pkg creates a new BE for it
    (reboot-needed module).
  - **update case**: `depend type=incorporate
    fmri=consolidation/osnet/osnet-incorporation@<OS build>` ties the
    package to the OS build it was built for, so a `pkg update` to a newer
    osnet build cannot go ahead without a rebuilt audiohd (pkg reports the
    conflict instead of quietly reinstalling the silent stock driver). Our
    publisher is also sticky for the installed package. Not tested against a
    real newer build yet: 23381 is the newest on openindiana.org
    (2026-11-09).
  - **rebuild for a new OS build**: read the new build's version and gate
    commit (`pkg info -r osnet-incorporation`, `pkg contents -mr
    driver/audio/audiohd | grep git-rev`); in ports/audiohd-apple/Makefile
    set OSNET_VERSION and GATE_COMMIT, fetch the tarball and set
    COMPONENT_ARCHIVE_HASH; check that patches/0001 still applies; `gmake
    clean publish` on the VM (after the VM runs that build, or with its
    headers); then on the iMac `pkg update` together with
    `pkg://oi-gnome/driver/audio/audiohd@<new>.1`.
  - installed on the iMac 2026-11-09: BE **gnome45-audiohd** (clone of
    gnome45, active on reboot), pkg verify clean, module sha256 9831202b…
    (= the tested v2). Booted into it at 14:07; the packaged module is
    loaded and reads SSID 0x106b5c00, and the user confirmed that sound
    from the GNOME session comes from the speakers.

## 7. iMac13,2 setup (2026-11-09)

- OI 2026.10 text install, then `pkg update` (osnet 23381, kernel
  illumos-08e59b676b), `pkg install mate-desktop`, then `mate_install` (Xorg,
  lightdm, NVIDIA 470 for the GTX 675MX/GK104). Afterwards: lightdm was
  installed disabled (enabled), ConsoleKit/UPower were in maintenance because
  the running dbus did not know their policy ("name lost": `svcadm refresh
  dbus` + clear), and the NVIDIA GLX link (ogl-select) only appears after a
  reboot.
- Access and address: notes/hosts.local.md (not in git).
- Publishers: openindiana.org, hipster-encumbered (media/vlc 3.0.23
  installed). The GNOME 45 packages come from a p5p made from the VM's
  oi-gnome repo (~/xfer/gnome45.p5p) and go into BE `gnome45`
  (~/xfer/gnome45-be.sh).
- Still broken, harmless: svc:/network/ipfilter (config error),
  input-method-cache (gtk2 immodules dir missing).

## 8. PulseAudio: GNOME's sound settings and the real hardware (2026-11-09)

Before: one output "0 - audiohd#0 onboard1, a", no ports, no HDMI, and
**GNOME's volume slider moved the HDMI device's volume, not the speakers**
(PA 60 % -> audiohd#1 volume 59, audiohd#0 stayed 100). Causes, in OI's
pulseaudio 17.0 component:
- 13-module-detect loads module-oss for one device only ($AUDIODEV or
  /dev/audio);
- module-oss (08-oss4) finds the mixer with SNDCTL_MIXERINFO mi.dev = N;
  illumos counts those indexes differently (sndctl_mixerinfo() adds 1), and
  on a two-device machine dsp0 got the second device's mixer;
- no ports, no device names, no notification of mixer changes in OSS.

`packages/desktop/pulseaudio` = oi-userland's component + patch 30 (module-oss
for every /dev/dspN, default first) + patch 31 (mixer derived from the dsp
link, /dev/dspN -> /dev/sound/X:Nmixer; "Built-in Audio (codec)" / "HDMI /
DisplayPort Audio" from SNDCTL_CARDINFO, form factor, icon; Speakers and
Headphones ports on audiohd's speaker/headphones controls; HDMI port; mixer
polled every second). Revision 2026.0.0.4, published to oi-gnome.

On the iMac (BE gnome45, `pkg change-facet
facet.version-lock.library/audio/pulseaudio=false`, then the oi-gnome FMRI;
pkg made a backup BE): tested with a private PA instance: both devices
named, ports present, PA 50 %/80 % -> audiohd#0 volume 50/79 with HDMI
untouched, the port switch sets speaker/headphones 0 and 75, and
`audioctl volume 70` shows up as 70 % within 2 s. The user's own session
gets the new modules after a new login. Not yet: jack state (port
availability), which needs an audiohd control (stage 2), and a listening
check.

Hardware mute (user: "mute does not mute the audiotest"): PA's mute was
software-only, so programs on /dev/dsp directly were not silenced; audiohd
has no mute control. Patch 31 now mutes in the mixer: the sink's volume
control to 0 at mute, PA's volume written back at unmute, volume changes
while muted kept for later, an audioctl change while muted counts as
unmute (polled), and PA leaves the hardware unmuted when it exits. Tested
on the iMac with audiotest playing: 60 % -> 59, mute -> 0, 40 % while
muted -> stays 0, unmute -> 39, `audioctl volume 55` while muted -> PA
unmuted, PA exit while muted -> 54. Needs `pulseaudio -k` (or a new login)
to load. Found on the way, not fixed: OI's OSSv4 code aborts PA when
module-oss is loaded with record=0 (source callbacks set on a NULL source,
source.c:479); module-detect loads with recording, so it does not show.

Build deps added on the VM: library/libsndfile/32, library/python/sip-39,
developer/check, developer/documentation-tool/doxygen.

## 9. Apple keyboard media keys (2026-11-09)

Captured on the iMac (xinput test-xi2 + raw /dev/usb/hid4): the Magic
Keyboard with Numeric Keypad (05ac:026c, boot protocol via usbkbm) sends
F7-F12 as plain F-keys (X keycodes 73-75, 76, 95, 96), **the same with
fn**; hid4 (input@0:hid_0_11) sends nothing. So no volume keysym ever
reaches GNOME; macOS and Linux hid-apple translate F-keys themselves using
the fn bit, which the boot protocol does not carry. FS-UAE uses F11/F12 as
its own menu/combination keys (libfsemu input.c), so plain F11/F12 cannot be
global volume keys. Options given to the user: Super+F-keys now (a gschema
override package), or plain keys + test, later fn support in usbkbm/hid
(as Linux hid-apple fnmode) for real Mac behaviour.
Also seen: X uses layout "us" for the Magic Keyboard and "de" for the
Bluetooth controller's emulated keyboard (05ac:820a).

User's choice: Super + F-keys. Done (2026-11-09):
- `packages/desktop/apple-media-keys` (desktop/gnome/apple-media-keys 1.0,
  local files only, like oi-userland's database/mysql-common): a
  30_apple-media-keys.gschema.override for
  org.gnome.settings-daemon.plugins.media-keys: previous/play/next =
  <Super>F7/F8/F9, volume-mute/down/up = <Super>F10/F11/F12. Plain F-keys
  stay free (FS-UAE's F11/F12).
- OI bug found on the way: svc:/application/desktop-cache/gconf-cache
  rebuilds gschemas.compiled only when a *.xml schema is newer, so a
  package that ships only an override is never compiled in.
  `packages/desktop/desktop-cache` = oi-userland's component + patch 11:
  also *.override files, and the directory's mtime (added/removed files).
  Revision 2026.0.0.23.
- Installed on the iMac (gnome45; version-lock facet lifted for
  service/gnome/desktop-cache), gconf-cache restarted once: "Doing rebuild",
  gsettings shows the six bindings. Takes effect at the next login. Not yet
  tried by hand.
- Later, for real Mac behaviour: fn support in usbkbm/hid (Linux hid-apple
  fnmode), then plain F7-F12 = media and fn+F-key = F-key.

## Sources

- illumos-gate: `usr/src/uts/common/io/bge`, commit a2876d03ca (12450);
  `usr/src/pkg/manifests/driver-network-bge.mf`
- Linux `drivers/net/ethernet/broadcom/tg3.[ch]`: 614f4d166eee (31-bit coherent mask,
  https://lists.openwall.net/netdev/2024/11/19/87), e4c00ba7274b (placeholder MAC,
  https://patchew.org/linux/20260324111931.3257972-3-sashal@kernel.org)
- FreeBSD `sys/dev/bge/if_bge.c`: fe26ad88e33 (57766, 2012), f8bb33c3310 (FTS, 2013),
  142cba958b7 (T2 Macs without NVRAM, 2026)
- OpenBSD `sys/dev/pci/if_bge.c` (57766 A0/A1 IDs)
- 57766 in Macs / 14e4:1686 with Apple subsystem: https://lists.freebsd.org/pipermail/freebsd-stable/2012-November/070592.html,
  https://lists.ipxe.org/pipermail/ipxe-devel/2013-April/002444.html
- BCM4364 in iMac19,x: Dortania Wireless Buyer's Guide,
  https://dortania.github.io/Wireless-Buyers-Guide/Airport.html
