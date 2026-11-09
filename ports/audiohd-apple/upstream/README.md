# audiohd Apple CS4206 fix: upstream submission (prepared, not submitted)

Everything here is local. Filing the issue and uploading the change are the
user's steps, from their own accounts. Nothing has been posted.

| File | What |
|---|---|
| `0001-NNNNN-audiohd-Apple-iMac-MacBook-CS4206-speakers-sil.patch` | the change, `git format-patch` against illumos-gate master b1c424a261 (2026-11-08); author isoriano1968, no trailers |
| `issue.md` | text for the illumos-gate issue (symptom, cause, Linux references, hardware) |
| `testing.md` | what was built and tested, and the exact tests still to do |

Commit message format (illumos): one line, `<issue number> <synopsis>`,
here `NNNNN audiohd: Apple iMac/MacBook CS4206 speakers silent (GPIO
amplifier enable)`. Reviewers' lines are added later as `Reviewed by: Name
<email>`, with no blank line after the first line; the integrator adds
`Approved by:`. illumos commits carry no body: the explanation goes in the
issue. Replace NNNNN with the issue number once it is filed.

## Process, as read on 2026-11-09

Source: https://illumos.org/docs/contributing/ ("How To Contribute"). The
Gerrit details below are Gerrit's general workflow; the page does not give
push commands, so check the "Download"/"Settings" pages on code.illumos.org
for the exact host and port.

1. **Test first** (testing.md: v2 on the iMac, the subsystem ID, speakers,
   headphones, HDMI). Update issue.md with the iMac13,2 codec subsystem ID.
2. **Account:** register in the illumos bug tracker,
   https://www.illumos.org/projects/illumos-gate/issues. Gerrit
   (https://code.illumos.org) uses the same credentials.
3. **Issue:** file it at
   https://www.illumos.org/projects/illumos-gate/issues/new with the text of
   issue.md. Note the number.
4. **Commit:** in a clone of https://github.com/illumos/illumos-gate:
   `git am 0001-NNNNN-*.patch`, then `git commit --amend` to put the real
   number in place of NNNNN.
5. **Build:** a full nightly build with the change (GCC 10 primary, GCC 14
   and smatch shadows), clean `mail_msg`, and `git pbchk`
   (https://illumos.org/docs/developers/build/). The out-of-tree build here
   does not replace it.
6. **Review on Gerrit:** add Gerrit's commit-msg hook (adds the Change-Id
   line), then `git push <code.illumos.org remote> HEAD:refs/for/master`.
   Ask for at least one reviewer (audio/driver people; the illumos developer
   list if nobody answers). Put "Testing Done" in the issue.
7. **RTI:** after review, mail advocates@lists.illumos.org with the issue
   link, the Gerrit link, the mail_msg and the testing notes. A core team
   member integrates it; then abandon the Gerrit change ("integrated").

## Local use until it is integrated

ports/audiohd-apple builds the module out of tree for the iMac's own kernel
(Makefile, `patches/` = the same patch). It is installed by hand in boot
environments (see notes/imac2019-network.md, section 6), and a `pkg update`
that replaces driver/audio/audiohd brings back the silent stock driver.
