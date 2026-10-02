import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { createPlan, executePlan, parseArgs, sourceSnapshot, PROJECT_ROOT } from './build_hypha.mjs';
import { expectedArtifacts, inspectPe, rejectSignedArtifacts, verifyArtifacts } from './hypha_build_artifacts.mjs';

function fixture(t) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-unified-build-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const root = path.join(tmp, 'repo with spaces');
  const sdk = path.join(tmp, 'external sdk');
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.mkdirSync(path.join(sdk, 'Interfaces/ACF'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const options = parseArgs(['--sdk', sdk, '--license-confirmed'], {});
  return { root, sdk, options };
}

function pe(machine = 0x8664, certificateBytes = 0) {
  const bytes = Buffer.alloc(512);
  bytes.write('MZ'); bytes.writeUInt32LE(64, 60); bytes.write('PE\0\0', 64);
  bytes.writeUInt16LE(machine, 68); bytes.writeUInt16LE(0x20b, 88);
  bytes.writeUInt32LE(certificateBytes, 64 + 24 + 112 + 4 * 8 + 4);
  return bytes;
}

function populate(plan, platform = plan.platform) {
  for (const a of expectedArtifacts(platform, plan.buildDir, plan.formats)) {
    fs.mkdirSync(path.dirname(a.executable), { recursive: true });
    fs.writeFileSync(a.executable, platform === 'windows' ? pe() : Buffer.from(`fixture-${a.role}-${a.format}`));
  }
}

function inspectionRunner(tool, args) {
  if (tool === 'lipo') return 'x86_64 arm64';
  if (tool === 'powershell.exe') return '{"File":"1.2.3","Product":"1.2.3"}';
  if (tool === '/usr/libexec/PlistBuddy') {
    if (args[1].includes('CFBundleShortVersionString')) return '1.2.3';
    if (args[1].includes('CFBundleExecutable')) return args[2].includes('PRE') ? 'Kirin Hypha PRE' : 'Kirin Hypha POST';
    if (args[1].includes('KirinHyphaAudioSuiteEnabled')) return 'false';
    if (args[1].includes('KirinHyphaAaxBuildMode')) return 'diagnostic';
  }
  throw new Error(`Unexpected fixture command ${tool}`);
}

const snapshot = () => ({ commit: 'a'.repeat(40), bNumber: 'B-1', state: 'clean source', fingerprint: 'b'.repeat(64) });

test('macOS plans two FFI architectures and all six targets in one incremental native graph', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' });
  assert.equal(p.commands.filter(c => c.tool === 'cargo').length, 2);
  assert.equal(p.commands.filter(c => c.tool === 'cmake').length, 2);
  const build = p.commands.at(-1).args;
  assert.equal(build.filter(a => /^KirinHypha(?:PRE|POST)_/.test(a)).length, 6);
  assert.ok(p.commands.find(c => c.tool === 'cmake').args.includes('-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64'));
  assert.ok(!JSON.stringify(p).includes('--clean-first'));
  assert.ok(!JSON.stringify(p).includes('wraptool'));
});

test('Windows plans explicit MSVC x64, four targets, one FFI build, and no AU', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'win32' });
  assert.equal(p.commands.filter(c => c.tool === 'cargo').length, 1);
  assert.ok(p.commands[0].args.includes('x86_64-pc-windows-msvc'));
  assert.ok(p.commands.find(c => c.tool === 'cmake').args.includes('Visual Studio 17 2022'));
  assert.equal(p.commands.at(-1).args.filter(a => /^KirinHypha(?:PRE|POST)_/.test(a)).length, 4);
  assert.ok(!p.commands.at(-1).args.some(a => a.endsWith('_AU')));
});

