import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

// Reads a chain trace (KIRIN_HYPHA_CHAIN_TRACE_DIR) without loading audio.f32 into memory.
// Each block stores its PCM planar: side (0 = input, 1 = output), then channel, then frame, as
// little-endian float32 (juce_shell/src/chain_trace/ChainTraceWriter.h). A host acceptance trace can be
// many gigabytes, so the metadata is validated whole and the PCM is read one window at a time.
// scripts/chain_trace_io.mjs gives the same values for traces small enough to load.

const CHUNK = 16 * 1024 * 1024;

function lines(buffer) {
  const out = [];
  let start = 0;
  for (let index = buffer.indexOf(10); index !== -1; index = buffer.indexOf(10, start)) {
    if (index > start) out.push(buffer.subarray(start, index).toString('utf8'));
    start = index + 1;
  }
  if (start < buffer.length) out.push(buffer.subarray(start).toString('utf8'));
  return out;
}

function sha256File(fd, size) {
  const digest = crypto.createHash('sha256'), buffer = Buffer.alloc(Math.min(CHUNK, Math.max(size, 1)));
  for (let position = 0; position < size;) {
    const count = fs.readSync(fd, buffer, 0, Math.min(buffer.length, size - position), position);
    if (count <= 0) throw new Error('Trace PCM ended while hashing');
    digest.update(buffer.subarray(0, count)); position += count;
  }
  return digest.digest('hex');
}

const blockBytes = row => row.frames * row.channels * 8;

export function openTraceWindowed(folder, { hashPcm = true } = {}) {
  const meta = fs.readFileSync(path.join(folder, 'blocks.jsonl'));
  const fd = fs.openSync(path.join(folder, 'audio.f32'), 'r');
  try {
    const size = fs.fstatSync(fd).size;
    const records = lines(meta).map(line => JSON.parse(line));
    const header = records.shift(), end = records.at(-1);
    if (header?.type !== 'header' || header.schema !== 1 || header.pcm !== 'f32le')
      throw new Error('Unsupported trace schema or PCM byte order');
    const rows = records.filter(row => row.type === 'block');
    let expectedOffset = 0, nextSerial = 1, missing = 0;
    for (const r of rows) {
      for (const value of [r.serial, r.localFrames, r.pcmOffset, r.frames, r.channels])
        if (!Number.isSafeInteger(value)) throw new Error('Unsafe integer in trace');
      if (r.serial < nextSerial || r.frames < 1 || r.frames > header.maximumFrames
          || ![1, 2].includes(r.channels) || r.pcmOffset !== expectedOffset)
        throw new Error('Invalid trace sequence, dimensions or offset');
      missing += r.serial - nextSerial; nextSerial = r.serial + 1;
      expectedOffset += blockBytes(r);
      if (expectedOffset > size) throw new Error('Truncated PCM');
    }
    const complete = end?.type === 'end' && end.written === rows.length
      && end.callbacks === rows.length + end.dropped && end.dropped === 0 && missing === 0 && expectedOffset === size;
    const result = {
      header, rows,
      integrity: { complete, missing, dropped: end?.dropped ?? null, cleanClose: end?.type === 'end', bytes: size },
      hashes: { metadata: crypto.createHash('sha256').update(meta).digest('hex'), pcm: hashPcm ? sha256File(fd, size) : null },
      // Rows first..last (inclusive) with sample(side, channel, frame) and bits(...) over one read.
      window(first, last = first) {
        if (!Number.isSafeInteger(first) || !Number.isSafeInteger(last) || first < 0 || last < first || last >= rows.length)
          throw new Error('Trace window is outside the recorded blocks');
        const begin = rows[first].pcmOffset, length = rows[last].pcmOffset + blockBytes(rows[last]) - begin;
        const buffer = Buffer.alloc(length);
        for (let done = 0; done < length;) {
          const count = fs.readSync(fd, buffer, done, length - done, begin + done);
          if (count <= 0) throw new Error('Trace PCM ended inside the window');
          done += count;
        }
        return rows.slice(first, last + 1).map(row => {
          const at = (side, channel, frame) => {
            if (![0, 1].includes(side) || channel < 0 || channel >= row.channels || frame < 0 || frame >= row.frames)
              throw new Error('Trace sample index is outside the block');
            return row.pcmOffset - begin + ((side * row.channels + channel) * row.frames + frame) * 4;
          };
          return { ...row, sample: (s, c, f) => buffer.readFloatLE(at(s, c, f)), bits: (s, c, f) => buffer.readUInt32LE(at(s, c, f)) };
        });
      },
      // Index of the first block whose local frame range reaches the frame (blocks are in order).
      blockAtFrame(localFrame) {
        let low = 0, high = rows.length;
        while (low < high) {
          const mid = (low + high) >>> 1;
          if (rows[mid].localFrames + rows[mid].frames <= localFrame) low = mid + 1; else high = mid;
        }
        return low < rows.length ? low : -1;
      },
      close() { fs.closeSync(fd); },
    };
    return result;
  } catch (error) {
    fs.closeSync(fd);
    throw error;
  }
}
