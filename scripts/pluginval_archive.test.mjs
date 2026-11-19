import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { resolvePin, verifyArchive } from './pluginval_archive.mjs';

function fixture(run) {
  const dir = mkdtempSync(join(tmpdir(), 'pluginval-pin-'));
  try {
    // A small, empty ZIP. No real vendor tool is executed by these tests.
    const bytes = Buffer.from('504b0506000000000000000000000000000000000000', 'hex');
    const path = join(dir, 'fixture.zip'); writeFileSync(path, bytes);
    const pin = { bytes: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
    run({ dir, path, bytes, pin });
  } finally { rmSync(dir, { recursive: true, force: true }); }
}
test('reviewed pins name exact official assets and executables', () => {
  for (const os of ['macOS', 'Windows']) {
    const p = resolvePin('v1.0.4', os);
    assert.match(p.url, /^https:\/\/github\.com\/Tracktion\/pluginval\/releases\/download\/v1\.0\.4\//);
    assert.match(p.sha256, /^[a-f0-9]{64}$/); assert.ok(p.bytes > 0);
    assert.ok(!p.executable.includes('..'));
  }
});
test('unknown version, latest and unknown platform fail closed', () => {
  for (const v of ['latest', '', 'v9.9.9']) assert.throws(() => resolvePin(v, 'macOS'), /Unregistered/);
  assert.throws(() => resolvePin('v1.0.4', 'Linux'), /Unregistered/);
});
test('correct fixture hash verifies the entire archive', () => fixture(({ pin, path }) => {
  assert.equal(verifyArchive(pin, path).verifiedSha256, pin.sha256);
}));
test('same-size one-byte change fails before consumer may extract', () => fixture(({ pin, path, bytes }) => {
  bytes[5] ^= 1; writeFileSync(path, bytes);
  let extracted = false;
  assert.throws(() => { verifyArchive(pin, path); extracted = true; }, /integrity mismatch/);
  assert.equal(extracted, false);
}));
test('truncation fails', () => fixture(({ pin, path, bytes }) => {
  writeFileSync(path, bytes.subarray(1)); assert.throws(() => verifyArchive(pin, path), /integrity mismatch/);
}));
test('missing and malformed pins fail', () => fixture(({ pin, path }) => {
  for (const bad of [{}, { ...pin, sha256: 'x' }, { ...pin, bytes: 0 }, { ...pin, bytes: NaN }])
    assert.throws(() => verifyArchive(bad, path), /pin/);
}));
test('cache executable alone is not an archive receipt', () => fixture(({ dir, pin }) => {
  writeFileSync(join(dir, 'pluginval.exe'), 'stale');
  assert.throws(() => verifyArchive(pin, join(dir, 'missing.zip')));
  mkdirSync(join(dir, 'directory.zip'));
  assert.throws(() => verifyArchive(pin, join(dir, 'directory.zip')), /regular file/);
}));
test('environment SHA cannot override reviewed CLI pin', () => fixture(({ path, pin }) => {
  const r = spawnSync(process.execPath, [fileURLToPath(new URL('./pluginval_archive.mjs', import.meta.url)),
    'verify', 'v1.0.4', 'macOS', path], { encoding: 'utf8', env: { ...process.env, PLUGINVAL_SHA256: pin.sha256 } });
  assert.equal(r.status, 1); assert.match(r.stderr, /integrity mismatch/);
}));
test('macOS verifies cached archive before fresh extraction, not old app reuse', () => {
  const s = readFileSync(new URL('./validate_macos_pluginval.sh', import.meta.url), 'utf8');
  const verify = s.indexOf('node scripts/pluginval_archive.mjs verify');
  assert.ok(verify > s.indexOf('for attempt in'));
  assert.ok(verify < s.indexOf('VERIFIED_STAGE='));
  assert.ok(s.indexOf('VERIFIED_STAGE=') < s.indexOf('unzip -q'));
  assert.ok(s.includes('mktemp -d "$CACHE_DIR/verified.XXXXXX"'));
  assert.ok(!s.includes('PLUGINVAL_EXE="$CACHE_DIR/pluginval.app'));
  assert.ok(!s.includes('releases/latest/'));
  assert.ok(s.includes('CI requires a pinned archive'));
  assert.ok(s.includes('exit 2')); // Explicit override remains diagnostic, never formal success.
});
test('Windows integrity failure is outside retry and extraction is a fresh exact stage', () => {
  const s = readFileSync(new URL('../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const start = s.indexOf('# Exact reviewed official archive;');
  const end = s.indexOf('$bundles =', start); const section = s.slice(start, end);
  assert.ok(start >= 0 && end > start);
  const verify = section.indexOf('node scripts/pluginval_archive.mjs verify');
  const extract = section.indexOf('Expand-Archive');
  assert.ok(verify > section.indexOf('# Integrity failure is outside the retry loop'));
  assert.ok(extract > verify);
  assert.ok(section.slice(verify, extract).includes("if ($LASTEXITCODE -ne 0) { throw"));
  assert.ok(section.includes('[guid]::NewGuid()'));
  assert.ok(section.includes('$exe = Join-Path $stage $pin.executable'));
  assert.ok(!section.includes('-Force')); assert.ok(!section.includes('Select-Object -First'));
});
