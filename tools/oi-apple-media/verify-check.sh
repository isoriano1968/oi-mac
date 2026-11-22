#!/bin/sh
#
# Copyright 2026 isoriano1968. CDDL-1.0.
#
# verify-check.sh ACCEPT OUTPUT -- judge a "pkg verify" of an installed OI Mac
# system against the accepted findings (verify-accept).  OUTPUT is made with
#	pkg verify > OUTPUT 2>&1; echo "verify rc=$?" >> OUTPUT
# (pkg -R /a from the live system: mount the BE's /var and rpool/export
# first, else the directories under the mounts get checked).
#
# A finding is accepted only if package, path and error text are in the
# list; only the two values of a Hash error are not compared.  Anything else
# fails: another finding, a line not understood, output without the rc line.
# Exit 0 pass, 1 fail, 2 usage.  Runs on illumos and Linux.
#
if [ $# -ne 2 ] || [ ! -r "$1" ] || [ ! -r "$2" ]; then
	echo "usage: $0 ACCEPT OUTPUT" >&2
	exit 2
fi
AWK=/usr/bin/nawk
[ -x $AWK ] || AWK=awk
$AWK '
FILENAME == ARGV[1] {
	if ($0 !~ /^#/ && $0 != "") { ok[$0] = 1; nok++ }
	next
}
/^[ \t]*$/ || /^Planning: / || /^PACKAGE[ \t]+STATUS/ { next }
/^verify rc=/ { rc = substr($0, 11); ended = 1; next }
/^pkg:\/\// {
	p = $1; sub(/^pkg:\/\/[^\/]*\//, "", p); sub(/@.*/, "", p)
	path = "-"
	if ($2 != "ERROR") { print "unexpected: " $0; bad++ }
	inpkg++
	next
}
/^[ \t]+[a-z]+: / {
	path = $0; sub(/^[ \t]+/, "", path)
	next
}
/^[ \t]+ERROR: / {
	e = $0; sub(/^[ \t]+ERROR: /, "", e)
	if (e ~ /^Hash: /) e = "Hash"
	k = p "|" path "|" e
	n++
	if (k in ok) seen[k] = 1
	else { print "unexpected: " k; bad++ }
	next
}
{ print "not understood: " $0; bad++ }
END {
	if (!ended) { print "incomplete: no verify rc line"; bad++ }
	else if (rc != 0 && rc != 1) { print "pkg verify rc " rc; bad++ }
	else if (rc == 1 && !n) { print "rc 1 but no findings"; bad++ }
	for (k in ok) if (!(k in seen)) gone++
	printf("verify-check: %d findings, %d not accepted, %d of %d listed not seen: %s\n",
	    n, bad, gone, nok, bad ? "FAIL" : "pass")
	exit (bad ? 1 : 0)
}' "$1" "$2"
