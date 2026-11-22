#!/bin/bash
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# Copyright 2026 isoriano1968
#
# privacy-scan: release gate of the public OI Mac image.  Any hit fails
# (exit 1): nothing of the builder (users, keys, networks, names, paths,
# logs, caches, private package origins) may be in an image given away.
#
#   privacy-scan.sh tree ROOT      an image tree (distro_const's pkg_image,
#                                  as a finalizer right before create_iso)
#   privacy-scan.sh iso ISO        the finished ISO, without mount or root
#                                  (Linux or illumos; needs 7z, python3,
#                                  gzip, ~12 GB in $WORK)
#   privacy-scan.sh installed ROOT an installed system (/ on the system,
#                                  or the altroot of its imported rpool):
#                                  the same checks, and nothing of the live
#                                  system: no jack (account, home, GDM
#                                  auto-login), no /.livecd, no NOPASSWD
#                                  sudo rule for anyone; the user made in
#                                  the installer is named in ALLOW_USERS
#                                  (e.g. ALLOW_USERS=tester), every other
#                                  account fails
#   privacy-scan.sh selftest ISO   must find the known leftovers of an old
#                                  test image: proves the gate works
#
# Live image policy (user, 2026-11-19): the live user jack keeps
# "jack ALL=(ALL) NOPASSWD: ALL" on the live stick only; that one rule is
# accepted in tree/iso mode and in no other form.
#
# What it looks at:
#   - accounts, homes, sudo rules, stored networks, node name, logs,
#     crash data and package caches (structure);
#   - files that must not exist anywhere (keys, histories, host ids);
#   - package origins (no file://, localhost or private addresses);
#   - the builder's identifiers in every file, binaries included:
#     privacy-ids.local next to this script (or $PRIVACY_IDS), fixed
#     strings, case-insensitive, one per line, git-ignored, never in the
#     image or the repository;
#   - PRIVACY_PUBLIC (off unless set): public strings that contain an
#     identifier and may stay, space-separated, e.g. a public handle that
#     starts with the login; only that exact string (any case) is taken,
#     the identifier alone or in any other word still fails;
#   - generic patterns (private IPv4, MAC and mail addresses, keys) in the
#     files the packages do not deliver as they are (pkg-undelivered.py,
#     from the manifests the image carries): what the build added;
#   - for an ISO also the .p5p archives on it and its raw bytes.
# privacy-allow: regexes of accepted hits ("label: detail"), with a reason
# each; a stock OpenIndiana image needs a few.
#
# Exit: 0 clean, 1 hits (on stdout; KEEP_HITS=file keeps them), 2 error.

set -u
export LC_ALL=C
[ -d /usr/gnu/bin ] && PATH=/usr/gnu/bin:$PATH
HERE=$(cd "$(dirname "$0")" && pwd)
IDS=${PRIVACY_IDS:-$HERE/privacy-ids.local}
ALLOW=${PRIVACY_ALLOW:-$HERE/privacy-allow}
# Accounts and names a public live image may have.
case ${1:-} in
installed)
	ALLOW_USERS=${ALLOW_USERS:-}
	ALLOW_HASH_USERS=${ALLOW_HASH_USERS:-root}
	LIVE_SUDO_RE='^$';;
*)
	ALLOW_USERS=${ALLOW_USERS:-jack}
	ALLOW_HASH_USERS=${ALLOW_HASH_USERS:-root jack}
	# the live user's rule, exactly
	LIVE_SUDO_RE='^[0-9]+:jack[[:space:]]+ALL=\(ALL\)[[:space:]]+NOPASSWD:[[:space:]]*ALL[[:space:]]*$';;
esac
ALLOW_NODENAMES=${ALLOW_NODENAMES:-openindiana}

