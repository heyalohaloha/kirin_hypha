// Offline inspection of the native float WAVs produced by probe_audio_corpus.mjs.
export function readFloatWav(buffer) {
  if (buffer.length < 44 || buffer.toString('ascii', 0, 4) !== 'RIFF'
    || buffer.toString('ascii', 8, 12) !== 'WAVE') throw new Error('Not a RIFF WAV');
  const limit = buffer.readUInt32LE(4) + 8;
  if (limit !== buffer.length) throw new Error('Truncated or trailing WAV data');
  let format, data;
  for (let at = 12; at + 8 <= limit;) {
    const id = buffer.toString('ascii', at, at + 4), size = buffer.readUInt32LE(at + 4);
    const start = at + 8, end = start + size;
    if (end > limit) throw new Error('Invalid WAV chunk');
    if (id === 'fmt ') {
      if (format || size < 16) throw new Error('Invalid WAV format');
      let tag = buffer.readUInt16LE(start);
      if (tag === 0xfffe) {
        if (size < 40 || buffer.readUInt16LE(start + 16) < 22
          || buffer.toString('hex', start + 24, start + 40) !== '0300000000001000800000aa00389b71')
          throw new Error('Not extensible IEEE float');
        tag = 3;
      }
      format = { tag, channels: buffer.readUInt16LE(start + 2), rate: buffer.readUInt32LE(start + 4),
        align: buffer.readUInt16LE(start + 12), bits: buffer.readUInt16LE(start + 14) };
    }
    if (id === 'data') {
      if (data) throw new Error('Multiple data chunks');
      data = buffer.subarray(start, end);
    }
    at = end + (size & 1);
  }
  if (!format || !data || format.tag !== 3 || format.bits !== 32
    || ![1, 2].includes(format.channels) || format.align !== format.channels * 4
    || ![44100, 48000, 88200, 96000, 176400, 192000].includes(format.rate)
    || data.length % format.align) throw new Error('Unsupported native float WAV');
  const frames = data.length / format.align;
  if (frames === 0 || frames > format.rate * 60) throw new Error('Invalid excerpt length');
  const step = Math.ceil(format.rate / 1000), wave = [];
  let peak = 0, energy = 0;
  for (let begin = 0; begin < frames; begin += step) {
    let low = 0, high = 0;
    for (let frame = begin; frame < Math.min(frames, begin + step); frame++) {
      for (let channel = 0; channel < format.channels; channel++) {
        const sample = data.readFloatLE(frame * format.align + channel * 4);
        if (!Number.isFinite(sample)) throw new Error('Non-finite PCM');
        low = Math.min(low, sample); high = Math.max(high, sample);
        peak = Math.max(peak, Math.abs(sample)); energy += sample * sample;
      }
    }
    // Envelope precision is display-only. Positions remain native integer samples.
    wave.push(Math.round(low * 32767), Math.round(high * 32767));
  }
  return { rate: format.rate, channels: format.channels, frames, step, wave, peak,
    rms: Math.sqrt(energy / (frames * format.channels)), pcmBytes: data.length };
}
