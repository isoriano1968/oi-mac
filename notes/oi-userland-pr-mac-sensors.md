# oi-userland PR: desktop/mac-sensors 1.2

Published 2026-11-13 on the user's yes: branch isoriano1968/oi-userland
`mac-sensors` (ed7c6b8a), PR https://github.com/OpenIndiana/oi-userland/pull/28357
(base oi/hipster). Pushed from a depth-1 scratch clone of bf309cf2 plus a
VM bundle.

Before that: local branch `mac-sensors` in the OI VM's worktree
~/src/oi-userland-macsensors (from origin/oi/hipster bf309cf2), one commit
ed7c6b8a by isoriano1968, no trailers. Built, linted and published to the
worktree's own repo (pkg://userland/desktop/system-monitor/mac-sensors@1.2).
Not pushed, no PR: pushing to isoriano1968/oi-userland and opening the PR
only on the user's yes.

Upstream: https://github.com/isoriano1968/mac-sensors (public since
2026-11-13), tag v1.2 = ef26d2a, tarball sha256 ce66a375...1c12.

## What would be published

Commit `ed7c6b8a desktop/mac-sensors: add 1.2` on bf309cf2:

| lines | file |
|------:|------|
| 47 | components/desktop/mac-sensors/Makefile |
| 33 | components/desktop/mac-sensors/mac-sensors.p5m |
| 29 | components/desktop/mac-sensors/manifests/sample-manifest.p5m |
| 11 | components/desktop/mac-sensors/pkg5 |

All new, text. Privacy grep (addresses, /home, /export, e-mail, host
names) found nothing.

## How each file was made

- Makefile: `templates/justmake.mk`, fields filled in, BUILD_BITS=NO_ARCH,
  install arguments PREFIX and PYTHON; the REQUIRED_PACKAGES block from
  `env - gmake RESOLVE_DEPS= REQUIRED_PACKAGES` (the plain target cannot
  resolve before the list exists).
- manifests/sample-manifest.p5m: `env - gmake sample-manifest`.
- mac-sensors.p5m: the sample manifest, copyright line, plus
  `depend library/desktop/gtk4` (loaded through gi, invisible to pkgdepend).
- pkg5: `env - gmake update-metadata`. No patches.

## PR text (draft)

Title: desktop/mac-sensors: add 1.2

Mac Sensors is a small GTK 4 app (Python, PyGObject, pycairo) that shows
the temperatures and fans of a machine on a drawing of it. On any machine
it reads the kernel sensor framework (/dev/sensors: coretemp, pchtemp,
amdnbtemp, ...). On Intel Macs it also reads the Apple SMC through
smcstat(8) of the applesmc driver, which I will propose to illumos-gate
separately; without it the app shows the CPU and chipset cards only.

Upstream: https://github.com/isoriano1968/mac-sensors (CDDL-1.0), release
v1.2. Built with justmake from the pristine tag tarball, no patches.

Tested on OpenIndiana 2026.10 (hipster) on a Mac Pro (Late 2013) and an
iMac (21.5-inch, Late 2013):

![Mac Pro](https://raw.githubusercontent.com/isoriano1968/mac-sensors/v1.2/screenshots/macpro6-1.png)
![iMac](https://raw.githubusercontent.com/isoriano1968/mac-sensors/v1.2/screenshots/imac14-1.png)

Note: the /dev/sensors nodes are root 0600 by default, so as a normal user
the app shows only the SMC values; that is a separate question for
illumos (minor_perm of ksensor).

## Follow-ups

- Upstream Makefile: `make install` twice into the same DESTDIR fails
  (writes over its own mode 0555 file); fix in 1.2.1 (rm -f / install(1)).
- ports/mac-sensors in this repo: build from the v1.2 tarball instead of
  the local copy (or retire it once the oi-userland package exists).
