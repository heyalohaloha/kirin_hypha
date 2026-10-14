"""Compare a retained artifact ZIP to its local delivery, without extracting or executing it."""
import hashlib
import json
from pathlib import Path
import stat
import sys
import zipfile


def digest(stream):
    value = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        value.update(chunk)
    return value.hexdigest()


def verify(archive, directory):
    root = Path(directory)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Artifact root must be a real directory')
    files, names, total = {}, set(), 0
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name = entry.filename.rstrip('/')
            parts = name.split('/')
            mode = entry.external_attr >> 16
            if (not name or any(part in ('', '.', '..') for part in parts)
                    or any(ord(c) < 32 or c in '\\:' for c in name)
                    or name.casefold() in names or stat.S_ISLNK(mode)
                    or stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise ValueError('Unsafe artifact ZIP entry')
            names.add(name.casefold())
            if entry.is_dir():
                continue
            total += entry.file_size
            if entry.flag_bits & 1 or entry.file_size > 2 * 1024**3 or total > 4 * 1024**3:
                raise ValueError('Unsupported encrypted/oversized artifact')
            target = root
            for part in parts:
                target = target / part
                if target.is_symlink():
                    raise ValueError('Artifact delivery traverses symlink')
            with source.open(entry) as zipped, target.open('rb') as local:
                expected = digest(zipped)
                if digest(local) != expected:
                    raise ValueError('Artifact ZIP and local delivery bytes differ')
            files[name] = expected
    actual = set()
    for file in root.rglob('*'):
        if file.is_symlink() or not (file.is_file() or file.is_dir()):
            raise ValueError('Artifact delivery contains a nonregular entry')
        if file.is_file():
            actual.add(file.relative_to(root).as_posix())
    if not files or actual != set(files):
        raise ValueError('Artifact ZIP and local delivery file sets differ')
    return files


if __name__ == '__main__':
    try:
        if len(sys.argv) != 3:
            raise ValueError('Usage: artifact_archive.py RETAINED_ZIP DELIVERY_DIRECTORY')
        print(json.dumps(verify(sys.argv[1], sys.argv[2]), sort_keys=True))
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print(f'[artifact-archive] {error}', file=sys.stderr)
        sys.exit(1)