test('SDK-free quick tests explicitly disable AAX and keep both roles and native architectures', t => {
  const f = fixture(t);
  const options = parseArgs(['--without-aax'], { KIRIN_AAX_SDK_PATH: 'nonexistent-sdk' });
  for (const [host, formats] of [['darwin', ['AU', 'VST3']], ['win32', ['VST3']]]) {
    const p = createPlan(options, { root: f.root, host });
    const full = createPlan(f.options, { root: f.root, host });
    assert.deepEqual(p.formats, formats); assert.notEqual(p.buildDir, full.buildDir);
    assert.match(p.buildDir, /-no-aax$/);
    const config = p.commands.find(c => c.tool === 'cmake').args;
    assert.ok(config.includes('-DKIRIN_HYPHA_AAX_SDK_PATH='));
    assert.ok(config.includes('-DKIRIN_HYPHA_REQUIRE_AAX=OFF'));
    assert.ok(config.includes('-DKIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=OFF'));
    assert.equal(p.commands.at(-1).args.filter(a => /^KirinHypha(?:PRE|POST)_/.test(a)).length, formats.length * 2);
    assert.ok(!p.commands.at(-1).args.some(a => a.endsWith('_AAX')));
    assert.equal(p.arch, host === 'darwin' ? 'universal' : 'x64');
  }
});

test('SDK-free receipts recheck every selected bundle and cannot reuse all-format output', t => {
  const f = fixture(t); const options = parseArgs(['--without-aax'], {});
  for (const host of ['darwin', 'win32']) {
    const p = createPlan(options, { root: f.root, host }); populate(p);
    const deps = { run: () => {}, snapshot, verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} };
    const result = executePlan(p, options, deps);
    assert.equal(result.artifacts.length, host === 'darwin' ? 4 : 2);
    assert.ok(result.artifacts.every(a => a.format !== 'AAX'));
    assert.equal(result.notForDistribution, true); assert.equal(result.signed, false);
    assert.deepEqual(executePlan(p, { ...options, verifyOnly: true }, deps), result);
    const saved = JSON.parse(fs.readFileSync(p.manifestPath, 'utf8'));
    saved.platform = saved.platform.replace('-no-aax', '');
    fs.writeFileSync(p.manifestPath, JSON.stringify(saved));
    assert.throws(() => executePlan(p, { ...options, verifyOnly: true }, deps), /current source/);
  }
});

test('quick tests still reject missing output and protect signed bundles', t => {
  const f = fixture(t); const options = parseArgs(['--without-aax'], {});
  const p = createPlan(options, { root: f.root, host: 'darwin' });
  assert.throws(() => verifyArtifacts(p, inspectionRunner), /Missing or empty/);
  populate(p);
  const first = expectedArtifacts(p.platform, p.buildDir, p.formats)[0];
  fs.mkdirSync(path.join(first.bundle, 'Contents/_CodeSignature'));
  assert.throws(() => executePlan(p, options, { run: () => 'Authority=Developer ID Application',
    snapshot, log: () => {} }), /signed Mac/);
  assert.ok(fs.existsSync(first.executable)); assert.ok(!fs.existsSync(p.manifestPath));
});

test('quick-test CLI needs no SDK or license flags and its plan is read-only', () => {
  const env = { PATH: process.env.PATH, ...(process.platform === 'win32' ? { SystemRoot: process.env.SystemRoot } : {}) };
  const planning = ['darwin', 'win32'].includes(process.platform) ? [] : ['--platform', 'macos'];
  const output = execFileSync(process.execPath, [path.join(PROJECT_ROOT, 'scripts/build_hypha.mjs'),
    '--without-aax', '--dry-run', ...planning], { env, encoding: 'utf8', cwd: path.dirname(PROJECT_ROOT) });
  const p = JSON.parse(output);
  assert.ok(!p.formats.includes('AAX'));
  assert.equal(p.expectedBundles, process.platform === 'win32' ? 2 : 4);
  assert.equal(p.signing, 'not performed');
  assert.ok(p.commands.every(c => !['wraptool', 'xcrun', 'gh'].includes(c.tool)));
});

