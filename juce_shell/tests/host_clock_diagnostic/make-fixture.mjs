import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
const destination = process.argv[2];
if (!destination || !path.isAbsolute(destination)) throw new Error('Explicit absolute WAV output required');
const rate = 48000, frames = rate * 24, channels = 2, bytes = frames * channels * 4;
const wav = Buffer.alloc(56 + bytes);
wav.write('RIFF', 0); wav.writeUInt32LE(wav.length - 8, 4); wav.write('WAVEfmt ', 8);
wav.writeUInt32LE(16, 16); wav.writeUInt16LE(3, 20); wav.writeUInt16LE(channels, 22);
wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 8, 28);
wav.writeUInt16LE(8, 32); wav.writeUInt16LE(32, 34);
wav.write('fact', 36); wav.writeUInt32LE(4, 40); wav.writeUInt32LE(frames, 44);
wav.write('data', 48); wav.writeUInt32LE(bytes, 52);
let seed = 0x48c10c24, squares = 0, peak = 0;
for (let i = 0; i < frames * channels; ++i) {
  seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5;
  const value = Math.fround(((seed >>> 0) / 0x100000000 - 0.5) * 0.02);
  wav.writeFloatLE(value, 56 + i * 4); squares += value * value; peak = Math.max(peak, Math.abs(value));
}
fs.writeFileSync(destination, wav, {flag:'wx'});
console.log(JSON.stringify({destination,rate,frames,channels,bytes:wav.length,
  samplePeakDbfs:20*Math.log10(peak),rmsDbfs:10*Math.log10(squares/(frames*channels)),
  sha256:crypto.createHash('sha256').update(wav).digest('hex')},null,2));
