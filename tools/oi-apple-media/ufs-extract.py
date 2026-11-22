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
# ufs-extract: read-only extraction of an illumos UFS image (the live
# media's boot_archive after gunzip) into a directory, without mount or
# root, e.g. on Linux for privacy-scan.sh.  Regular files, directories and
# symbolic links are extracted (modes kept, owners printed in the listing);
# devices, fifos and sockets are only listed.
#
# Usage: ufs-extract.py image destdir [listing]
#   listing: one line per object: type mode uid gid size path
#
# On-disk format: usr/src/uts/common/sys/fs/ufs_fs.h, ufs_inode.h,
# fs/ufs_fsdir.h (little-endian on x86, UFS1 layout).

import os
import struct
import sys

SBOFF = 8192
FS_MAGIC = 0x011954
MTB_UFS_MAGIC = 0xdecade
NDADDR, NIADDR = 12, 3


class Ufs:
    def __init__(self, path):
        self.f = open(path, 'rb')
        sb = self.read(SBOFF, 2048)
        magic = struct.unpack_from('<I', sb, 1372)[0]
        if magic not in (FS_MAGIC, MTB_UFS_MAGIC):
            sys.exit('ufs-extract: no UFS superblock (magic %#x)' % magic)
        (self.sblkno, self.cblkno, self.iblkno, self.dblkno,
         self.cgoffset, self.cgmask) = struct.unpack_from('<6i', sb, 8)
        self.ncg, self.bsize, self.fsize, self.frag = \
            struct.unpack_from('<4i', sb, 44)
        self.nindir, self.inopb = struct.unpack_from('<2i', sb, 116)
        self.ipg, self.fpg = struct.unpack_from('<2i', sb, 184)

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def inode(self, ino):
        cg = ino // self.ipg
        cgstart = self.fpg * cg + self.cgoffset * (cg & ~self.cgmask)
        frag = cgstart + self.iblkno + \
            ((ino % self.ipg) // self.inopb) * self.frag
        off = frag * self.fsize + (ino % self.inopb) * 128
        d = self.read(off, 128)
        mode, nlink, suid, sgid, size = struct.unpack_from('<4HQ', d, 0)
        db = struct.unpack_from('<12i', d, 40)
        ib = struct.unpack_from('<3i', d, 88)
        blocks = struct.unpack_from('<i', d, 104)[0]
        uid, gid = struct.unpack_from('<2I', d, 116)
        return dict(mode=mode, nlink=nlink, size=size, db=db, ib=ib,
                    blocks=blocks, uid=uid, gid=gid, raw=d)

    def indirect(self, blk, level, out, need):
        if blk == 0:
            out.extend([0] * min(need - len(out), self.nindir **
                                 (level + 1)))
            return
        ptrs = struct.unpack('<%di' % self.nindir,
                             self.read(blk * self.fsize, self.bsize))
        for p in ptrs:
            if len(out) >= need:
                return
            if level == 0:
                out.append(p)
            else:
                self.indirect(p, level - 1, out, need)

    def blocks_of(self, ino):
        need = (ino['size'] + self.bsize - 1) // self.bsize
        out = list(ino['db'][:min(need, NDADDR)])
        for level in range(NIADDR):
            if len(out) >= need:
                break
            self.indirect(ino['ib'][level], level, out, need)
        return out[:need]

    def data(self, ino):
        size = ino['size']
        chunks = []
        for i, b in enumerate(self.blocks_of(ino)):
            n = min(self.bsize, size - i * self.bsize)
            chunks.append(b'\0' * n if b == 0 else
                          self.read(b * self.fsize, n))
        return b''.join(chunks)

    def symlink(self, ino):
        if ino['blocks'] == 0:   # fast symlink: target in the block list
            return ino['raw'][40:40 + ino['size']]
        return self.data(ino)

    def readdir(self, ino):
        d = self.data(ino)
        off = 0
        while off + 8 <= len(d):
            dino, reclen, namlen = struct.unpack_from('<IHH', d, off)
            if reclen == 0:
                break
            if dino:
                name = d[off + 8:off + 8 + namlen].decode('utf-8',
                                                          'replace')
                if name not in ('.', '..'):
                    yield name, dino
            off += reclen


TYPES = {0x4000: 'd', 0x8000: 'f', 0xA000: 'l', 0x2000: 'c', 0x6000: 'b',
         0x1000: 'p', 0xC000: 's'}


def walk(fs, ino_no, rel, dest, listing, seen):
    ino = fs.inode(ino_no)
    t = TYPES.get(ino['mode'] & 0xF000, '?')
    perm = ino['mode'] & 0o7777
    listing.write('%s %04o %d %d %d /%s\n' % (t, perm, ino['uid'],
                                              ino['gid'], ino['size'], rel))
    path = os.path.join(dest, rel)
    if t == 'd':
        os.makedirs(path, exist_ok=True)
        if ino_no in seen:
            return
        seen.add(ino_no)
        for name, child in fs.readdir(ino):
            walk(fs, child, os.path.join(rel, name) if rel else name,
                 dest, listing, seen)
        os.chmod(path, perm | 0o700)
    elif t == 'f':
        with open(path, 'wb') as out:
            out.write(fs.data(ino))
        os.chmod(path, (perm & 0o777) | 0o600)
    elif t == 'l':
        if os.path.lexists(path):
            os.unlink(path)
        os.symlink(fs.symlink(ino).decode('utf-8', 'replace'), path)


def main():
    if len(sys.argv) < 3:
        sys.exit('usage: ufs-extract.py image destdir [listing]')
    fs = Ufs(sys.argv[1])
    os.makedirs(sys.argv[2], exist_ok=True)
    listing = open(sys.argv[3], 'w') if len(sys.argv) > 3 else \
        open(os.devnull, 'w')
    walk(fs, 2, '', sys.argv[2], listing, set())


if __name__ == '__main__':
    main()
