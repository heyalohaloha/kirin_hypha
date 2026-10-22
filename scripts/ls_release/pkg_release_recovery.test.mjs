import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, executeRelease, STAGES, parseReleaseArgs } from '../release_hypha.mjs';
import { artifactPaths, packageAll } from './hypha_release_local.mjs';
import { finishPkgStaple, PkgNetworkPending } from './pkg_staple.mjs';
import { digest, fileFact, hash, acceptanceCriteria } from './hypha_release_contract.mjs';
import { updateBinding } from '../updates/update_key_binding.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-package-resume-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: ['B', 1234].join('-'), state: 'clean source', fingerprint: 'b'.repeat(64) };
  const statePath = path.join(root, 'release_state/profile.json'), state = makeState(root, { state: statePath }, () => source);
  state.freeze = { inputs: digest(state.inputs), updateCheck: updateBinding(''), criteriaSha256: digest(acceptanceCriteria(state)) };
  state.freeze.sha256 = digest(state.freeze);
  const files = artifactPaths(state), packagePath = files[0]; fs.mkdirSync(path.dirname(packagePath), { recursive: true });
  fs.writeFileSync(packagePath, 'xar!signed fixture payload');
  const context = { packagePath, statePath: path.join(path.dirname(path.resolve(root, state.inputs.hostsReport)), 'pkg-staple/receipt.json'),
    binding: state.freeze.sha256, identity: { source: { commit: source.commit, bNumber: source.bNumber }, version: '1.2.3', aaxIncluded: true, updatePublicKey: '' }, keychainProfile: 'fixture' };
  const binding = ids => ({ ...updateBinding(''), binaries: ids.map(id => ({ format: id.split('/')[0], role: id.split('/')[1], binarySha256: 'd'.repeat(64), publicKeySha256: '',
    architectures: ['arm64', 'x86_64'].map(architecture => ({ architecture, sha256: 'c'.repeat(64) })) })) });
  const mac = binding(['AU/PRE', 'AU/POST', 'VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  const win = binding(['VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  const metadata = { schema: 'kirin-hypha-pkg-artifact-v1', source: { commit: source.commit, bNumber: source.bNumber },
    version: '1.2.3', signed: true, aaxIncluded: true, updateCheck: mac, fileName: path.basename(packagePath) };
  const originalHash = fileFact(packagePath).sha256, submission = '12345678-1234-1234-1234-123456789abc';
  const runTicket = (tool, args) => {
    if (args[0] === 'notarytool') {
      if (args[1] === 'log') fs.writeFileSync(args[3], JSON.stringify({ jobId: submission, status: 'Accepted', sha256: originalHash,
        archiveFilename: path.basename(packagePath), logFormatVersion: 1, issues: null }));
      return { status: 0, stdout: JSON.stringify({ id: submission, status: 'Accepted' }) };
    }
    return { status: 68 };
  };
  assert.throws(() => finishPkgStaple(context, { wait: () => {}, run: runTicket, metadata, verifyLocal: () => mac }), PkgNetworkPending);
  state.stages.packages = { status: 'FAIL', evidence: { pkgStaple: fileFact(context.statePath) } };
  const legal = path.join(root, 'legal'), legalNames = ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt', 'source-delivery.json', 'component-notices.json'];
  const sourceSha = 'f'.repeat(64), url = 'https://github.com/heyalohaloha/kirin_hypha/releases/download/v1.2.3/source.zip';
  for (const name of legalNames) { fs.mkdirSync(path.dirname(path.join(legal, name)), { recursive: true }); fs.writeFileSync(path.join(legal, name), 'fixture'); }
  fs.writeFileSync(path.join(legal, 'source-delivery.json'), JSON.stringify({ commit: source.commit, sha256: sourceSha, url }));
  fs.writeFileSync(path.join(legal, 'component-notices.json'), JSON.stringify({ schema: 'hypha-delivered-components-v1', commit: source.commit, sourceSha256: sourceSha, components: [{}] }));
  fs.writeFileSync(path.join(legal, 'legal-delivery.json'), JSON.stringify({ schema: 'hypha-legal-delivery-v1', commit: source.commit, sourceSha256: sourceSha, sourceDownload: url,
    files: legalNames.map(name => ({ path: name, sha256: hash(fs.readFileSync(path.join(legal, name))) })) }));
  state.stages['source-delivery'] = { status: 'PASS', evidence: { legalDir: legal }, facts: [fileFact(path.join(legal, 'LICENSE'))] };
  const calls = [];
  const run = async (tool, args, options) => {
    calls.push([tool, ...args]);
    if (tool === 'node' && args[0].endsWith('build_kirin_hypha_pkg.mjs')) {
      assert.ok(args.includes('--resume-staple'));
      assert.equal(options.env.KIRIN_PKG_STAPLE_STATE, context.statePath);
      assert.equal(options.env.KIRIN_PKG_STAPLE_BINDING, context.binding);
      finishPkgStaple(context, { wait: () => {}, resume: true, verifyLocal: () => mac, run: (tool, a) => {
        assert.equal(a[0], 'stapler');
        if (a[1] === 'staple') { const ticket = Buffer.from('74386c720100010000000000000000007338636874386c72010002000400000000000000', 'hex'); fs.appendFileSync(packagePath, ticket); }
        return { status: 0 };
      } });
      return '';
    }
    assert.equal(tool, 'node'); assert.ok(args.includes('--skip-macos-pkg'));
    for (const file of files.slice(3)) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'artifact'); }
    fs.writeFileSync(files[5], JSON.stringify({ version: '1.2.3', commit: source.commit, unsigned_smoke_test: false,
      aax_included: true, git_dirty: 'false', updateCheck: mac, sha256: fileFact(files[3]).sha256 }));
    fs.writeFileSync(files[8], JSON.stringify({ updateCheck: win, installer: { payload: win.binaries.map(b => ({ role: b.role, format: b.format, binary_sha256: b.binarySha256 })) } }));
    for (const file of [files[3], files[6]]) fs.writeFileSync(`${file}.sha256`, `${fileFact(file).sha256}  ${path.basename(file)}\n`);
    return '';
  };
  return { state, root, statePath, source, files, context, calls, run };
}