die() { echo "privacy-scan: $*" >&2; exit 2; }
[ $# -eq 2 ] || die "usage: privacy-scan.sh tree ROOT | iso ISO | installed ROOT | selftest ISO"
[ -s "$IDS" ] || die "no identifier list $IDS (see the header)"

WORK=${WORK:-}
if [ -z "$WORK" ]; then
	WORK=$(mktemp -d "${TMPDIR:-/tmp}/privacy-scan.XXXXXX") || die mktemp
	trap 'rm -rf "$WORK"' EXIT
else
	mkdir -p "$WORK" || die "cannot create $WORK"
fi
HITS=$WORK/hits
: > "$HITS"
IDS_RE=$WORK/ids
grep -v '^#' "$IDS" | grep -v '^[[:space:]]*$' > "$IDS_RE"
[ -s "$IDS_RE" ] || die "identifier list $IDS is empty"
# PRIVACY_PUBLIC: grep -o reports the longest match at a place, so with the
# public strings among the patterns a public string comes out whole and is
# dropped; an identifier anywhere else still comes out as itself
PUB=$WORK/public
PUB_RE=$WORK/ids+public
: > "$PUB"
for w in ${PRIVACY_PUBLIC:-}; do printf '%s\n' "$w"; done |
    tr '[:upper:]' '[:lower:]' > "$PUB"
cat "$IDS_RE" "$PUB" > "$PUB_RE"
ids_in() {	# stdin: grep -o output; stdout: the identifiers, public dropped
	tr '[:upper:]' '[:lower:]' | grep -v -x -F -f "$PUB" 2>/dev/null ||
	    true
}
ALLOW_RE=$WORK/allow
grep -v '^#' "$ALLOW" 2>/dev/null | grep . > "$ALLOW_RE"

hit() {
	[ -s "$ALLOW_RE" ] && printf '%s\n' "$*" | grep -q -E -f "$ALLOW_RE" &&
	    return
	printf '%s\n' "$*" >> "$HITS"
}

# ---- structure of an image tree --------------------------------------------
# $1 tree, $2 the image path of the tree's top ("" or "usr/")

check_users() {
	local r=$1 u
	[ -f "$r/etc/passwd" ] || return
	while IFS=: read -r name _ uid _ _ home _; do
		[ -n "$uid" ] || continue
		if { [ "$uid" -ge 100 ] && [ "$uid" -lt 60000 ]; } ||
		   [ "$uid" -eq 65432 ]; then
			case " $ALLOW_USERS " in *" $name "*) continue;; esac
			hit "users: etc/passwd: $name uid $uid home $home"
		fi
	done < "$r/etc/passwd"
	for u in "$r"/export/home/*; do
		[ -e "$u" ] || continue
		case " $ALLOW_USERS " in *" ${u##*/} "*) continue;; esac
		hit "users: home directory ${u#$r/}"
	done
	[ -f "$r/etc/shadow" ] && awk -F: -v ok=" $ALLOW_HASH_USERS " \
	    '$2 ~ /^\$/ && index(ok, " " $1 " ") == 0 { print $1 }' \
	    "$r/etc/shadow" | while read -r u; do
		hit "users: etc/shadow: password hash for $u"
	done
}

check_sudo() {
	local r=$1 f
	for f in "$r"/etc/sudoers.d/*; do
		[ -f "$f" ] && hit "sudo: ${f#$r/}: $(grep -v '^#' "$f" |
		    grep . | head -c 160 | tr '\n' ' ')"
	done
	[ -f "$r/etc/sudoers" ] && grep -n 'NOPASSWD' "$r/etc/sudoers" |
	    grep -v '^[0-9]*:[[:space:]]*#' | grep -v -E "$LIVE_SUDO_RE" |
	    while read -r l; do
		hit "sudo: etc/sudoers: $l"
	done
	# NOPASSWD in sudoers.d is reported with the file above
}

# Installed system: nothing of the live system may be left.
check_installed() {
	local r=$1 f
	grep -q '^jack:' "$r/etc/passwd" 2>/dev/null &&
	    hit "installed: etc/passwd still has the live user jack"
	grep -q '^jack:' "$r/etc/shadow" 2>/dev/null &&
	    hit "installed: etc/shadow still has the live user jack"
	[ -e "$r/jack" ] && hit "installed: /jack (live home) still there"
	[ -e "$r/.livecd" ] && hit "installed: /.livecd still there"
	for f in "$r"/etc/gdm/custom.conf "$r"/etc/gdm/*.conf; do
		[ -f "$f" ] || continue
		grep -E -i -q '^[[:space:]]*AutomaticLogin[[:space:]]*=[[:space:]]*jack' 		    "$f" && hit "installed: ${f#$r/}: auto-login for jack"
	done
	grep -r -l -E '(^|[[:space:]])jack([[:space:]]|$)' 	    "$r/etc/sudoers" "$r/etc/sudoers.d" "$r/etc/user_attr" 	    "$r/etc/user_attr.d" 2>/dev/null | while read -r f; do
		hit "installed: ${f#$r/} names jack"
	done
}

# Never anywhere.
BAD_NAMES='authorized_keys authorized_keys2 known_hosts id_rsa id_rsa.pub
id_dsa id_dsa.pub id_ecdsa id_ecdsa.pub id_ed25519 id_ed25519.pub
.bash_history .sh_history .zsh_history .lesshst .viminfo .python_history
.wget-hsts .mysql_history .psql_history .node_repl_history .netrc
.git-credentials .Xauthority .ICEauthority .xsession-errors'
# Never outside /usr (programs of these names are fine).
CONF_NAMES='hostid machine-id zpool.cache core'
check_names() {
	local r=$1 top=$2 n args=()
	for n in $BAD_NAMES; do args+=(-o -name "$n"); done
	if [ "$top" != usr/ ]; then
		for n in $CONF_NAMES; do args+=(-o -name "$n"); done
		args+=(-o -name 'ssh_host_*key*' -o -name '*.dhc' \
		    -o -name 'vmcore.*' -o -name 'unix.[0-9]*')
	fi
	find "$r" \( -false "${args[@]}" \) \( -type f -o -type l \) \
	    2>/dev/null | while read -r f; do
		case "$top${f#$r/}" in usr/*) [ "${f##*/}" = core ] &&
		    continue;; esac
		hit "file: $top${f#$r/}"
	done
}

