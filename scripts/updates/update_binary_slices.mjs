// Structural parsing, without running or modifying a distributed executable.
// Layout: Apple's cctools include/mach-o/{fat,loader}.h and XNU mach/machine.h.
const FAT = new Map([[0xcafebabe, [false, false]], [0xbebafeca, [true, false]],
  [0xcafebabf, [false, true]], [0xbfbafeca, [true, true]]]);
const CPU = new Map([[0x01000007, 'x86_64'], [0x0100000c, 'arm64']]);

export function universalSlices(bytes, required = false) {
  const format = bytes.length >= 4 ? FAT.get(bytes.readUInt32BE(0)) : undefined;
  if (!format) {
    if (required) throw new Error('Update key evidence requires a Universal Mach-O binary');
    return null;
  }
  const [little, wide] = format, stride = wide ? 32 : 20;
  const u32 = offset => little ? bytes.readUInt32LE(offset) : bytes.readUInt32BE(offset);
  const u64 = offset => little ? bytes.readBigUInt64LE(offset) : bytes.readBigUInt64BE(offset);
  if (bytes.length < 8 || u32(4) !== 2 || bytes.length < 8 + 2 * stride) {
    throw new Error('Universal update evidence needs exactly ARM64 and Intel slices');
  }
  const slices = [];
  for (let index = 0; index < 2; index++) {
    const row = 8 + index * stride, type = u32(row), architecture = CPU.get(type);
    const start = wide ? u64(row + 8) : BigInt(u32(row + 8));
    const size = wide ? u64(row + 16) : BigInt(u32(row + 12));
    const align = u32(row + (wide ? 24 : 16));
    if (!architecture || start < BigInt(8 + 2 * stride) || size < 32n
        || start + size > BigInt(bytes.length) || align > 63
        || start % (1n << BigInt(align)) !== 0n || (wide && u32(row + 28) !== 0)) {
      throw new Error('Invalid Universal architecture bounds/alignment');
    }
    const slice = bytes.subarray(Number(start), Number(start + size));
    const magic = slice.readUInt32BE(0);
    const sliceType = magic === 0xfeedfacf ? slice.readUInt32BE(4)
      : magic === 0xcffaedfe ? slice.readUInt32LE(4) : 0;
    if (sliceType !== type) throw new Error('Universal record and Mach-O slice architecture disagree');
    slices.push({ architecture, start: Number(start), end: Number(start + size), bytes: slice });
  }
  slices.sort((a, b) => a.start - b.start);
  if (slices[0].end > slices[1].start || new Set(slices.map(s => s.architecture)).size !== 2) {
    throw new Error('Overlapping or duplicate Universal architecture slices');
  }
  return slices.sort((a, b) => a.architecture.localeCompare(b.architecture));
}
