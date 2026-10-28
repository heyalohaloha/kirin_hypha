"""Descriptor-based extended attribute inspection; no privileged entry point."""
import ctypes
import errno
import os


def list_extended_attributes(fd):
    """Read accessible Darwin xattr names through an open descriptor."""
    os.fstat(fd)
    libc = ctypes.CDLL(None, use_errno=True)
    listing = getattr(libc, 'flistxattr', None)
    if listing is None:
        raise OSError(errno.ENOTSUP, 'extended attributes unavailable')
    listing.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int)
    listing.restype = ctypes.c_ssize_t
    size = listing(fd, None, 0, 0x0020)  # XATTR_SHOWCOMPRESSION
    if size < 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number))
    if size > 1024 * 1024:
        raise ValueError('extended attribute list too large')
    if size == 0:
        return ()
    buffer = ctypes.create_string_buffer(size)
    actual = listing(fd, buffer, size, 0x0020)
    if actual != size:
        raise ValueError('extended attribute list changed or cannot be read')
    raw = buffer.raw[:size]
    if not raw.endswith(b'\0'):
        raise ValueError('invalid extended attribute list')
    return tuple(sorted(item.decode('utf-8', 'strict') for item in raw[:-1].split(b'\0')))


def has_no_extended_attributes(fd):
    try:
        return not list_extended_attributes(fd)
    except (OSError, ValueError, UnicodeError):
        return False


def read_extended_attributes(fd):
    """Capture accessible attribute values; never infer missing values from an error."""
    before = os.fstat(fd)
    names = list_extended_attributes(fd)
    libc = ctypes.CDLL(None, use_errno=True)
    getter = getattr(libc, 'fgetxattr', None)
    if getter is None:
        raise OSError(errno.ENOTSUP, 'extended attribute values unavailable')
    getter.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t,
                       ctypes.c_uint32, ctypes.c_int)
    getter.restype = ctypes.c_ssize_t
    values = {}
    total = 0
    for name in names:
        raw_name = name.encode('utf-8')
        size = getter(fd, raw_name, None, 0, 0, 0x0020)
        if size < 0:
            number = ctypes.get_errno()
            raise OSError(number, os.strerror(number))
        total += size
        if total > 16 * 1024 * 1024:
            raise ValueError('extended attribute values too large')
        if size:
            buffer = ctypes.create_string_buffer(size)
            actual = getter(fd, raw_name, buffer, size, 0, 0x0020)
            if actual != size:
                raise ValueError('extended attribute changed or cannot be read')
            values[name] = buffer.raw[:size]
        else:
            values[name] = b''
    after = os.fstat(fd)
    if (before.st_dev, before.st_ino, before.st_ctime_ns) != (after.st_dev, after.st_ino, after.st_ctime_ns):
        raise ValueError('extended attributes changed during read')
    if names != list_extended_attributes(fd):
        raise ValueError('extended attribute list changed during read')
    return values