check_config() {
	local r=$1 f n c
	for f in etc/dladm/secobj.conf etc/ipadm/ipadm.conf; do
		[ -f "$r/$f" ] || continue
		grep -v '^[[:space:]]*#' "$r/$f" | grep -q . &&
		    hit "network: $f: $(grep -v '^#' "$r/$f" | head -c 160 |
		    tr '\n' ' ')"
	done
	for f in "$r"/etc/hostname.* "$r"/etc/dhcp.*; do
		[ -f "$f" ] && [ -s "$f" ] && hit "network: ${f#$r/} not empty"
	done
	for f in $(find "$r/etc/nwam" -type f 2>/dev/null); do
		case "$f" in *known*|*wlan*|*wifi*) [ -s "$f" ] &&
		    hit "network: ${f#$r/} (stored WLANs)";; esac
	done
	if [ -f "$r/etc/nodename" ]; then
		n=$(head -1 "$r/etc/nodename")
		case " $ALLOW_NODENAMES " in *" $n "*) ;;
		*) hit "name: etc/nodename: $n";; esac
	fi
	find "$r/var/log" "$r/var/adm" -type f -size +0 2>/dev/null |
	    while read -r f; do hit "log: ${f#$r/} not empty"; done
	for f in var/crash var/cores var/pkg/cache var/pkg/download \
	    var/tmp tmp root/.cache; do
		[ -d "$r/$f" ] || continue
		c=$(find "$r/$f" -type f 2>/dev/null | wc -l)
		[ "$c" -gt 0 ] && hit "leftover: $f has $c files"
	done
	c=$(find "$r"/var/pkg/publisher/*/file -type f 2>/dev/null | wc -l)
	[ "$c" -gt 0 ] && hit "leftover: var/pkg/publisher/*/file has $c files"
}

check_publishers() {
	local r=$1 f
	for f in "$r/var/pkg/pkg5.image" "$r/var/pkg/cfg_cache"; do
		[ -f "$f" ] || continue
		grep -n -o -E -i '(file://|https?://(localhost|127\.|10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[01])\.))[^ ,"]*' \
		    "$f" | sort -u | while read -r l; do
			hit "pkg: ${f#$r/}: $l"
		done
	done
}

check_ids() {	# $1 tree, $2 label
	local d=$1 label=$2 f found
	grep -r -l -a -F -i -f "$IDS_RE" "$d" 2>/dev/null |
	    while read -r f; do
		found=$(grep -a -o -F -i -f "$PUB_RE" "$f" | ids_in |
		    sort -u | head -3 | tr '\n' ' ')
		[ -n "$found" ] && hit "id: $label${f#$d/}: $found"
	    done
}

