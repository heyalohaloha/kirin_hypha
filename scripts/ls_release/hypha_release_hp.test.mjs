import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { updateHpDocuments, publishGithub, checkTag, publicDownloadFacts, verifyPublicHp,
  hpPreflight, publishHp, HP_FILES } from './hypha_release_hp.mjs';
import { fileFact, hash, Checkpoint } from './hypha_release_contract.mjs';

const base = 'https://github.com/heyalohaloha/kirin_hypha/releases';
function page(lang, version = '1.1.49') {
  return `<html lang="${lang}"><section id="downloads"><h2 id="hypha-download-title">Hypha ${version}</h2>
<time datetime="2026-09-02">2026-09-02</time>
<a href="${base}/download/v${version}/Kirin-Hypha-${version}-macOS-Universal.pkg">PKG</a>
<a href="${base}/download/v${version}/Kirin-Hypha-${version}-Windows-x64-Setup.exe">Download for Windows VST3</a>
macOS 12+ (VST3 + Audio Unit) / Windows 10/11 (VST3)
<a href="${base}/tag/v${version}">${version}</a></section></html>`;
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-hp-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.writeFileSync(path.join(root, 'AGENTS.md'), '# GitHub budget policy\n');
  const payload = path.join(root, 'payload.pkg'); fs.writeFileSync(payload, 'signed fixture bytes');
  return { root, payload, state: { root, candidate: { id: 'candidate', version: '1.2.3', commit: 'a'.repeat(40) },
    inputs: { notes: 'reviewed-notes.md', date: '2026-10-01' }, stages: { packages: { facts: [fileFact(payload)] } } } };
}

test('HP transforms both real-shaped languages, installer links, version/date tests and AAX formats together', () => {
  const docs = [page('en'), page('ja'), 'v1.1.49 /1\\.1\\.49/ 2026-09-02'];
  const result = updateHpDocuments(docs, '1.2.3', '2026-10-01');
  assert.ok(result.every(s => !s.includes('1.1.49') && !s.includes('2026-09-02')));
  assert.match(result[2], /1\\\.2\\\.3/);
  for (const html of result.slice(0, 2)) {
    assert.match(html, /Audio Unit \+ AAX/); assert.match(html, /Windows 10\/11 \(VST3 \+ AAX\)/);
    assert.match(html, /macOS-Universal\.pkg/); assert.match(html, /Windows-x64-Setup\.exe/);
  }
  assert.deepEqual(docs, [page('en'), page('ja'), 'v1.1.49 /1\\.1\\.49/ 2026-09-02']);
});

test('changed/mixed HP markup or zip-only primary channels require review', () => {
  assert.throws(() => updateHpDocuments([page('en'), page('ja', '1.1.50'), 'test'], '1.2.3', '2026-10-01'), /version mismatch/);
  assert.throws(() => updateHpDocuments([page('en').replace('.pkg', '.zip'), page('ja'), 'test'], '1.2.3', '2026-10-01'), /PKG/);
  assert.throws(() => updateHpDocuments([page('en'), page('ja'), 'test'], 'latest', '2026-10-01'), /Invalid/);
});

function githubMock(f, { published = false, existingAsset = false, corrupt = false } = {}) {
  let release = published ? { id: 1, draft: false, target_commitish: f.state.candidate.commit, assets: [], html_url: 'release' } : null;
  const fact = f.state.stages.packages.facts[0]; const calls = [];
  const asset = () => ({ name: path.basename(f.payload), size: fact.bytes, digest: `sha256:${fact.sha256}` });
  if (release || existingAsset) {
    release ||= { id: 1, draft: true, target_commitish: f.state.candidate.commit, assets: [], html_url: 'release' };
    release.assets.push(asset());
  }
  const run = async (tool, args) => {
    calls.push([tool, args]); assert.equal(tool, 'gh');
    if (args[0] === 'api') {
      if (args[1].includes('/git/ref/')) return release?.draft === false
        ? JSON.stringify({ object: { type: 'commit', sha: f.state.candidate.commit } }) : '';
      if (args[1].includes('/commits/')) return JSON.stringify({ sha: f.state.candidate.commit });
      if (args[1].includes('/releases/tags/')) return release ? JSON.stringify(release) : '';
    }
    if (args[1] === 'create') release = { id: 1, draft: true, assets: [], target_commitish: f.state.candidate.commit, html_url: 'release' };
    if (args[1] === 'upload') release.assets.push(asset());
    if (args[1] === 'download') {
      const dir = args[args.indexOf('--dir') + 1];
      fs.writeFileSync(path.join(dir, path.basename(f.payload)), corrupt ? 'corrupt' : fs.readFileSync(f.payload));
    }
    if (args[1] === 'edit') release.draft = false;
    return '';
  };
  return { run, calls };
}