test('a retained staple failure can finish packages without rebuilding the existing PKG', async t => {
  const f = fixture(t);
  await assert.rejects(packageAll(f.state, f.run));
  assert.deepEqual(f.calls, []);
  const facts = await packageAll(f.state, f.run, { resumeStaple: true });
  assert.equal(facts.find(row => row.path === f.context.packagePath)?.sha256, fileFact(f.context.packagePath).sha256);
  assert.ok(facts.some(row => row.path.endsWith('-macOS-Universal.zip')));
  assert.ok(facts.some(row => row.path.endsWith('-Windows-x64-Setup.exe')));
  assert.ok(f.calls.every(call => call.includes('--resume-staple') || call.includes('--skip-macos-pkg')));
  assert.ok(facts.every(row => !row.path.includes('pkg-staple')));
});
test('explicit coordinator resume preserves the packages FAIL history and records private evidence separately', async t => {
  const f = fixture(t), evidence = fileFact(f.context.packagePath);
  const ids = STAGES.slice(0, STAGES.indexOf('packages'));
  for (const id of ids) if (!f.state.stages[id]) f.state.stages[id] = { status: 'PASS', facts: [evidence] };
  const actions = Object.fromEntries(STAGES.map(id => [id, async () => { throw new Error('only packages may execute'); }]));
  actions.packages = async () => ({ facts: await packageAll(f.state, f.run, { resumeStaple: true }) });
  assert.throws(() => parseReleaseArgs(['--state', f.statePath, '--resume-pkg-staple']));
  const options = parseReleaseArgs(['--state', f.statePath, '--execute', '--until', 'packages', '--resume-pkg-staple']);
  await executeRelease(f.state, options, { statePath: f.statePath, snapshot: () => f.source, clean: () => {}, actions, log: () => {} });
  assert.equal(f.state.stages.packages.status, 'PASS', f.state.blocker?.reason);
  assert.equal(f.state.stages.packages.previous[0].status, 'FAIL');
});
