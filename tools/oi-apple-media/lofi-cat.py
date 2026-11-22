#!/usr/bin/env python3
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
# lofi-cat: write the uncompressed contents of an illumos lofi compressed
# file (lofiadm -C; the live media's solaris.zlib, solarismisc.zlib) to
# stdout, without lofi or root, e.g. on Linux for privacy-scan.sh.
#
# Layout (usr/src/cmd/lofiadm/main.c, usr/src/uts/common/io/lofi.c):
#   char     algorithm[36]      "gzip", "gzip-N", "lzma"
#   uint32   segment size       (big-endian, as all fields)
#   uint32   index entries      (segments + 1)
#   uint32   last segment size  (uncompressed)
#   uint64   index[entries]     segment offsets from the start of the data
#   data                        (after the index)
# Each segment starts with a flag byte: 0 stored as it is, 1 compressed.
# gzip segments are zlib streams; lzma segments carry the 13-byte LZMA
# header (properties + size), as lzma "alone" streams.
#
# Usage: lofi-cat.py file.zlib > file.img      ('-' reads stdin)

import lzma
import struct
import sys
import zlib


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '-'
    f = sys.stdin.buffer if path == '-' else open(path, 'rb')
    hdr = f.read(48)
    alg = hdr[:36].split(b'\0', 1)[0].decode()
    segsize, nidx, lastsize = struct.unpack('>III', hdr[36:48])
    index = struct.unpack('>%dQ' % nidx, f.read(8 * nidx))
    out = sys.stdout.buffer
    nseg = nidx - 1
    pos = 0
    for i in range(nseg):
        clen = index[i + 1] - index[i]
        if index[i] != pos:
            sys.exit('lofi-cat: index not contiguous at segment %d' % i)
        data = f.read(clen)
        pos += clen
        usize = lastsize if i == nseg - 1 else segsize
        flag, data = data[0], data[1:]
        if flag == 0:
            seg = data
        elif alg.startswith('gzip'):
            seg = zlib.decompress(data)
        elif alg == 'lzma':
            seg = lzma.decompress(data, format=lzma.FORMAT_ALONE)
        else:
            sys.exit('lofi-cat: unknown algorithm %r' % alg)
        if len(seg) != usize:
            sys.exit('lofi-cat: segment %d is %d bytes, not %d'
                     % (i, len(seg), usize))
        out.write(seg)


if __name__ == '__main__':
    try:
        main()
    except BrokenPipeError:
        pass
