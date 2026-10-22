// Package trailers follow Apple's Security/notarization.cpp package_trailer_t format.
import fs from 'node:fs';
import crypto from 'node:crypto';
import { fileFact } from './hypha_release_contract.mjs';

function trailer(bytes, offset, type, length) {
  return bytes.toString('ascii', offset, offset + 4) === 't8lr'
    && bytes.readUInt16LE(offset + 4) === 1 && bytes.readUInt16LE(offset + 6) === type
    && bytes.readUInt32LE(offset + 8) === length && bytes.readUInt32LE(offset + 12) === 0;
}
export function inspectStapleBytes(original, current) {
  const base = fs.statSync(original).size, size = fs.statSync(current).size;
  if (base < 4 || size < base || size - base > 16 * 1024 * 1024) throw new Error('PKG staple size is invalid');
  const a = fs.openSync(original, 'r'), b = fs.openSync(current, 'r');
  try {
    const header = Buffer.alloc(4); fs.readSync(a, header, 0, 4, 0);
    if (header.toString('ascii') !== 'xar!') throw new Error('Expected a signed flat XAR package');
    const left = Buffer.alloc(1024 * 1024), right = Buffer.alloc(left.length);
    for (let offset = 0; offset < base; offset += left.length) {
      const length = Math.min(left.length, base - offset);
      if (fs.readSync(a, left, 0, length, offset) !== length || fs.readSync(b, right, 0, length, offset) !== length
          || !left.subarray(0, length).equals(right.subarray(0, length))) throw new Error('Original PKG is not the exact final prefix');
    }
    if (base >= 16) {
      const tail = Buffer.alloc(16); fs.readSync(a, tail, 0, 16, base - 16);
      if (tail.toString('ascii', 0, 4) === 't8lr') throw new Error('Original PKG already has a staple trailer');
    }
    if (size === base) return { stapled: false, current: fileFact(current) };
    const extra = Buffer.alloc(size - base);
    if (fs.readSync(b, extra, 0, extra.length, base) !== extra.length || extra.length < 36) throw new Error('Truncated ticket');
    const length = extra.length - 32;
    if (!trailer(extra, 0, 1, 0) || !trailer(extra, extra.length - 16, 2, length)
        || extra.toString('ascii', 16, 20) !== 's8ch') throw new Error('Staple must append exactly one ticket and its trailers');
    return { stapled: true, current: fileFact(current), ticketBytes: length,
      ticketSha256: crypto.createHash('sha256').update(extra.subarray(16, -16)).digest('hex') };
  } finally { fs.closeSync(a); fs.closeSync(b); }
}