GEN_RE='(^|[^0-9.])(10\.[0-9]{1,3}|192\.168|172\.(1[6-9]|2[0-9]|3[01]))\.[0-9]{1,3}\.[0-9]{1,3}([^0-9.]|$)|(^|[^0-9a-fA-F:])([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}([^0-9a-fA-F:]|$)|[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+\.[A-Za-z0-9.-]*[A-Za-z]{2,}|PRIVATE KEY-----|(ssh-(rsa|ed25519|dss)|ecdsa-sha2-nistp[0-9]+) AAAA'
check_added() {	# $1 undelivered list ("status image-path tree-path")
	local st img p
	while read -r st img p; do
		case "$img" in var/pkg/*|var/sadm/*|var/cache/fontconfig/*)
			continue;; esac
		grep -I -n -o -E "$GEN_RE" "$p" 2>/dev/null | sort -u |
		    while IFS= read -r l; do
			hit "pattern: $img ($st): $l"
		    done
	done < "$1"
}

manifest_dirs() {	# trees... -> var/pkg/publisher/*/pkg directories
	local t
	for t in "$@"; do
		ls -d "$t"/var/pkg/publisher/*/pkg 2>/dev/null
	done
}

scan_trees() {	# "top=tree" ...
	local s top tree
	for s in "$@"; do
		top=${s%%=*} tree=${s#*=}
		echo "== tree /$top (${tree##*/})"
		check_users "$tree"
		check_sudo "$tree"
		check_names "$tree" "$top"
		check_config "$tree"
		check_publishers "$tree"
		check_ids "$tree" "/$top"
	done
	echo "== files the packages do not deliver"
	local mdirs
	mdirs=$(manifest_dirs $(for s in "$@"; do echo "${s#*=}"; done))
	if [ -z "$mdirs" ]; then
		hit "tool: no package manifests in the image"
		return
	fi
	python3 -I "$HERE/pkg-undelivered.py" $mdirs -- "$@" \
	    > "$WORK/undelivered" 2>/dev/null ||
	    hit "tool: pkg-undelivered.py failed"
	cp "$WORK/undelivered" "$WORK/undelivered.list"
	check_added "$WORK/undelivered"
}

# ---- ISO ------------------------------------------------------------------

need() { command -v "$1" >/dev/null || die "$1 is needed"; }

x7z() {	# $1 archive, $2 dest, rest: 7z options; only links 7z deems unsafe
	# ("Dangerous link path", "... via another link") may fail: their
	# targets are files of the tree, scanned as such
	local a=$1 d=$2
	shift 2
	7z x -y -bd -o"$d" "$@" "$a" > "$WORK/7z.log" 2>&1 && return 0
	grep -E '^ERROR' "$WORK/7z.log" | grep -v 'Dangerous link' |
	    grep -q . && { hit "tool: 7z cannot extract ${a##*/}: $(grep \
	    -E '^ERROR' "$WORK/7z.log" | grep -v 'Dangerous link' |
	    head -2 | tr '\n' ' ')"; return 1; }
	return 0
}

scan_p5p() {	# $1 .p5p
	python3 -I - "$1" "$IDS_RE" "$PUB" <<'EOF' | while read -r l; do hit "$l"; done
import gzip, re, sys, tarfile
p5p = sys.argv[1]
ids = [l.strip().lower().encode() for l in open(sys.argv[2]) if l.strip()]
pub = [l.strip().lower().encode() for l in open(sys.argv[3]) if l.strip()]
# the public strings, longest first, taken out before the identifiers
pub_re = re.compile(b'|'.join(re.escape(w) for w in
                              sorted(pub, key=len, reverse=True))) if pub else None
name = p5p.rsplit('/', 1)[-1]
with tarfile.open(p5p) as t:
    for m in t:
        if not m.isfile():
            continue
        d = t.extractfile(m).read()
        if d[:2] == b'\x1f\x8b':
            try:
                d = gzip.decompress(d)
            except Exception:
                pass
        low = d.lower()
        if pub_re:
            low = pub_re.sub(b'\0', low)
        found = sorted({i.decode() for i in ids if i in low})
        if found:
            print('id: %s:%s: %s' % (name, m.name, ' '.join(found[:3])))
        if m.name.endswith(('pub.p5i', 'pkg5.repository')):
            for w in (b'file://', b'localhost'):
                if w in low:
                    print('pkg: %s:%s: %s' % (name, m.name, w.decode()))
EOF
}

