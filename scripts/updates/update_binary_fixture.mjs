// Disposable synthetic Mach-O bytes for offline binding tests, never executable.
export function universalFixture(markers = ['disabled', 'disabled'],
  { wide = false, little = false, sliceLittle = true, label = 'TEST fixture' } = {}) {
  const bytes = Buffer.alloc(1024), stride = wide ? 32 : 20;
  bytes.writeUInt32BE(wide ? (little ? 0xbfbafeca : 0xcafebabf) : (little ? 0xbebafeca : 0xcafebabe));
  const u32 = (value, offset) => little ? bytes.writeUInt32LE(value, offset) : bytes.writeUInt32BE(value, offset);
  const u64 = (value, offset) => little ? bytes.writeBigUInt64LE(BigInt(value), offset) : bytes.writeBigUInt64BE(BigInt(value), offset);
  u32(2, 4);
  for (let index = 0; index < 2; index++) {
    const row = 8 + index * stride, offset = index === 0 ? 256 : 768;
    const type = index === 0 ? 0x0100000c : 0x01000007;
    u32(type, row); u32(0, row + 4);
    if (wide) { u64(offset, row + 8); u64(192, row + 16); u32(8, row + 24); }
    else { u32(offset, row + 8); u32(192, row + 12); u32(8, row + 16); }
    if (sliceLittle) {
      bytes.writeUInt32LE(0xfeedfacf, offset); bytes.writeUInt32LE(type, offset + 4);
    } else {
      bytes.writeUInt32BE(0xfeedfacf, offset); bytes.writeUInt32BE(type, offset + 4);
    }
    bytes.write(label, offset + 32, 32);
    if (markers[index] !== null) bytes.write(`KirinHyphaUpdateKeySha256=${markers[index]};`, offset + 64);
  }
  return bytes;
}
