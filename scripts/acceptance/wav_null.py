#!/usr/bin/env python3
"""Compare decoded WAV samples without alignment, gain, dither or tolerance."""
import argparse
import hashlib
import json
import math
import os
import struct
import sys


def inspect(handle):
    size = os.fstat(handle.fileno()).st_size
    header = handle.read(12)
    if len(header) != 12 or header[:4] != b'RIFF' or header[8:] != b'WAVE':
        raise ValueError('Expected a little-endian RIFF WAVE file (RF64 is unsupported)')
    if struct.unpack('<I', header[4:8])[0] + 8 != size:
        raise ValueError('RIFF length differs from file length')
    fmt = data = None
    while handle.tell() < size:
        chunk = handle.read(8)
        if len(chunk) != 8:
            raise ValueError('Truncated chunk header')
        kind, length = struct.unpack('<4sI', chunk)
        start = handle.tell()
        if start + length + (length & 1) > size:
            raise ValueError('Truncated chunk')
        if kind == b'fmt ':
            if fmt is not None or length < 16 or length > 4096:
                raise ValueError('Invalid or duplicate format chunk')
            body = handle.read(length)
            tag, channels, rate, byte_rate, align, bits = struct.unpack('<HHIIHH', body[:16])
            if tag == 0xfffe:
                if length < 40 or struct.unpack('<H', body[16:18])[0] < 22:
                    raise ValueError('Truncated extensible format')
                valid = struct.unpack('<H', body[18:20])[0]
                guid = body[24:40]
                if valid != bits or guid[4:] != bytes.fromhex('00001000800000aa00389b71'):
                    raise ValueError('Unsupported extensible sample format')
                tag = struct.unpack('<I', guid[:4])[0]
            if (tag, bits) not in {(1, 16), (1, 24), (1, 32), (3, 32), (3, 64)}:
                raise ValueError('Only PCM16/24/32 and IEEE float32/64 are supported')
            if channels < 1 or rate < 1 or align != channels * (bits // 8) or byte_rate != rate * align:
                raise ValueError('Invalid WAV format geometry')
            fmt = dict(tag=tag, channels=channels, sampleRate=rate, bits=bits, align=align)
        elif kind == b'data':
            if data is not None:
                raise ValueError('Multiple data chunks are unsupported')
            data = (start, length)
        handle.seek(start + length + (length & 1))
    if fmt is None or data is None or data[1] == 0 or data[1] % fmt['align']:
        raise ValueError('Missing, empty or incomplete audio frames')
    return fmt, data


def samples(handle, fmt, data):
    handle.seek(data[0])
    width = fmt['bits'] // 8
    remaining = data[1]
    while remaining:
        block = handle.read(min(remaining, width * 8192))
        if not block or len(block) % width:
            raise ValueError('Audio changed or truncated while reading')
        remaining -= len(block)
        for offset in range(0, len(block), width):
            value = block[offset:offset + width]
            if fmt['tag'] == 3:
                yield struct.unpack('<f' if width == 4 else '<d', value)[0]
            else:
                yield int.from_bytes(value, 'little', signed=True) / (1 << (fmt['bits'] - 1))


def fact(handle):
    handle.seek(0)
    digest = hashlib.sha256()
    for block in iter(lambda: handle.read(1024 * 1024), b''):
        digest.update(block)
    return dict(bytes=os.fstat(handle.fileno()).st_size, sha256=digest.hexdigest())


def compare(reference, processed):
    with open(reference, 'rb') as a, open(processed, 'rb') as b:
        before = (fact(a), fact(b))
        a.seek(0)
        b.seek(0)
        af, ad = inspect(a)
        bf, bd = inspect(b)
        frames = ad[1] // af['align']
        if af['channels'] != bf['channels'] or af['sampleRate'] != bf['sampleRate'] or frames != bd[1] // bf['align']:
            raise ValueError('Sample rate, channels and frame count must agree; no trimming or alignment is allowed')
        mismatched = non_finite = 0
        peak = 0.0
        for av, bv in zip(samples(a, af, ad), samples(b, bf, bd)):
            if not math.isfinite(av) or not math.isfinite(bv):
                non_finite += 1
                mismatched += 1
            elif av != bv:
                mismatched += 1
                peak = max(peak, abs(av - bv))
        if before != (fact(a), fact(b)):
            raise ValueError('Input WAV changed during comparison')
        return dict(schema='hypha-wav-null-v1', passed=mismatched == 0 and non_finite == 0,
                    reference=before[0], processed=before[1], frames=frames,
                    channels=af['channels'], sampleRate=af['sampleRate'], mismatchedSamples=mismatched,
                    nonFiniteSamples=non_finite, latencySamples=0 if mismatched == 0 else None,
                    peakAbsoluteDifference=peak)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('processed')
    args = parser.parse_args()
    try:
        result = compare(args.reference, args.processed)
    except (OSError, ValueError, struct.error):
        print(json.dumps(dict(passed=False, error='Invalid, incompatible or changing WAV inputs')), file=sys.stderr)
        return 2
    print(json.dumps(result, allow_nan=False))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