scan_iso() {
	local iso=$1 m ba trees=() i=0
	need 7z; need python3; need gzip
	[ -f "$iso" ] || die "no ISO $iso"
	echo "== ISO"
	x7z "$iso" "$WORK/iso" -x'!*.zlib' -x'!platform/*/boot_archive*' \
	    -x'![BOOT]'
	check_names "$WORK/iso" ""
	check_ids "$WORK/iso" "iso:/"
	for m in $(find "$WORK/iso" -name '*.p5p'); do
		echo "== ${m#$WORK/iso/}"
		scan_p5p "$m"
	done
	# boot archives: the live root (UFS, gzip)
	for ba in $(7z l -ba "$iso" 2>/dev/null | awk '{print $NF}' |
	    grep -E '^platform/[^/]+/(amd64/)?boot_archive[^.]*$'); do
		echo "== $ba"
		i=$((i + 1))
		7z e -so "$iso" "$ba" 2>/dev/null | gzip -dc > "$WORK/ba.img" ||
		    { hit "tool: cannot read $ba"; continue; }
		python3 -I "$HERE/ufs-extract.py" "$WORK/ba.img" \
		    "$WORK/root$i" "$WORK/root$i.list" ||
		    hit "tool: cannot extract $ba"
		rm -f "$WORK/ba.img"
		trees+=("=$WORK/root$i")
	done
	# lofi images: solaris.zlib is /usr, solarismisc.zlib /etc /var /opt
	for m in $(7z l -ba "$iso" 2>/dev/null | awk '{print $NF}' |
	    grep '\.zlib$'); do
		echo "== $m"
		7z e -so "$iso" "$m" 2>/dev/null |
		    python3 -I "$HERE/lofi-cat.py" - > "$WORK/z.img" ||
		    { hit "tool: cannot read $m"; continue; }
		x7z "$WORK/z.img" "$WORK/${m%.zlib}"
		rm -f "$WORK/z.img"
		case $m in solaris.zlib) trees+=("usr/=$WORK/${m%.zlib}");;
		*) trees+=("=$WORK/${m%.zlib}");; esac
	done
	scan_trees "${trees[@]}"
	echo "== raw ISO bytes"
	grep -a -o -F -i -f "$PUB_RE" "$iso" | ids_in | sort | uniq -c |
	    while read -r c s; do hit "raw: $s ($c times)"; done
}

# ---- main -----------------------------------------------------------------

mode=$1 target=$2
case $mode in
tree)	[ -d "$target" ] || die "no directory $target"
	scan_trees "=$target";;
installed)
	[ -d "$target" ] || die "no directory $target"
	check_installed "$target"
	scan_trees "=$target";;
iso)	scan_iso "$target";;
selftest)
	scan_iso "$target"
	rc=0
	for want in '^users: etc/passwd: ' '^sudo: etc/sudoers.d/' \
	    '^file: .*authorized_keys' '^id: ' '^raw: ' '^leftover: '; do
		if grep -q -E "$want" "$HITS"; then
			echo "selftest: found  $want"
		else
			echo "selftest: MISSED $want"; rc=1
		fi
	done
	echo "selftest: $(sort -u "$HITS" | wc -l) distinct hits"
	[ -n "${KEEP_HITS:-}" ] && sort -u "$HITS" > "$KEEP_HITS"
	[ -n "${KEEP_HITS:-}" ] && [ -f "$WORK/undelivered.list" ] &&
	    cp "$WORK/undelivered.list" "$KEEP_HITS.undelivered"
	[ $rc -eq 0 ] && echo "selftest: PASS" || echo "selftest: FAIL"
	exit $rc;;
*)	die "unknown mode $mode";;
esac

[ -n "${KEEP_HITS:-}" ] && sort -u "$HITS" > "$KEEP_HITS"
[ -n "${KEEP_HITS:-}" ] && [ -f "$WORK/undelivered.list" ] &&
    cp "$WORK/undelivered.list" "$KEEP_HITS.undelivered"
if [ -s "$HITS" ]; then
	sort -u "$HITS"
	echo "privacy-scan: FAIL, $(sort -u "$HITS" | wc -l) hits" >&2
	exit 1
fi
echo "privacy-scan: clean"
exit 0