test('release mode refuses the SDK-free diagnostic shortcut rather than weakening release gates', () => {
  assert.throws(() => execFileSync(process.execPath, [path.join(PROJECT_ROOT, 'scripts/build_hypha.mjs'),
    '--release', '--without-aax', '--help'], { encoding: 'utf8', stdio: 'pipe' }),
  error => error.status === 1 && /Unknown release option: --without-aax/.test(error.stderr));
});

test('bad arguments and unsupported OS/architecture fail before tools', (t) => {
  const f = fixture(t);
  assert.throws(() => parseArgs(['--sign']), /Unknown option/);
  assert.throws(() => parseArgs(['--sdk', '--dry-run']), /requires a value/);
  for (const invalid of [{ jobs: 0 }, { jobs: '1.5' }, { jobs: 65 }, { buildId: '../escape' },
    { platform: 'linux' }, { licenseConfirmed: false }, { sdk: '' }]) {
    assert.throws(() => createPlan({ ...f.options, ...invalid }, { root: f.root, host: 'darwin' }));
  }
  assert.throws(() => createPlan({ ...f.options, platform: 'windows', arch: 'arm64', dryRun: true },
    { root: f.root, host: 'darwin' }), /not qualified/);
  assert.throws(() => createPlan({ ...f.options, platform: 'windows' },
    { root: f.root, host: 'darwin' }), /cross-OS/);
});

test('SDK marker, repository confinement and real symlinks are validated', (t) => {
  const f = fixture(t);
  const inside = path.join(f.root, 'sdk'); fs.mkdirSync(path.join(inside, 'Interfaces/ACF'), { recursive: true });
  const link = path.join(path.dirname(f.root), 'sdk-link'); fs.symlinkSync(inside, link, 'junction');
  for (const sdk of [inside, link]) {
    assert.throws(() => createPlan({ ...f.options, sdk }, { root: f.root, host: 'darwin' }), /outside/);
  }
  assert.throws(() => createPlan({ ...f.options, sdk: path.dirname(f.root) },
    { root: f.root, host: 'darwin' }), /Interfaces\/ACF/);
  fs.symlinkSync(path.dirname(f.root), path.join(f.root, 'target'), 'junction');
  assert.throws(() => createPlan(f.options, { root: f.root, host: 'darwin' }), /symlink/);
});

test('SDK environment is non-secret input, and CLI override wins', () => {
  assert.equal(parseArgs([], { KIRIN_AAX_SDK_PATH: 'env-sdk' }).sdk, 'env-sdk');
  assert.equal(parseArgs(['--sdk', 'cli-sdk'], { KIRIN_AAX_SDK_PATH: 'env-sdk' }).sdk, 'cli-sdk');
});

test('dry-run is read-only, permits cross-OS planning, and ignores all signing secrets', (t) => {
  const f = fixture(t); const options = { ...f.options, dryRun: true, platform: 'windows' };
  const plan = createPlan(options, { root: f.root, host: 'darwin' });
  let printed = '';
  executePlan(plan, options, { run: () => assert.fail('dry-run ran a tool'),
    snapshot: () => assert.fail('dry-run read source'), log: value => { printed += value; } });
  assert.match(printed, /windows-x64/); assert.ok(!fs.existsSync(plan.buildDir));
  const output = execFileSync(process.execPath, [path.join(PROJECT_ROOT, 'scripts/build_hypha.mjs'),
    '--sdk', f.sdk, '--license-confirmed', '--platform', 'macos', '--dry-run'], { encoding: 'utf8',
    cwd: path.dirname(f.root),
    env: { ...process.env, KIRIN_AAX_PACE_CUSTOMER_NUMBER: 'fixture-private-input' } });
  assert.ok(!output.includes('fixture-private-input'));
});

