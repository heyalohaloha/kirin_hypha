"""Read-only bundle comparison; ignore only macOS's provenance tracking value.

This is not an installer, signature check or permission to bypass Gatekeeper.
No attribute, file, permission or security setting is written by this tool.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess

PROVENANCE = b'com.apple.provenance:'
HEX_LINE = re.compile(rb'^[0-9A-Fa-f]{8}(  |$)')


def xattrs_without_macos_provenance(output):
    """Filter one-path `xattr -s -l -x` output, preserving all other bytes."""
    kept, current = [], None
    for line in output.splitlines(keepends=True):
        body = line.rstrip(b'\n')
        if HEX_LINE.match(body):
            if current is None:
                raise ValueError('Unexpected xattr output format')
        elif body.endswith(b':') and body != b':':
            current = body
        else:
            raise ValueError('Unexpected xattr output format')
        if current != PROVENANCE:
            kept.append(line)
    return b''.join(kept)


def read_xattrs(path):
    return subprocess.check_output(['/usr/bin/xattr', '-s', '-l', '-x', str(path)])


def inventory(root, xattrs=read_xattrs):
    """Compare bytes, link text, mode, flags and attributes, never link targets.

    Owners, inode numbers and times differ legitimately between payload and install.
    Inspect only quiescent bundles. Concurrent changes are rejected when observed;
    this is not a privileged or adversarial-filesystem installation verifier.
    """
    root = Path(os.path.abspath(root))
    for parent in (root, *root.parents):
        if parent.is_symlink():
            raise ValueError('Root or ancestor is a symlink')
    if not root.is_dir():
        raise ValueError('Bundle root is not a directory')
    rows = []

    def visit(path):
        before = path.lstat()
        raw = xattrs(path)
        row = dict(path=str(path.relative_to(root)), mode=stat.S_IMODE(before.st_mode),
                   flags=getattr(before, 'st_flags', 0),
                   xattrsSha256=hashlib.sha256(xattrs_without_macos_provenance(raw)).hexdigest())
        if stat.S_ISLNK(before.st_mode):
            row.update(kind='symlink', target=os.readlink(path))
        elif stat.S_ISREG(before.st_mode):
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(fd, 'rb') as stream:
                opened = os.fstat(stream.fileno())
                if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                    raise ValueError('File changed during inspection')
                digest = hashlib.sha256()
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            row.update(kind='file', bytes=before.st_size, sha256=digest.hexdigest())
        elif stat.S_ISDIR(before.st_mode):
            row.update(kind='directory')
            for child in sorted(path.iterdir()):
                visit(child)
        else:
            raise ValueError('Unsupported filesystem entry')
        after = path.lstat()
        fields = ('st_dev', 'st_ino', 'st_mode', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
        if any(getattr(before, k) != getattr(after, k) for k in fields) or raw != xattrs(path):
            raise ValueError('Bundle changed during inspection')
        rows.append(row)

    visit(root)
    return sorted(rows, key=lambda row: row['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('expected', type=Path)
    parser.add_argument('installed', type=Path)
    args = parser.parse_args()
    try:
        expected, installed = inventory(args.expected), inventory(args.installed)
        same = expected == installed
        print(json.dumps(dict(equal=same, expectedEntries=len(expected),
                              installedEntries=len(installed),
                              excludedAttribute='com.apple.provenance')))
        return 0 if same else 1
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(json.dumps(dict(equal=False, error=str(error))))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
