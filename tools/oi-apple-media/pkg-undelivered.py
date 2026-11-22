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
# pkg-undelivered: the files of an image tree that its installed packages
# do not deliver as they are, i.e. what the image build (or a person) added
# or changed.  Reads the installed manifests the image carries
# (var/pkg/publisher/*/pkg/<stem>/<version>), compares each regular file's
# SHA-1 with the file action's hash.
#
# Usage: pkg-undelivered.py MANIFEST_DIR... -- PREFIX=TREE...
#   MANIFEST_DIR  a var/pkg/publisher/<pub>/pkg directory
#   PREFIX=TREE   a tree whose paths are PREFIX + relative path in the
#                 image (e.g. usr/=solaris.zlib.x, =root)
# Output: "<status> <image path> <tree path>" per file, status
#   added     not delivered by any installed package
#   changed   delivered, content differs (editable files the build set)

import hashlib
import os
import shlex
import sys


def manifests(dirs):
    for d in dirs:
        for stem in os.listdir(d):
            sd = os.path.join(d, stem)
            if not os.path.isdir(sd):
                continue
            for ver in os.listdir(sd):
                p = os.path.join(sd, ver)
                if os.path.isfile(p):
                    yield p


def delivered(dirs):
    files = {}
    for m in manifests(dirs):
        with open(m, encoding='utf-8', errors='replace') as f:
            for line in f:
                kind = line.split(' ', 1)[0]
                if kind not in ('file', 'link', 'hardlink'):
                    continue
                try:
                    toks = shlex.split(line)
                except ValueError:
                    toks = line.split()
                h, path = None, None
                for t in toks[1:]:
                    if '=' in t:
                        k, v = t.split('=', 1)
                        if k == 'path':
                            path = v
                        elif k == 'hash' and h is None:
                            h = v
                    elif h is None:
                        h = t
                if path:
                    # links: delivered, content not compared (7z writes
                    # Rock Ridge symlinks as plain files)
                    files[path.lstrip('/')] = h if kind == 'file' else ''
    return files


def sha1(p):
    s = hashlib.sha1()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            s.update(b)
    return s.hexdigest()


def main():
    args = sys.argv[1:]
    if '--' not in args:
        sys.exit(__doc__ if False else 'usage: pkg-undelivered.py '
                 'MANIFEST_DIR... -- PREFIX=TREE...')
    i = args.index('--')
    files = delivered(args[:i])
    print('# %d delivered files known' % len(files), file=sys.stderr)
    for spec in args[i + 1:]:
        prefix, tree = spec.split('=', 1)
        for dp, dn, fn in os.walk(tree):
            for n in fn:
                p = os.path.join(dp, n)
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                img = prefix + os.path.relpath(p, tree)
                h = files.get(img)
                if h is None:
                    print('added', img, p)
                elif h and sha1(p) != h:
                    print('changed', img, p)


if __name__ == '__main__':
    main()
