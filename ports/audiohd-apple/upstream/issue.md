# illumos-gate issue (draft, to be filed by the user)

**Project:** illumos gate · **Tracker:** Bug · **Category:** driver - audio
(or kernel) · **Subject:**

    audiohd: Apple iMac/MacBook CS4206 speakers silent (GPIO amplifier enable)

## Description

On Apple machines with a Cirrus Logic CS4206 (or CS4207) HD Audio codec, no
sound comes out of the internal speakers or the headphone jack, although
playback otherwise works. `audiotest` reports every channel as OK at the
right sample rate, `audioctl` shows sensible volumes, and the device links
are correct, but nothing is heard.

Seen on an iMac13,2 (27-inch, Late 2012) running OpenIndiana 2026.10
(illumos-08e59b676b):

- HD Audio controller: Intel 8086:1e20 rev 04 (7 Series PCH), PCI subsystem
  8086:7270 (not Apple's ID), driver audiohd, instance 0
- codec: Cirrus CS4206 (vendor/device 0x10134206), subsystem 0x106b5c00,
  "HD codec: Cirrus CS4206"
- second instance: NVIDIA 10de:0e0a (GK104 HDMI audio), PCI subsystem
  106b:010c, codec 0x10de0040, subsystem 0x106b010c (not affected: no
  APPLE_AMP_GPIO flag)

### Cause

audiohd_create_codec() and audiohd_restore_codec_gpio() program every codec
that does not have NO_GPIO with GPIO mask 0xff, data 0xff and **direction
0xf1**, which leaves GPIO1, GPIO2 and GPIO3 as inputs.

On Apple's CS4206/CS4207 boards those GPIOs switch the external amplifiers:
GPIO3 the speaker amplifier, GPIO1 or GPIO2 the headphone amplifier. With
them as inputs both amplifiers stay off. Linux handles this in
sound/hda/codecs/cirrus/cs420x.c (formerly sound/pci/hda/patch_cirrus.c),
fixups `gpio_13` (GPIO1 = headphones, GPIO3 = speakers; the default for
every Apple codec subsystem, `SND_PCI_QUIRK_VENDOR(0x106b, "Apple",
CS420X_APPLE)`) and `gpio_23` (GPIO2/GPIO3; iMac12,1, iMac12,2, iMac14,1).
There `gpio_mask = gpio_dir = hp | speaker`, and the data turns the
amplifiers on (cs_automute()).

### Fix

- Read the codec's subsystem ID (verb 0xf20 on the audio function group)
  and keep it in hda_codec_t (`ssid`).
- New codec flag APPLE_AMP_GPIO, set for the CS4206 and for the CS4207
  (new table entry).
- For a codec with APPLE_AMP_GPIO **and** an Apple subsystem vendor (0x106b,
  as Linux matches it), use GPIO direction 0xff instead of 0xf1. Data stays
  0xff, so GPIO1-3 are driven high and both amplifiers are on. Every other
  codec, including a CS4206/CS4207 on a non-Apple board, keeps today's
  0xf1.

The controller's PCI subsystem cannot be used for this: on the iMac13,2 it
is Intel's 8086:7270. The codec's own subsystem ID is Apple's on the Macs
seen in public dumps, e.g. 0x106b5100 (iMac11,1) and 0x106b5500 (MacBook
Air 5,1). On the tested iMac13,2 the CS4206 reports 0x106b5c00.

Both amplifiers are always on (there is no jack-sense automute of the
speakers yet, which audiohd does not do for the other codecs either).

### Testing

Testing Done (iMac13,2, OpenIndiana 2026.10, illumos-08e59b676b + this
patch, module built out of tree with the gate's flags):

- codec subsystem IDs read with mdb: CS4206 0x106b5c00 (Apple path taken),
  NVIDIA HDMI 0x106b010c (unchanged)
- `audiotest -2` on the CS4206: tones heard from the internal speakers, left
  and right correct; with headphones: heard in the headphones (the speakers
  keep playing, see below)
- before the patch: audiotest "OK", but nothing heard
- not tested: HDMI audio by ear (no display with speakers), suspend/resume,
  other Apple models, a non-Apple CS4206 board, a full gate build

The speakers keep playing with headphones plugged in before playback starts:
audiohd only reacts to jack events in its playback interrupt
(audiohd_pin_sense()). That is unchanged by this fix.

### References

- Linux: sound/hda/codecs/cirrus/cs420x.c, cs420x_fixup_gpio_13(),
  cs420x_fixup_gpio_23(), cs420x_fixup_tbl[];
  commit "ALSA: hda/cirrus - support for iMac 12,1 model" (2022)
- FreeBSD hdaa(4): CS4206 on Apple boards (hdaa_patches.c, GPIO patches)
