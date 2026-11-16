# illumos: console fonts larger than 16x32 (Spleen 32x64 for HiDPI)

Found 2026-11-16 while testing `system/font/console/spleen` and
`system/apple/hidpi` (the 32x64 console font for the MacBookPro14,3's
2880x1800 panel). The test ran on the Linux workstation, with no Mac: OI-hipster-minimal-20261030
(osnet c893fd03ad = the MBP's kernel), headless QEMU/KVM + OVMF (edk2
2025, Debian), VGA with EDID at 1280x800 and at 2880x1800, plus
`/boot/fonts/spleen-32x64.fnt` and `/boot/conf.d/apple-hidpi` added to a
copy of the ISO with xorriso. Screens came from the QEMU monitor's
`screendump`; keys went through its `sendkey`.

## Results

| test | result |
|---|---|
| vtfontcvt + patch, all glyphs vs the BDF (Linux workstation checker) | 12x24, 16x32, 32x64: every mapped glyph identical; 12x24/16x32 byte-identical to the stock tool |
| loader draws Spleen 32x64 | yes (menu at 2880x1800: 90x28, crisp) |
| conf.d `exec="loadfont …"` alone | **no effect**: the loader stays on its automatic font (14x28) |
| conf.d `exec` + `screen-font="32x64"` | loader uses 32x64 |
| kernel boots with the 32x64 font module | **panic**: `BAD TRAP: type=e (#pf Page fault) rp=fffffffffbcaff90 addr=fffffe0646e97008`, thread t0, right after the banner; no stack printed |
| control: same path with Spleen 16x32 | boots to the installer, kernel console in Spleen 16x32 |

So a 32x64 font installed today makes OI 2026.10 **unbootable**. The
hidpi package must not be installed on any machine until patch 0001 is in
the kernel it boots.

## Cause 1: kernel early console glyph buffer (patch 0001)

`uts/i86pc/boot/boot_fb.c` (unix, BOOT_DRIVER_OBJS):

```c
/* We have largest font 16x32 with depth 32. This will allocate 2048 bytes from BSS. */
#define MAX_GLYPH (16 * 32 * 4)
static uint8_t glyph[MAX_GLYPH];
```

`bit_to_pix()` expands every character into `glyph[]` for the font that
the loader handed over (`xbi_init_font()`, which takes any size). A
32x64 glyph at 32 bpp needs 8192 bytes, so each character overwrites
6 KB of the BSS after it. The later kernel tem allocates its buffer from
the font's size (`ts_pix_data_size`), and `font_bit_to_pix*()` are
generic; only the early console is limited.

`0001-boot_fb-glyph-buffer-for-console-fonts-up-to-32x64.patch`:
- the buffer covers 32x64 at 32 bpp in unix (8 KB BSS). boot_fb.c is
  also built into dboot (BOOT_DRIVER_OBJS, -D_BOOT), which keeps the
  old 16x32 buffer and so ignores a larger font module (built-in font
  for dboot's few messages) instead of overflowing its 2 KB buffer;
- `xbi_init_font()` ignores a font module whose glyphs are larger
  (0 or > 32 wide, 0 or > 64 high), so the kernel falls back to its
  built-in font instead of overflowing.

Checked: gcc 14 `-fsyntax-only -Wall` against the full 586678ac tree, on
the Linux workstation (boot_fb.c is the same in c893fd03ad). Not built into a unix yet
(that needs the OI VM). Test: a BE with the patched unix + the hidpi
package, booted first in QEMU (OI VM image or this ISO with the patched
unix and boot_archive), then on the MBP with the user's OK.

## Cause 2: loader `loadfont` of a size already in the list (patch 0002)

- The loader reads `/boot/conf.d` twice (the first pass shows `screen-font`
  32x64, the second 14x28; debug snippet `exec="show screen-font"`). Why
  the Forth code reads the directory twice (`include_conf_files` /
  `get_conf_files` → `scan_conf_dir`) is not yet clear; it is harmless
  for plain `name=value` lines.
- In the second pass, `insert_font()` finds an entry of the same size
  (the Spleen file itself) and only sets FONT_RELOAD. The manual flag is
  lost and `set_font()` takes the automatic font. The same happens at
  the prompt with `loadfont /boot/fonts/16x32.fnt`, because the stock
  16x32 is already in the list.

`0002-loader-loadfont-of-a-known-font-size-stays-manual.patch`
(`boot/common/gfx_fb.c`, `insert_font()`): for a manual load, the entry
becomes the manual font. The same file keeps its loaded glyphs; a
different file of that size is loaded now, so the entry does not keep
the old glyphs. Checked: gcc 14 `-fsyntax-only -Wall -Werror` with the
EFI loader's include paths, on the Linux workstation.

Workaround without 0002, already in
`packages/system/apple-hidpi/files/apple-hidpi-font`: a
`screen-font="32x64"` line after the `exec`. Its hook (`font_set()`)
marks the font manual again in each pass. Tested in QEMU: the loader is
on 32x64 after both passes.

## Tested 2026-11-16 (QEMU on the Linux workstation, 2880x1800)

Built on the OI VM in ~/src/illumos-macboot (worktree, branch
macboot-c893 = c893fd03ad + AP-start 0001 + 0001 + 0002 here; bldenv +d,
`uts/intel all.prereq`, then `uts/i86pc/unix`, then `boot`; userboot
fails to link and is not needed). unix sha256 065c387e…2507,
loader64.efi 73a9247b…a76b.

| ISO | result |
|---|---|
| patched unix + stock loader + apple-hidpi-font (exec + screen-font) | boots to the installer; banner `macboot-c893-0-g03560a7e`; kernel console in Spleen 32x64 |
| stock unix + patched loader + `exec="loadfont …"` alone | loader menu in 32x64 (stock loader: 14x28); kernel panics (stock boot_fb), as expected |
| patched unix + patched loader + `exec` alone | loader and kernel console in 32x64, installer reached |

Trap for testing a new unix on the ISO: **krtld takes the symbols of unix
from the copy inside boot_archive**, not from the unix the loader loaded.
With only /platform/i86pc/kernel/amd64/unix replaced, every unix whose
layout differs from the stock one traps in early boot (fakebop
"Unexpected trap", kobj_init calling stock addresses); a rebuild without
patches booted only because its layout matched exactly. The test ISOs
therefore carry the patched unix in the boot_archive too (gunzip, lofi
mount on the VM, copy, gzip -9; boot_archive.hash = sha1 of the
uncompressed archive). On an installed system bootadm update-archive
does this.

## Next

1. Done (above). The workaround line in apple-hidpi-font stays until the
   loader with 0002 is what the machines boot.
2. KMS consoles (i915n, amdgpu con_kms) take over from boot_fb and draw
   through tem with the same font: the graphics session checks them at
   32x64 on the iMac once 0001 is in a kernel; until then it ships
   nothing larger than 16x32 on the Macs (agreed 2026-11-16).
3. apple-hidpi: declare a dependency on a kernel with 0001 (once we
   publish one), so the font cannot be installed onto a kernel that
   panics with it.
4. Upstream: an illumos issue for each (boot_fb overflow; loadfont of a
   known size; conf.d read twice). Filing them is the user's decision.
