import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
export const baseline = path.join(root, 'juce_shell/tests/fixtures/freq-default');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const requireEqual = (a, b, message) => {
  if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error(message);
};
const expected = [300, 600, 900].flatMap(width => ['en', 'ja'].map(lang => `${width}-${lang}`)).sort();
function cases(render) {
  if (!Array.isArray(render.cases)) throw new Error('missing cases');
  requireEqual(render.cases.map(row => row.name).sort(), expected, 'missing/duplicate cases');
  return new Map(render.cases.map(row => [row.name, row]));
}
function png(file, width, height) {
  const data = fs.readFileSync(file);
  if (data.length < 24 || data.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a'
      || data.toString('ascii', 12, 16) !== 'IHDR'
      || data.readUInt32BE(16) !== width || data.readUInt32BE(20) !== height)
    throw new Error('invalid PNG dimensions');
  return hash(data);
}
export function check(actual, binary, source, base = baseline) {
  if (fs.realpathSync(actual) === fs.realpathSync(base)) throw new Error('fresh render required');
  if (!/^[0-9a-f]{40}$/.test(source)) throw new Error('full source commit required');
  const provenance = read(path.join(base, 'bundle.json'));
  for (const key of ['source', 'binary_sha256', 'fixture_sha256', 'backend', 'os', 'juce'])
    if (!provenance[key]) throw new Error(`missing baseline provenance: ${key}`);
  const fixture = Buffer.concat(['PriorFreqFixture.h', 'SpectrumPerformanceFixture.h']
    .map(name => fs.readFileSync(path.join(root, 'juce_shell/tests', name))));
  requireEqual(hash(fixture), provenance.fixture_sha256, 'fixture changed; review required');
  const before = read(path.join(base, 'render.json'));
  const after = read(path.join(actual, 'render.json'));
  for (const key of ['backend', 'os', 'juce', 'mode'])
    requireEqual(after[key], before[key], `incompatible ${key}`);
  const reference = cases(before), rendered = cases(after);
  for (const name of expected) {
    const a = reference.get(name), b = rendered.get(name), width = Number(name.split('-')[0]);
    requireEqual([a.width, a.height], [width, width * 2 / 3], 'baseline dimensions');
    requireEqual([b.width, b.height], [a.width, a.height], 'render dimensions');
    requireEqual(b.labels, a.labels, `text or bounds changed: ${name}`);
    requireEqual(b.rgba_sha256, a.rgba_sha256, `RGBA changed: ${name}`);
    const file = `${name}.png`;
    requireEqual(png(path.join(base, file), a.width, a.height), provenance.png_sha256[file], 'baseline changed');
    requireEqual(png(path.join(actual, file), b.width, b.height), provenance.png_sha256[file], `PNG changed: ${name}`);
  }
  return { status: 'PASS', cases: expected.length, source,
    binary_sha256: hash(fs.readFileSync(binary)), baseline_source: provenance.source,
    fixture_sha256: provenance.fixture_sha256 };
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 5) throw new Error('usage: node scripts/check_freq_baseline.mjs RENDER_DIR BINARY SOURCE_COMMIT');
    console.log(JSON.stringify(check(...process.argv.slice(2)), null, 2));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
