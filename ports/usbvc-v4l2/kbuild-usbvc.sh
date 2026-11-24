#!/bin/sh
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
#
# Copyright 2026 isoriano1968
#
# Out-of-tree build of usbvc from an illumos-gate worktree with the gate's
# amd64 kernel flags (as ports/bge-57766/Makefile), against the worktree's
# own uts headers (sys/videodev2.h changed).  Usage: kbuild-usbvc.sh GATE OUT
set -e
G=$1; O=$2
mkdir -p $O
CC=/usr/gcc/10/bin/gcc
CFLAGS="-m64 -Ui386 -U__i386 -O2 -std=gnu99 -mcmodel=kernel -mno-red-zone
 -ffreestanding -mno-mmx -mno-sse -msave-args -fno-shrink-wrap
 -mindirect-branch=thunk-extern -mindirect-branch-register
 -fno-asynchronous-unwind-tables -fstack-protector-strong
 -fno-inline-small-functions -fno-inline-functions-called-once -fno-ipa-cp
 -fno-ipa-icf -fno-clone-functions -fno-reorder-functions
 -fno-reorder-blocks-and-partition -fno-aggressive-loop-optimizations
 --param=max-inline-insns-single=450 -fno-builtin -fident -gdwarf-2"
CPPFLAGS="-D_ASM_INLINES -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64
 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -nostdinc
 -I$G/usr/src/uts/intel -I$G/usr/src/uts/common -I/usr/include"
CWARN="-Wall -Werror -Winline -Wno-missing-braces -Wno-sign-compare
 -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused
 -Wno-empty-body -Wno-maybe-uninitialized -Wno-switch -Wno-parentheses"
for f in usbvc usbvc_v4l2; do
	$CC $CFLAGS $CPPFLAGS $CWARN ${EXTRA} -c -o $O/$f.o \
	    $G/usr/src/uts/common/io/usb/clients/video/usbvc/$f.c
done
/usr/bin/ld -r -dy -N misc/usba -o $O/usbvc $O/usbvc.o $O/usbvc_v4l2.o
/opt/onbld/bin/i386/ctfconvert -l usbvc -o $O/usbvc $O/usbvc
/usr/bin/nm -u -h $O/usbvc | awk '{print $NF}' | LC_ALL=C sort -u > $O/undef.txt
for k in /platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix /kernel/misc/amd64/usba; do
	/usr/bin/nm -h -g $k; done | awk -F'|' '$7 !~ /UNDEF/ {gsub(/ /,"",$8); print $8}' |
	LC_ALL=C sort -u > $O/kdefs.txt
m=`LC_ALL=C comm -23 $O/undef.txt $O/kdefs.txt`
if [ -n "$m" ]; then echo "undefined: $m"; exit 1; fi
echo "OK: `wc -l < $O/undef.txt` undefined symbols, all in unix/genunix/usba"
/usr/bin/digest -a sha256 $O/usbvc
