#!/bin/ksh93
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# A copy of the CDDL is available at http://www.illumos.org/license/CDDL.
#
# Copyright 2026 isoriano1968
#
# build-media.sh text|gnome|mac -- build an OI-Apple medium with distro_const,
# from OpenIndiana hipster and the local publisher oi-local only.  Runs on
# OpenIndiana as an ordinary user with password-less sudo (distro_const
# needs root).  oi-local is served read-only on localhost for the build
# (im_pop.py cannot name the publisher of a file:// origin).
#
#   OI_LOCAL_REPO   oi-local repository (default ~/oi-local-repo)
#   DC_AREA         build area, a directory (default /rpool/dc, OI's own:
#                   the image keeps the area's path in its SMF repository
#                   and pkg history, so it must not name a user or home)
#   DEPOT_PORT      (default 10080)
#   AMIX_REPO       mac only: the image's amix repository, served on
#                   DEPOT_PORT + 1 (default ~/oi-mac-r1-v2/amix)
#   OI_MAC_DIAG=1   mac only: a diagnostic image (live boot state on the
#                   console and serial port), for tests, never published
#
# mac = OI Mac r1 (oi-mac-gnome.xml): OI_LOCAL_REPO is the image's own
# repository (default ~/oi-mac-r1-v2/oi-local), exactly the image's set.
#
set -e
FLAVOUR=${1:?usage: $0 text|gnome|mac [distro_const options]}
shift
HERE=$(cd "$(dirname "$0")" && pwd)
XML=oi-apple-$FLAVOUR.xml
if [[ $FLAVOUR == mac ]]; then
	XML=oi-mac-gnome.xml
	REPO=${OI_LOCAL_REPO:-$HOME/oi-mac-r1-v2/oi-local}
	AREPO=${AMIX_REPO:-$HOME/oi-mac-r1-v2/amix}
else
	REPO=${OI_LOCAL_REPO:-$HOME/oi-local-repo}
fi
AREA=${DC_AREA:-/rpool/dc}
[[ -d $AREA ]] || sudo -n /usr/bin/mkdir -p $AREA
[[ -w $AREA ]] || sudo -n /usr/bin/chown $(id -un) $AREA
PORT=${DEPOT_PORT:-10080}
OSNET=23395
CFG=$AREA/cfg-$FLAVOUR
URL=http://localhost:$PORT/
APORT=$((PORT + 1))
AURL=http://localhost:$APORT/

[[ -f $HERE/$XML ]] || { print -u2 "no flavour $FLAVOUR"; exit 2; }
mkdir -p $AREA $CFG
cp $HERE/oi-apple-media-mod $HERE/oi-mac-publishers $HERE/oi-mac-must-have $HERE/oi-mac-ba-mod $CFG/
chmod 0755 $CFG/oi-apple-media-mod $CFG/oi-mac-publishers $CFG/oi-mac-must-have $CFG/oi-mac-ba-mod
print "OI_LOCAL_URL=$URL\nOSNET_BUILD=$OSNET" > $CFG/oi-apple-media.conf
[[ $FLAVOUR == mac ]] && print "AMIX_URL=$AURL" >> $CFG/oi-apple-media.conf
# OI_MAC_DIAG=1: diagnostic image (oi-mac-publishers), never published
if [[ $OI_MAC_DIAG == 1 ]]; then print "OI_MAC_DIAG=1" >> $CFG/oi-apple-media.conf; fi
sed -e "s#@BUILD_AREA@#$AREA#g; s#@CFG@#$CFG#g; s#@DEPOT_PORT@#$PORT#g" \
    -e "s#@AMIX_PORT@#$APORT#g" $HERE/$XML > $CFG/$XML

/usr/lib/pkg.depotd -d $REPO -p $PORT --readonly --log-access none \
    --log-errors stderr >$AREA/depot-$FLAVOUR.log 2>&1 &
DEPOT=$!
ADEPOT=
if [[ $FLAVOUR == mac ]]; then
	/usr/lib/pkg.depotd -d $AREPO -p $APORT --readonly --log-access none \
	    --log-errors stderr >$AREA/depot-amix.log 2>&1 &
	ADEPOT=$!
fi
# pkg.depotd (cherrypy) does not always end on SIGTERM
# $! is not pkg.depotd itself (a ksh subshell that may already be gone):
# after start-up the depots' own pids, by their repository and port
DPIDS=
function stop_depots {
	typeset pids="$DEPOT $ADEPOT $DPIDS"
	kill $pids 2>/dev/null
	sleep 2
	kill -9 $pids 2>/dev/null
	return 0
}
trap stop_depots EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
	/usr/bin/curl -fs ${URL}versions/0/ >/dev/null && break
	sleep 2
done
/usr/bin/pkgrepo info -s $URL
[[ $FLAVOUR == mac ]] && /usr/bin/pkgrepo info -s $AURL
# pgrep -f sees only the first 80 characters of the arguments: match the
# whole command line with pargs
function depot_pid {
	typeset p
	for p in $(/usr/bin/pgrep -u $(id -u) -f pkg.depotd || true); do
		/usr/bin/pargs -l $p 2>/dev/null |
		    /usr/bin/grep -F -- "pkg.depotd -d $1 -p $2 " >/dev/null &&
		    print $p
	done
	return 0
}
DPIDS=$(depot_pid $REPO $PORT)
if [[ $FLAVOUR == mac ]]; then
	DPIDS="$DPIDS $(depot_pid $AREPO $APORT)"
fi
print "depot pids: $DPIDS"

# distro_const's exit status, not tee's: a failed build must not end with
# status 0 (ISO v9 "ended rc=0" after im-pop failed)
set +e
set -o pipefail
sudo -n /usr/bin/distro_const build "$@" $CFG/$XML 2>&1 |
    tee $AREA/dc-$FLAVOUR.log
DCRC=$?
set +o pipefail
set -e
OUT=oi-apple-$FLAVOUR
[[ $FLAVOUR == mac ]] && OUT=oi-mac-gnome
ls -l $AREA/$OUT/media/ || true
# ksh93 may exec the last command in place of the shell and then never
# runs the EXIT trap: stop the depots here as well
stop_depots
print "distro_const rc=$DCRC"
exit $DCRC