test('one successful graph writes measured diagnostic receipt and supports verify-only', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' });
  const calls = []; populate(p);
  const result = executePlan(p, f.options, { run: (tool, args) => calls.push({ tool, args }),
    snapshot, verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} });
  assert.equal(result.artifacts.length, 6); assert.equal(result.signed, false);
  assert.equal(result.notForDistribution, true); assert.equal(calls.length, 7);
  assert.ok(!fs.existsSync(path.join(p.buildDir, '.hypha-build.lock')));
  const verified = executePlan(p, { ...f.options, verifyOnly: true }, { snapshot,
    verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} });
  assert.deepEqual(verified, result);
});

test('failure invalidates stale success without deleting bundles; retry releases lock', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' }); populate(p);
  fs.writeFileSync(p.manifestPath, 'old receipt');
  assert.throws(() => executePlan(p, f.options, { run: tool => { if (tool === 'cmake') throw new Error('compiler failure'); },
    snapshot, log: () => {} }), /compiler failure/);
  assert.ok(!fs.existsSync(p.manifestPath));
  assert.ok(fs.existsSync(expectedArtifacts('macos', p.buildDir)[0].executable));
  assert.ok(!fs.existsSync(path.join(p.buildDir, '.hypha-build.lock')));
  assert.equal(executePlan(p, f.options, { run: () => {}, snapshot,
    verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} }).artifacts.length, 6);
});

test('missing output and a changed source cannot become PASS', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' });
  assert.throws(() => executePlan(p, f.options, { run: () => {}, snapshot,
    verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} }), /Missing or empty/);
  populate(p); let reads = 0;
  assert.throws(() => executePlan(p, f.options, { run: () => {},
    snapshot: () => ({ ...snapshot(), fingerprint: String(reads++) }),
    verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} }), /Source changed/);
  assert.ok(!fs.existsSync(p.manifestPath));
});

test('concurrent lock and signed receipt are retained rather than overwritten', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' });
  fs.mkdirSync(p.buildDir, { recursive: true });
  const lock = path.join(p.buildDir, '.hypha-build.lock'); fs.writeFileSync(lock, 'other build');
  assert.throws(() => executePlan(p, f.options)); assert.equal(fs.readFileSync(lock, 'utf8'), 'other build');
  fs.unlinkSync(lock);
  const receipt = path.join(p.buildDir, 'kirin-hypha-macos-aax-notarization.json'); fs.writeFileSync(receipt, 'preserve');
  fs.writeFileSync(p.manifestPath, 'preserve old manifest');
  assert.throws(() => executePlan(p, f.options), /overwrite release output/);
  assert.equal(fs.readFileSync(receipt, 'utf8'), 'preserve');
  assert.equal(fs.readFileSync(p.manifestPath, 'utf8'), 'preserve old manifest');
});

test('Mac rejects missing architecture/version/Native-only marker', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' }); populate(p);
  for (const replacement of ['arm64', 'x86_64']) {
    assert.throws(() => verifyArtifacts(p, (tool, args) => tool === 'lipo' ? replacement : inspectionRunner(tool, args)), /Universal/);
  }
  assert.throws(() => verifyArtifacts(p, (tool, args) => args[1]?.includes('CFBundleShortVersionString')
    ? '0.0.0' : inspectionRunner(tool, args)), /mismatch/);
  assert.throws(() => verifyArtifacts(p, (tool, args) => args[1]?.includes('KirinHyphaAudioSuiteEnabled')
    ? 'true' : inspectionRunner(tool, args)), /Native-only/);
});

test('Windows verifies all four PE x64 headers and version resources', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'win32' }); populate(p);
  assert.equal(verifyArtifacts(p, inspectionRunner).length, 4);
  fs.writeFileSync(expectedArtifacts('windows', p.buildDir)[0].executable, pe(0xaa64));
  assert.throws(() => verifyArtifacts(p, inspectionRunner), /not Windows x64/);
});

