"""Compare a retained artifact ZIP to its local delivery, without extracting or executing it."""
import ctypes
import errno
import hashlib
import os
import shutil
import tempfile
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


def safe_entry(entry, names):
    name = entry.filename.rstrip('/')
    parts = name.split('/')
    mode = entry.external_attr >> 16
    if (not name or any(part in ('', '.', '..') for part in parts)
            or any(ord(c) < 32 or c in '\\:' for c in name)
            or name.casefold() in names or stat.S_ISLNK(mode)
            or stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)
            or (stat.S_ISDIR(mode) and not entry.is_dir())
            or (stat.S_ISREG(mode) and entry.is_dir())):
        raise ValueError('Unsafe artifact ZIP entry')
    names.add(name.casefold())
    return name, parts


def require_independent_file(file):
    info = file.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise ValueError('Artifact delivery contains a nonregular file or hardlink')


def verify(archive, directory):
    root = Path(directory)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Artifact root must be a real directory')
    files, names, directories, total = {}, set(), set(), 0
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name, parts = safe_entry(entry, names)
            directories.update('/'.join(parts[:i]) for i in range(1, len(parts)))
            if entry.is_dir():
                directories.add(name)
                continue
            total += entry.file_size
            if entry.flag_bits & 1 or entry.file_size > 2 * 1024**3 or total > 4 * 1024**3:
                raise ValueError('Unsupported encrypted/oversized artifact')
            target = root
            for part in parts:
                target = target / part
                if target.is_symlink():
                    raise ValueError('Artifact delivery traverses symlink')
            require_independent_file(target)
            with source.open(entry) as zipped, target.open('rb') as local:
                expected = digest(zipped)
                if digest(local) != expected:
                    raise ValueError('Artifact ZIP and local delivery bytes differ')
            files[name] = expected
    actual, actual_directories = set(), set()
    for file in root.rglob('*'):
        if file.is_symlink() or not (file.is_file() or file.is_dir()):
            raise ValueError('Artifact delivery contains a nonregular entry')
        if file.is_file():
            require_independent_file(file)
            actual.add(file.relative_to(root).as_posix())
        elif file.is_dir():
            actual_directories.add(file.relative_to(root).as_posix())
    if not files or actual != set(files) or actual_directories != directories:
        raise ValueError('Artifact ZIP and local delivery file sets differ')
    return files


def rename_exclusive(source, destination):
    libc = ctypes.CDLL(None, use_errno=True)
    src, dst = os.fsencode(source), os.fsencode(destination)
    if sys.platform == 'darwin':
        fn = libc.renamex_np
        result = fn(src, dst, 0x00000004)  # RENAME_EXCL
    elif sys.platform.startswith('linux'):
        fn = libc.renameat2
        result = fn(-100, src, -100, dst, 1)  # RENAME_NOREPLACE
    else:
        raise ValueError('Atomic exclusive directory rename is unavailable')
    if result:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(destination))


def extract(archive, destination):
    destination = Path(destination)
    if destination.is_symlink():
        raise ValueError('Existing artifact destination is a symlink')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.windows-delivery-', dir=destination.parent))
    try:
        names, total = set(), 0
        with zipfile.ZipFile(archive) as source:
            for entry in source.infolist():
                name, parts = safe_entry(entry, names)
                total += entry.file_size
                if entry.flag_bits & 1 or entry.file_size > 2 * 1024**3 or total > 4 * 1024**3:
                    raise ValueError('Unsupported encrypted/oversized artifact')
                target = temporary.joinpath(*parts)
                if entry.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with source.open(entry) as src, target.open('xb') as dst:
                        shutil.copyfileobj(src, dst, 1024 * 1024)
        verify(archive, temporary)
        def compare_existing():
            verify(archive, destination)
            for entry in temporary.rglob('*'):
                relative = entry.relative_to(temporary)
                if stat.S_IMODE(entry.lstat().st_mode) != stat.S_IMODE((destination / relative).lstat().st_mode):
                    raise ValueError('Existing artifact tree has different permissions')
        if destination.exists():
            compare_existing()
        else:
            try:
                rename_exclusive(temporary, destination)
            except OSError as error:
                if error.errno != errno.EEXIST:
                    raise
                compare_existing()
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


if __name__ == '__main__':
    try:
        if len(sys.argv) == 4 and sys.argv[1] == '--extract':
            extract(sys.argv[2], sys.argv[3])
        elif len(sys.argv) == 3:
            print(json.dumps(verify(sys.argv[1], sys.argv[2]), sort_keys=True))
        else:
            raise ValueError('Usage: artifact_archive.py [--extract] RETAINED_ZIP DELIVERY_DIRECTORY')
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print(f'[artifact-archive] {error}', file=sys.stderr)
        sys.exit(1)
