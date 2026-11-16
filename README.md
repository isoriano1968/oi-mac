# oi-mac: OpenIndiana on Intel Macs

Drivers, packages and notes I wrote to run OpenIndiana Hipster on Intel
Macs: a Mac Pro 2013 (MacPro6,1), iMacs (iMac13,2, iMac14,1, iMac19,1)
and a MacBook Pro 2017 (MacBookPro14,3). Every piece is an IPS package
built from a recipe in this repository: pinned upstream sources (sha256),
a `patches/` directory, a package manifest (`.p5m`) and a license file.
The repository holds no binaries.

The GNOME desktop itself is in
[GNOME_3_for_OpenIndiana](https://github.com/isoriano1968/GNOME_3_for_OpenIndiana);
general OpenIndiana work that is meant for oi-userland and illumos-gate is
in `oi-upstream`.

## What is here

| Package | Directory | What it does |
|---|---|---|
| `driver/sensor/applesmc` | `ports/applesmc` | Apple SMC driver: temperatures and fans as illumos ksensors, `smcstat(8)`, `smcfan(8)` with an SMF service and the rights profile "Apple SMC Fan Control" (illumos issue 18700) |
| `system/applesmc-fancurve` | `packages/system/applesmc-fancurve` | SMF service that raises the fan floor from the hottest memory, CPU, GPU and chipset sensors |
| `desktop/system-monitor/mac-sensors` | `ports/mac-sensors` | GTK 4 application: temperatures and fans on a drawing of the machine, fan presets |
| `driver/audio/audiohd` | `ports/audiohd-apple` | illumos audiohd with Apple CS420x GPIO (speakers), HDMI/DisplayPort output on AMD and Intel display codecs, monitor names |
| `driver/input/atopcase` | `ports/atopcase` | MacBook 2015-2017 SPI keyboard and trackpad (port of FreeBSD atopcase/intelspi) |
| `driver/input/appletb` | `ports/appletb` | MacBook Pro 2016-2017 Touch Bar (T1) in F-key mode |
| `driver/graphics/applegmux` | `ports/applegmux` | Apple gmux backlight and mux state, `gmuxctl` |
| (patches for `bge`) | `ports/bge-57766` | Broadcom BCM57766 fixes for the iMac's Ethernet |
| `system/font/console/spleen` | `packages/fonts/spleen-console` | Spleen console fonts up to 32x64 for Retina panels |
| `system/apple/hidpi` | `packages/system/apple-hidpi` | HiDPI defaults: console font, GNOME scale 2 |
| `desktop/gnome/apple-media-keys` | `packages/desktop/apple-media-keys` | GNOME media keys on Super+F7..F12 for Apple keyboards (also in GNOME_3_for_OpenIndiana) |

`notes/` holds the engineering logs: the Mac Pro 2013 (amdgpu for the
FirePro D500 on illumos-drm, power management analysis), the iMacs, the
MacBook Pro 2017, the unified Mac graphics stack plan, and illumos fixes
found on these machines (EFI system partition on 4Kn disks, CPU start
reports, console fonts up to 32x64), each with its patch. Several of these
illumos changes are generic and will be offered to illumos-gate.

## Build

You need an OpenIndiana Hipster 2026.10 (or later) build machine.

**Components under `packages/`** use oi-userland's build system:

```bash
pfexec pkg install build-essential
git clone https://github.com/OpenIndiana/oi-userland.git ~/src/oi-userland
pkgrepo create ~/oi-repo && pkgrepo add-publisher -s ~/oi-repo oi-gnome
cp -r packages/<category>/<name> ~/src/oi-userland/components/<category>/
cd ~/src/oi-userland/components/<category>/<name>
gmake WS_REPO=$HOME/oi-repo PUBLISHER=oi-gnome publish
```

From another machine, `OI_HOST=user@buildhost tools/oi-pipeline.sh -s build,publish <category/name>`
does the same over ssh (`tools/oi-sync.sh` copies the components).

**Drivers under `ports/`** build out of tree against the pristine
illumos-gate snapshot their Makefile names (downloaded and checked by
sha256):

```bash
cd ports/applesmc
gmake && gmake check && gmake publish REPO=$HOME/oi-repo
```

`gmake check` refuses a module with a symbol the running kernel does not
export. A driver package is tied to the OS build it was built for (its
version carries the osnet build); rebuild it after a `pkg update` of the
OS.

## Install, and how to go back

Always install into a new boot environment, so the running system stays
as it is:

```bash
pfexec pkg set-publisher -g file://$HOME/oi-repo oi-gnome
pfexec beadm create mac && pfexec beadm mount mac /mnt
pfexec pkg -R /mnt install driver/sensor/applesmc desktop/system-monitor/mac-sensors
pfexec beadm umount mac && pfexec beadm activate mac
```

`driver/audio/audiohd` replaces the OS's own audiohd package: install it
with `pkg -R /mnt update driver/audio/audiohd@<version>` from the same
publisher.

**To go back:** `pfexec beadm activate <previous BE>` and reboot. Nothing
in the old boot environment changes. In the same boot environment,
`pfexec pkg uninstall <package>` removes a package; for audiohd,
`pkg update` from the openindiana.org publisher brings the stock driver
back.

## Security

- **The sources can be checked.** Upstream archives are pinned by sha256;
  every change to them is a readable patch with a header. The drivers
  that are new code (atopcase, appletb, applegmux, applesmc) are in the
  repository as source.
- **Kernel drivers run with full privileges.** Install them into a new
  boot environment and keep the old one until you trust them.
- **Fan control** writes only fan minimums. `smcfan` needs the rights
  profile "Apple SMC Fan Control"; `applesmc-fancurve` runs as an SMF
  service and gives the fans back to the SMC when it stops.
- **Mac Sensors** reads the illumos ksensors (root-only device nodes)
  through a small helper that runs with `pfexec` and the rights profile
  "Mac Sensors" (`file_dac_read`, `sys_devices`); the package adds that
  profile to "Console User", so only the console user gets it.
- **No setuid files, no new users, no network services.**
- Please report a problem as a GitHub issue; for a security problem,
  open an issue asking for a private contact first.

## Private files

Files whose name contains `.local` (for example `notes/hosts.local.md`,
which the notes refer to for machine addresses and access) are private and
never published; `.gitignore` excludes them.

## License

Each component is under its upstream license (see its `.license` file);
my own code is CDDL-1.0 unless its header says otherwise. The build
recipes follow oi-userland (CDDL).

Iggi
