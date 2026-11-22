#!/bin/sh
#
# gen-mog.sh REPO MANIFEST NEWVERSION [PINS] -- pkgmogrify transforms for
# userland-incorporation-apple: the new version and publisher, and, for
# every package that both OI's incorporation locks and oi-local carries,
# the lock moved to oi-local's newest version of it, or, with PINS (lines
# "name version", # comments), to the version PINS names (an image's set).
#
# Copyright 2026 isoriano1968. CDDL-1.0.
#
REPO=$1 MAN=$2 NEW=$3 PINS=$4
echo "<transform set name=pkg.fmri -> edit value '@[^:]*' '@$NEW'>"
echo "<transform set name=pkg.fmri -> edit value 'pkg://[^/]*/' 'pkg://oi-local/'>"
echo "<transform set name=pkg.description -> edit value '\$' ' (OI-Apple: the oi-local builds of GNOME 45, Xorg 21.1, Mesa 26 and the Apple ports)'>"
if [ -n "$PINS" ]; then
	/usr/bin/nawk '!/^#/ && NF == 2' "$PINS" | sort > /tmp/uia.$$
else
	/usr/bin/pkgrepo list -H -s "$REPO" | /usr/bin/nawk '
	    { n = $2; v = $NF; sub(/:.*/, "", v); if (!(n in seen)) { seen[n] = v } }
	    END { for (n in seen) print n, seen[n] }' | sort > /tmp/uia.$$
fi
/usr/bin/nawk '$1 == "depend" && /type=incorporate/ {
	for (i = 2; i <= NF; i++) if ($i ~ /^fmri=/) {
		f = $i; sub(/^fmri=(pkg:\/)?/, "", f); sub(/@.*/, "", f); print f } }' "$MAN" |
    sort -u > /tmp/uia-inc.$$
/usr/bin/join /tmp/uia.$$ /tmp/uia-inc.$$ | while read n v; do
	# n@<version> -> n@<oi-local version> (the build part ,5.11 dropped)
	e=$(echo "$n" | sed 's#[.+/]#\\&#g')
	echo "<transform depend type=incorporate fmri=(pkg:/)?$e@.* -> edit fmri '@.*' '@$v'>"
done
rm -f /tmp/uia.$$ /tmp/uia-inc.$$