test('GitHub stages an immutable draft, hashes read-back before publish, pins exact commit', async t => {
  const f = fixture(t); const m = githubMock(f);
  await publishGithub(f.state, m.run, () => {});
  const create = m.calls.find(([, a]) => a[1] === 'create')[1];
  assert.equal(create[create.indexOf('--target') + 1], f.state.candidate.commit);
  assert.ok(create.includes('--draft'));
  assert.ok(m.calls.findIndex(([, a]) => a[1] === 'download') < m.calls.findIndex(([, a]) => a[1] === 'edit'));
  assert.ok(!m.calls.some(([, a]) => a.includes('--clobber') || a[1] === 'delete'));
});

test('interrupted draft upload and published same-byte release are reusable without duplicate upload', async t => {
  const f = fixture(t);
  for (const scenario of [{ existingAsset: true }, { published: true }]) {
    const m = githubMock(f, scenario); await publishGithub(f.state, m.run, () => {});
    assert.ok(!m.calls.some(([, a]) => ['create', 'upload'].includes(a[1])));
  }
});

test('wrong published bytes, foreign tags and authentication failure never become new release', async t => {
  const f = fixture(t); const m = githubMock(f, { existingAsset: true, corrupt: true });
  await assert.rejects(publishGithub(f.state, m.run, () => {}), /bytes/);
  assert.ok(!m.calls.some(([, a]) => a[1] === 'edit'));
  await assert.rejects(checkTag(f.state, async () => JSON.stringify({ object: { type: 'commit', sha: 'f'.repeat(40) } })), /frozen/);
  await assert.rejects(publishGithub(f.state, async () => { throw new Error('HTTP 403'); }, () => {}), /403/);
});

test('public-user-path download validates actual streamed bytes, not HEAD 200 or asset names', async t => {
  const f = fixture(t); const data = fs.readFileSync(f.payload);
  let requests = 0;
  const fetcher = async url => { requests++; assert.match(url, /download\/v1\.2\.3\/payload.pkg/); return new Response(data); };
  assert.equal((await publicDownloadFacts(f.state, f.state.stages.packages.facts, fetcher))[0].sha256, hash(data));
  assert.equal(requests, 1);
  await assert.rejects(publicDownloadFacts(f.state, f.state.stages.packages.facts,
    async () => new Response('wrong')), /hash mismatch/);
  await assert.rejects(publicDownloadFacts(f.state, f.state.stages.packages.facts,
    async () => new Response('no', { status: 404 })), /download failed/);
});

test('HP public check validates both languages, both platforms and exact deployed source', async t => {
  const f = fixture(t); f.state.hpCommit = 'c'.repeat(40);
  const docs = updateHpDocuments([page('en'), page('ja'), 'test'], '1.2.3', '2026-10-01');
  const responses = url => url.includes('deployment-source') ? f.state.hpCommit : docs[url.includes('/ja/') ? 1 : 0];
  await verifyPublicHp(f.state, async url => new Response(responses(url)));
  await assert.rejects(verifyPublicHp(f.state, async url => new Response(url.includes('/ja/') ? docs[0] : responses(url))), /language/);
  await assert.rejects(verifyPublicHp(f.state, async url => new Response(url.includes('deployment-source') ? 'old' : responses(url))), /source commit/);
});

