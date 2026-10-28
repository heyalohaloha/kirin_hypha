"""Unprivileged, descriptor-relative AAX source inspection; no system writes."""
import ctypes
import base64
import errno
import hashlib
import json
import os
import stat
import sys

if sys.platform == 'darwin':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from aax_swap_xattrs import read_extended_attributes
else:
    def read_extended_attributes(fd):
        names = sorted(os.listxattr(fd))
        if any(name.startswith('system.posix_acl_') for name in names):
            raise ValueError('source ACL not permitted')
        values = {name: os.getxattr(fd, name) for name in names}
        if names != sorted(os.listxattr(fd)):
            raise ValueError('extended attribute list changed')
        return values


def has_no_extended_acl(fd):
    """Fail closed: accept only Darwin's no-ACL result for an open descriptor."""
    os.fstat(fd)  # A bad descriptor must not be confused with an absent ACL.
    libc = ctypes.CDLL(None, use_errno=True)
    getter = getattr(libc, 'acl_get_fd_np', None)
    free = getattr(libc, 'acl_free', None)
    if getter is None or free is None:
        return False
    getter.argtypes = (ctypes.c_int, ctypes.c_int)
    getter.restype = ctypes.c_void_p
    free.argtypes = (ctypes.c_void_p,)
    free.restype = ctypes.c_int
    ctypes.set_errno(0)
    acl = getter(fd, 0x00000100)  # ACL_TYPE_EXTENDED, Darwin sys/acl.h.
    if acl:
        free(acl)
        return False
    return ctypes.get_errno() == errno.ENOENT


def _same(a, b):
    names = ('st_dev', 'st_ino', 'st_mode', 'st_size', 'st_nlink', 'st_mtime_ns', 'st_ctime_ns')
    return all(getattr(a, name) == getattr(b, name) for name in names)


def _open_absolute_dir(path, require_root=False):
    if not isinstance(path, str) or not os.path.isabs(path) or os.path.normpath(path) != path or path == '/':
        raise ValueError('source must be an absolute normalized bundle path')
    if not hasattr(os, 'O_NOFOLLOW'):
        raise ValueError('O_NOFOLLOW unavailable')
    current = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        if require_root:
            _require_root_object(current)
        for component in path.split('/')[1:]:
            if not component or component in ('.', '..') or '\0' in component:
                raise ValueError('invalid source path component')
            try:
                child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=current)
            except OSError as error:
                if error.errno in (errno.ELOOP, errno.ENOTDIR):
                    raise ValueError('source path contains a symlink or non-directory') from error
                raise
            os.close(current)
            current = child
            if require_root:
                _require_root_object(current)
        return current
    except BaseException:
        os.close(current)
        raise


def _require_root_object(fd):
    info = os.fstat(fd)
    if info.st_uid != 0 or info.st_mode & 0o022:
        raise ValueError('installed path is not exclusively root-writable')
    if sys.platform == 'darwin' and not has_no_extended_acl(fd):
        raise ValueError('installed path ACL is not supported')