test('PE bounds, optional-header width and signed Windows output fail closed', (t) => {
  for (const bytes of [Buffer.alloc(0), Buffer.alloc(64), pe().subarray(0, 100)]) {
    assert.throws(() => inspectPe(bytes), /PE/);
  }
  const wrong = pe(); wrong.writeUInt16LE(0x10b, 88);
  assert.throws(() => inspectPe(wrong), /64-bit/);
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'win32' }); populate(p);
  fs.writeFileSync(expectedArtifacts('windows', p.buildDir)[0].executable, pe(0x8664, 1024));
  assert.throws(() => rejectSignedArtifacts('windows', p.buildDir, () => {}), /signed Windows/);
});

test('Developer ID output is protected while local ad-hoc output is rebuildable', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' }); populate(p);
  fs.mkdirSync(path.join(expectedArtifacts('macos', p.buildDir)[0].bundle, 'Contents/_CodeSignature'));
  assert.throws(() => rejectSignedArtifacts('macos', p.buildDir, () => 'Authority=Developer ID Application'), /signed Mac/);
  assert.doesNotThrow(() => rejectSignedArtifacts('macos', p.buildDir, () => 'Signature=adhoc'));
});

test('verify-only detects changed binary or source without rebuilding', (t) => {
  const f = fixture(t); const p = createPlan(f.options, { root: f.root, host: 'darwin' }); populate(p);
  executePlan(p, f.options, { run: () => {}, snapshot, verify: () => verifyArtifacts(p, inspectionRunner), log: () => {} });
  const opts = { ...f.options, verifyOnly: true };
  assert.throws(() => executePlan(p, opts, { snapshot: () => ({ ...snapshot(), fingerprint: 'changed' }) }), /current source/);
  fs.appendFileSync(expectedArtifacts('macos', p.buildDir)[0].executable, 'changed');
  assert.throws(() => executePlan(p, opts, { snapshot, verify: () => verifyArtifacts(p, inspectionRunner) }), /Artifact manifest mismatch/);
});

test('source fingerprint detects edits with unchanged dirty filenames', (t) => {
  const f = fixture(t);
  const git = args => execFileSync('git', args, { cwd: f.root, stdio: 'pipe' });
  git(['init']); git(['add', '.']);
  git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', '[B-1] fixture']);
  fs.writeFileSync(path.join(f.root, 'new-source.txt'), 'one'); const first = sourceSnapshot(f.root);
  fs.writeFileSync(path.join(f.root, 'new-source.txt'), 'two'); const second = sourceSnapshot(f.root);
  assert.notEqual(first.fingerprint, second.fingerprint);
  assert.equal(second.state, 'modified source');
});

test('source fingerprint includes JUCE working-tree bytes, not only its pinned commit', (t) => {
  const f = fixture(t);
  const git = (root, args) => execFileSync('git', args, { cwd: root, stdio: 'pipe' });
  const commit = (root) => {
    git(root, ['init']); git(root, ['add', '.']);
    git(root, ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
      'commit', '-m', '[B-1] fixture']);
  };
  commit(f.root);
  const juce = path.join(f.root, 'juce_shell/JUCE'); fs.mkdirSync(juce, { recursive: true });
  fs.writeFileSync(path.join(juce, 'source.cpp'), 'original'); commit(juce);
  // Use a real git submodule entry to match the production source boundary.
  git(f.root, ['add', 'juce_shell/JUCE']);
  git(f.root, ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
    'commit', '-m', '[B-2] submodule']);
  const before = sourceSnapshot(f.root);
  fs.writeFileSync(path.join(juce, 'source.cpp'), 'changed');
  const after = sourceSnapshot(f.root);
  assert.equal(before.commit, after.commit); assert.equal(before.juce.commit, after.juce.commit);
  assert.notEqual(before.juce.fingerprint, after.juce.fingerprint);
  assert.notEqual(before.fingerprint, after.fingerprint);
});
