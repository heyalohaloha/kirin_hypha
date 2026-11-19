import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { resolvePin, verifyArchive } from './pluginval_archive.mjs';
import { runGateFixture } from './macos_pluginval_fixture.mjs';

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
  assert.equal(resolvePin('v1.0.4', 'macOS').url,
    'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip');
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

// New behavioral fixtures require a POSIX shell; the macOS gate never ships on Windows.
const shellFixture = { skip: process.platform === 'win32' };
const operations = (r, name) => r.events.filter(e => e.operation === name);
const gateSource = () => readFileSync(new URL('./validate_macos_pluginval.sh', import.meta.url), 'utf8');
function checkGateCase(kind, r) {
  assert.equal(r.status, kind === 'ci-override' || kind.startsWith('bad-') ? 1 : kind === 'local-override' ? 2 : 0, r.stderr);
  if (kind === 'ci-override') {
    assert.match(r.stderr, /CI requires a pinned archive/); assert.deepEqual(r.events, []);
  } else if (kind === 'local-override') {
    assert.equal(operations(r,'execute').length,2); assert.equal(operations(r,'extract').length,0);
    assert.equal(operations(r,'verify').length,0); assert.match(r.stderr,/NOT a formal qualification/);
  } else if (kind.startsWith('bad-')) {
    assert.match(r.stderr,/integrity mismatch/); assert.equal(operations(r,'verify').length,1);
    assert.equal(operations(r,'verified')[0].status,1);
    assert.equal(operations(r,'extract').length,0); assert.equal(operations(r,'execute').length,0);
    assert.equal(operations(r,'bundle-check').length,0);
  } else {
    const [verify]=operations(r,'verify'), [extract]=operations(r,'extract');
    assert.equal(operations(r,'verify').length,1); assert.equal(operations(r,'extract').length,1);
    assert.equal(verify.archive,extract.archive); assert.notEqual(verify.archive,r.cachePath);
    assert.equal(verify.sha256,r.goodSha256); assert.equal(extract.sha256,r.goodSha256);
    assert.equal(operations(r,'verified')[0].status,0);
    assert.equal(operations(r,'execute').length,2); assert.equal(operations(r,'bundle-check').length,1);
    if (kind==='cache-swap') {
      assert.equal(operations(r,'shared-cache-replaced').length,1); assert.equal(r.cacheSha256,r.badSha256);
    } else assert.equal(r.cacheSha256,r.goodSha256);
  }
  assert.equal(operations(r,'download').length,['download','bad-download'].includes(kind)?1:0);
  assert.deepEqual(r.cacheEntries,kind==='bad-download'?[]:['pluginval_macOS.zip']);
}
for (const kind of ['ci-override','local-override','bad-cache','bad-download','cached','download','cache-swap'])
  test('macOS gate behavior: '+kind,shellFixture,()=>checkGateCase(kind,runGateFixture(kind)));

test('macOS fixtures reject a missing CI exit, formal local override and ignored verification',shellFixture,()=>{
  const original=gateSource();
  const changes=[
    ['ci-override','    exit 1\n  fi\n  PINNED_TOOL=0','    :\n  fi\n  PINNED_TOOL=0'],
    ['local-override','  PINNED_TOOL=0','  PINNED_TOOL=1'],
    ['bad-cache','macOS "$ZIP" > "$OUTPUT_DIR/pluginval-tool.json"','macOS "$ZIP" > "$OUTPUT_DIR/pluginval-tool.json" || true'],
  ];
  for (const [kind,before,after] of changes) {
    assert.equal(original.split(before).length,2,'mutation must select exactly one boundary');
    const r=runGateFixture(kind,{script:original.replace(before,after)});
    assert.throws(()=>checkGateCase(kind,r),undefined,'fixture must reject '+kind+' regression');
  }
});
test('macOS fixture detects a return to shared ZIP after successful verification',shellFixture,()=>{
  const original=gateSource(),before='  unzip -q "$ZIP" -d "$VERIFIED_STAGE"';
  assert.equal(original.split(before).length,2);
  const r=runGateFixture('cache-swap',{script:original.replace(before,'  unzip -q "$CACHE_ZIP" -d "$VERIFIED_STAGE"')});
  assert.equal(r.status,0); assert.equal(operations(r,'extract')[0].sha256,r.badSha256);
  assert.throws(()=>checkGateCase('cache-swap',r));
});