def inspect_source(path, include_bytes=False, require_root=False, allow_existing_links=False):
    root_fd = _open_absolute_dir(path, require_root)
    root_device = os.fstat(root_fd).st_dev
    entries, files, attributes, total, metadata_total = [], [], [], 0, 0

    def capture_attributes(fd, relative, existing_values=None):
        nonlocal metadata_total
        if existing_values is None and sys.platform == 'darwin' and not has_no_extended_acl(fd):
            raise ValueError('source ACL not permitted or cannot be inspected')
        values = read_extended_attributes(fd) if existing_values is None else existing_values
        if existing_values is None and sys.platform == 'darwin' and not has_no_extended_acl(fd):
            raise ValueError('source ACL changed during inspection')
        metadata_total += sum(len(value) for value in values.values())
        if metadata_total > 64 * 1024 * 1024:
            raise ValueError('bundle extended attributes exceed size limit')
        attributes.append({'path': relative, 'values': [
            {'name': name, 'base64': base64.b64encode(values[name]).decode('ascii')}
            for name in sorted(values, key=lambda value: value.encode('utf-16-be', 'strict'))
        ]})

    def walk(directory_fd, relative):
        nonlocal total
        before = os.fstat(directory_fd)
        if require_root:
            _require_root_object(directory_fd)
            if before.st_dev != root_device:
                raise ValueError('installed bundle crosses a volume')
        if not stat.S_ISDIR(before.st_mode):
            raise ValueError('source is not a directory')
        entries.append({'path': relative, 'kind': 'directory', 'mode': stat.S_IMODE(before.st_mode)})
        capture_attributes(directory_fd, relative)
        names = os.listdir(directory_fd)
        for name in sorted(names, key=lambda value: value.encode('utf-16-be', 'strict')):
            name.encode('utf-8', 'strict')
            child_path = relative + '/' + name if relative else name
            named = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            if stat.S_ISLNK(named.st_mode):
                raise ValueError('source contains a symlink')
            if stat.S_ISDIR(named.st_mode):
                child_fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory_fd)
                try:
                    if not _same(named, os.fstat(child_fd)):
                        raise ValueError('source changed during inspection')
                    walk(child_fd, child_path)
                finally:
                    os.close(child_fd)
            elif stat.S_ISREG(named.st_mode):
                if named.st_nlink != 1:
                    raise ValueError('source contains a hardlink')
                fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory_fd)
                try:
                    opened = os.fstat(fd)
                    if require_root:
                        _require_root_object(fd)
                        if opened.st_dev != root_device:
                            raise ValueError('installed file crosses a volume')
                    if not _same(named, opened) or not stat.S_ISREG(opened.st_mode):
                        raise ValueError('source changed during inspection')
                    total += opened.st_size
                    if include_bytes and total > 256 * 1024 * 1024:
                        raise ValueError('bundle exceeds snapshot size limit')
                    digest, chunks, size = hashlib.sha256(), [], 0
                    while True:
                        chunk = os.read(fd, 65536)
                        if not chunk:
                            break
                        size += len(chunk)
                        if size > opened.st_size:
                            raise ValueError('source changed during inspection')
                        digest.update(chunk)
                        if include_bytes:
                            chunks.append(chunk)
                    capture_attributes(fd, child_path)
                    if size != opened.st_size or not _same(opened, os.fstat(fd)) or not _same(opened, os.stat(name, dir_fd=directory_fd, follow_symlinks=False)):
                        raise ValueError('source changed during inspection')
                    entries.append({'path': child_path, 'kind': 'file', 'mode': stat.S_IMODE(opened.st_mode),
                                    'bytes': size, 'sha256': digest.hexdigest()})
                    if include_bytes:
                        files.append({'path': child_path, 'base64': base64.b64encode(b''.join(chunks)).decode('ascii')})
                finally:
                    os.close(fd)
            else:
                raise ValueError('source contains an unsupported file type')
        if not _same(before, os.fstat(directory_fd)):
            raise ValueError('source changed during inspection')

    try:
        walk(root_fd, '')
    finally:
        os.close(root_fd)
    encoded = json.dumps(entries, ensure_ascii=False, separators=(',', ':')).encode('utf-8', 'strict')
    metadata = json.dumps(attributes, ensure_ascii=False, separators=(',', ':')).encode('utf-8', 'strict')
    result = {'sha256': hashlib.sha256(encoded).hexdigest(), 'entries': entries,
              'metadataSha256': hashlib.sha256(metadata).hexdigest(), 'attributes': attributes}
    if include_bytes:
        result['files'] = files
    return result


if __name__ == '__main__':
    import sys
    try:
        if len(sys.argv) != 3 or sys.argv[1] not in ('inspect', 'snapshot', 'installed'):
            raise ValueError('usage: aax_swap_source.py inspect|snapshot ABSOLUTE_BUNDLE')
        print(json.dumps(inspect_source(sys.argv[2], sys.argv[1] == 'snapshot', sys.argv[1] == 'installed'), ensure_ascii=True, separators=(',', ':')))
    except (OSError, ValueError, UnicodeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
