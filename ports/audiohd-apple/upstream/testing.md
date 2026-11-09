# Testing (for the issue's "Testing Done" and the RTI)

## Built

- Patch `0001-NNNNN-audiohd-...patch` is against illumos-gate master
  b1c424a261 (2026-11-08). audiohd is unchanged from 08e59b676b (the iMac's
  kernel) to master, so the same patch is in ../patches/ and builds the test
  module.
- **Not a gate build.** The audiohd module was compiled out of tree on an
  OpenIndiana 2026.10 VM (`ports/audiohd-apple`, `gmake`): gcc 10.5 with the
  flags of usr/src/uts/Makefile.uts + uts/intel/Makefile.intel (-mcmodel=kernel,
  -msave-args, retpolines, -Wall -Werror with the release CERRWARN list and
  audiohd's own), `ld -r -dy -N drv/audio`, ctfconvert. 0 warnings. Every
  undefined symbol resolves in unix, genunix and drv/audio.
- `cstyle -cpP audiohd.c audiohd.h` and `hdrchk audiohd.h` (onbld 2026):
  nothing new. The one cstyle complaint (audiohd.c, audiohd_select_mic()
  continuation line) and the hdrchk header-guard style complaint are in
  existing code and appear the same on the unpatched files.
- **Still to do before the RTI:** a full nightly build of illumos-gate with
  the patch (gcc 10 primary, gcc 14 + smatch shadows, clean mail_msg), and
  `git pbchk`. Neither was done here.

## Tested so far (iMac13,2, Cirrus CS4206, OI 2026.10, illumos-08e59b676b)

| What | Result |
|---|---|
| v1 (all CS4206, no subsystem check): `audiotest -2 /dev/sound/audiohd:0dsp`, internal speakers | **OK, heard** (user, 2026-11-09 08:55) |
| v1: vlc playing a sound file to /dev/dsp from the MATE session | played, not yet confirmed by ear |
| v2 (this patch) loaded: BE gnome45, one boot (`beadm activate -t`), 2026-11-09 09:34, sha256 9831202b…4847 | OK |
| v2: codec subsystem IDs read with mdb | audiohd#0 CS4206 vid 0x10134206, **ssid 0x106b5c00** (Apple, so the Apple path is taken); audiohd#1 NVIDIA vid 0x10de0040, ssid 0x106b010c (no APPLE_AMP_GPIO flag, unchanged) |
| v2: speakers, no headphones: `audiotest -2 /dev/sound/audiohd:0dsp` | **OK, heard**, left and right correct (user, 2026-11-09 ~10:20) |
| v2: headphones plugged in, same audiotest | **OK, heard in the headphones**; the speakers kept playing too (user, same time). The jack was plugged in before playback started. |
| HDMI codec (audiohd#1, NVIDIA 0x10de0040) | not tested by ear (no display with speakers). The codec is unchanged by the patch (no APPLE_AMP_GPIO flag); its subsystem ID was read as above |
| a non-Apple machine (should behave exactly as before) | not available |

Speakers staying on with headphones plugged in is today's audiohd
behaviour, not a regression: audiohd mutes the speaker pin only when it sees
a jack unsolicited response in its playback interrupt
(audiohd_pin_sense()), so a jack plugged in before playback starts is not
noticed. The patch only enables the amplifiers.

## What the user must test before submitting (exact steps)

The v2 module is already in the iMac's boot environment `gnome45`
(sha256 9831202b0f58e525acacfd1ce26ea0c26261171fef37ccc555dd5935e7ab4847).
To test it on its own instead: create a one-boot BE as below.

1. Boot the v2 driver for one boot only:

       sudo beadm activate -t gnome45      # or a fresh BE, see below
       sudo reboot -p

   (A fresh BE: `sudo beadm create audiohd-v2 && sudo beadm mount audiohd-v2
   /mnt/v2 && sudo cp audiohd /mnt/v2/kernel/drv/amd64/audiohd &&
   sudo bootadm update-archive -R /mnt/v2 && sudo beadm umount audiohd-v2 &&
   sudo beadm activate -t audiohd-v2`.) The next reboot returns to the
   active BE.

2. Check the driver and the codec subsystem ID (must start with 106b):

       digest -a sha256 /kernel/drv/amd64/audiohd
       for a in $(echo '::devbindings -q audiohd' | sudo mdb -k | awk '{print $1}'); do
           echo "$a::print struct dev_info devi_driver_data | ::print audiohd_state_t codec[0] | ::print hda_codec_t vid ssid" | sudo mdb -k
       done

   (`::devbindings -q` prints a name after each address, so the addresses
   are taken one by one.)

   (audiohd keeps its state as the devinfo's driver data; the module has
   CTF, so mdb knows hda_codec_t.)

3. Speakers: `sudo audiotest -2 /dev/sound/audiohd:0dsp`. Expect left,
   right and stereo tones from the internal speakers.

4. Headphones: plug headphones into the jack, run the same audiotest.
   Expect the tones in the headphones. Note whether the speakers also keep
   playing (expected with this patch: there is no automute).

5. HDMI/DisplayPort audio: with a monitor with speakers on the Mini
   DisplayPort/Thunderbolt port, `sudo audiotest -2
   /dev/sound/audiohd:1dsp`. Without one, at least check that `audioctl
   show-device -d 'audiohd#1'` still shows "Unknown HD codec: 0x10de0040"
   and that audiotest reports OK.

6. Suspend/resume: audiohd_resume() also goes through the changed code
   (audiohd_restore_codec_gpio()). Not tested; say so in the issue.

Put the results in the issue as "Testing Done".