test('HP dirtiness, target drift and wrong baseline stop before any production command', async t => {
  const f = fixture(t); const root = path.join(f.root, 'hp'); fs.mkdirSync(path.join(root, '.vercel'), { recursive: true });
  fs.writeFileSync(path.join(root, 'README_DEPLOY.md'), 'standard clean deploy');
  fs.writeFileSync(path.join(root, '.vercel/project.json'), JSON.stringify({ projectName: 'expected', projectId: 'expected-id' }));
  for (const [i, file] of HP_FILES.entries()) { fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true });
    fs.writeFileSync(path.join(root, file), i < 2 ? page(i === 0 ? 'en' : 'ja') : 'test'); }
  f.state.inputs.hpRoot = root; f.state.inputs.hpBaseCommit = 'c'.repeat(40);
  f.state.inputs.hpProjectSha256 = fileFact(path.join(root, '.vercel/project.json')).sha256;
  let dirty = ''; let target = 'expected expected-id';
  const run = async (tool, args) => {
    if (tool === 'vercel') return target;
    if (args[0] === 'branch') return 'main';
    if (args[0] === 'status') return dirty;
    return f.state.inputs.hpBaseCommit;
  };
  await hpPreflight(f.state, run);
  dirty = ' M other.html'; await assert.rejects(hpPreflight(f.state, run), Checkpoint);
  dirty = ''; target = 'different'; await assert.rejects(hpPreflight(f.state, run), /project name\/ID/);
});

test('HP workflow resumes only its own planned changes and uses the standard verified deployment once', async t => {
  const f = fixture(t); const root = path.join(f.root, 'hp'); fs.mkdirSync(path.join(root, '.vercel'), { recursive: true });
  fs.writeFileSync(path.join(root, 'README_DEPLOY.md'), 'standard clean deployment');
  fs.writeFileSync(path.join(root, '.vercel/project.json'), JSON.stringify({ projectName: 'expected', projectId: 'expected-id' }));
  for (const [i, file] of HP_FILES.entries()) {
    fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true });
    fs.writeFileSync(path.join(root, file), i < 2 ? page(i === 0 ? 'en' : 'ja') : 'v1.1.49 2026-09-02');
  }
  f.state.inputs.hpRoot = root; f.state.inputs.hpBaseCommit = 'c'.repeat(40);
  f.state.inputs.hpProjectSha256 = fileFact(path.join(root, '.vercel/project.json')).sha256;
  let head = f.state.inputs.hpBaseCommit; let remote = head; let failTests = true; let deployed = false;
  const calls = [];
  const run = async (tool, args) => {
    calls.push([tool, args]);
    if (tool === 'vercel') { assert.deepEqual(args, ['project', 'inspect', '--non-interactive']); return 'expected expected-id'; }
    if (tool === 'npm') { if (failTests) throw new Error('HP tests failed'); return ''; }
    if (tool === 'bash') { assert.deepEqual(args, ['scripts/deploy-production-clean.sh']); deployed = true; return ''; }
    assert.equal(tool, 'git');
    if (args[0] === 'branch') return 'main';
    if (args[0] === 'rev-parse') return head;
    if (args[0] === 'ls-remote') return remote;
    if (args[0] === 'status') return f.state.hpPlanned && head === f.state.inputs.hpBaseCommit ? ' M hypha.html' : '';
    if (args[0] === 'diff') return args.includes('--cached') ? '' : HP_FILES.join('\n');
    if (args[0] === 'ls-files') return '';
    if (args[0] === 'commit') head = 'd'.repeat(40);
    if (args[0] === 'push') { assert.ok(!args.includes('--force')); remote = head; }
    return '';
  };
  const fetcher = async url => {
    if (!deployed) return new Response('not deployed', { status: 503 });
    return new Response(url.includes('deployment-source') ? head : fs.readFileSync(path.join(root,
      url.includes('/ja/') ? 'ja/hypha.html' : 'hypha.html'), 'utf8'));
  };
  await assert.rejects(publishHp(f.state, run, () => {}, fetcher), /tests failed/);
  assert.ok(f.state.hpPlanned); assert.equal(f.state.hpCommit, undefined);
  failTests = false;
  await publishHp(f.state, run, () => {}, fetcher);
  assert.equal(f.state.hpCommit, head);
  assert.equal((await publishHp(f.state, run, () => {}, fetcher)).reused, true);
  assert.equal(calls.filter(([tool, args]) => tool === 'git' && args[0] === 'commit').length, 1);
  assert.equal(calls.filter(([tool]) => tool === 'bash').length, 1);
  assert.equal(calls.filter(([tool, args]) => tool === 'git' && args[0] === 'push').length, 1);
});
