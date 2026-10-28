// GitHub Release resume fixtures: a mocked gh only, no network and no real release.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { publishGithub } from './hypha_release_hp.mjs';
import { fileFact, hash, Checkpoint } from './hypha_release_contract.mjs';

const REPO = 'heyalohaloha/kirin_hypha';
const RELEASES = `repos/${REPO}/releases`;
const UPLOADS = `https://uploads.github.com/${RELEASES}`;
const COMMIT = 'a'.repeat(40);
const OTHER = 'b'.repeat(40);
const TAG = 'v1.2.3';
const NAME = 'Kirin Hypha 1.2.3';
const NOTES = 'Reviewed release notes\n';

const method = args => (args.includes('--method') ? args[args.indexOf('--method') + 1] : 'GET');
const KIND = {
  create: ({ tool, args }) => tool === 'gh'
    && (args[0] === 'release' ? args[1] === 'create' : args[1] === RELEASES && method(args) === 'POST'),
  upload: ({ tool, args }) => tool === 'gh'
    && (args[0] === 'release' ? args[1] === 'upload' : String(args[1]).startsWith('https://uploads.github.com/')),
  publish: ({ tool, args }) => tool === 'gh'
    && (args[0] === 'release' ? args[1] === 'edit' : method(args) === 'PATCH'),
  readback: ({ tool, args, options }) => (tool === 'gh' && !!options?.stdoutFile) || (args[0] === 'release' && args[1] === 'download'),
  tagRelease: ({ args }) => args.some(a => String(a).includes('/releases/tags/')),
};

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-github-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.writeFileSync(path.join(root, 'AGENTS.md'), '# GitHub budget policy\n');
  fs.writeFileSync(path.join(root, 'reviewed-notes.md'), NOTES);
  const payload = path.join(root, 'payload.pkg'); fs.writeFileSync(payload, 'signed fixture bytes');
  const source = path.join(root, 'source.zip'); fs.writeFileSync(source, 'source delivery fixture bytes');
  const state = { root, candidate: { id: 'candidate', version: '1.2.3', commit: COMMIT },
    inputs: { notes: 'reviewed-notes.md', date: '2026-10-01' },
    stages: { packages: { facts: [fileFact(payload)] },
      provenance: { status: 'PASS', facts: [fileFact(source)], publicFacts: [fileFact(source)] } } };
  const saves = [];
  return { root, state, saves, facts: [fileFact(payload), fileFact(source)],
    save: () => saves.push(structuredClone(state.githubRelease ?? null)) };
}

const intent = (extra = {}) => ({ tag: TAG, commit: COMMIT, name: NAME, status: 'requested',
  requestedAt: '2026-10-18T00:00:00.000Z', ...extra });

function release(id, { tag = TAG, commit = COMMIT, name = NAME, draft = true, assets = [] } = {}) {
  return { id, tag_name: tag, target_commitish: commit, name, draft, prerelease: false, assets,
    html_url: `https://github.com/${REPO}/releases/tag/${draft ? `untagged-${id}` : tag}` };
}

function asset(id, fact, bytes = fs.readFileSync(fact.path)) {
  return { id, name: path.basename(fact.path), size: bytes.length, digest: `sha256:${hash(bytes)}`,
    state: 'uploaded', bytes };
}

function fields(args) {
  const out = {};
  for (let i = 1; i < args.length; i++) {
    if (args[i] !== '-f' && args[i] !== '-F') continue;
    const [key, ...rest] = args[i + 1].split('='); let value = rest.join('=');
    if (args[i] === '-F') {
      value = value === 'true' ? true : value === 'false' ? false
        : value.startsWith('@') ? fs.readFileSync(value.slice(1), 'utf8') : value;
    }
    out[key] = value;
  }
  return out;
}

// GitHub semantics that matter here: a draft has no tag ref, so the tag endpoints 404 until publish.
function github(f, { releases = [], fail = () => null, corrupt = null } = {}) {
  const mock = { releases, calls: [], fail, intentAtCreate: undefined };
  let nextId = 500; let nextAsset = 900;
  const find = id => releases.find(r => r.id === id);
  const json = value => JSON.stringify(value, (key, v) => (key === 'bytes' ? undefined : v));
  const missing = options => { if (options.optional404) return ''; throw new Error('gh failed; HTTP 404'); };
  const addDraft = (tag, commit, name) => { const r = release(nextId++, { tag, commit, name }); releases.push(r); return r; };
  mock.count = kind => mock.calls.filter(KIND[kind]).length;
  mock.run = async (tool, args, options = {}) => {
    mock.calls.push({ tool, args: [...args], options });
    assert.equal(tool, 'gh');
    if (options.stdoutFile) {
      assert.deepEqual(args.slice(0, 1).concat(args.slice(2)), ['api', '-H', 'Accept: application/octet-stream']);
      const id = Number(new RegExp(`^${RELEASES}/assets/(\\d+)$`).exec(args[1])?.[1]);
      const found = releases.flatMap(r => r.assets).find(a => a.id === id);
      if (!found) throw new Error('gh failed; HTTP 404');
      fs.writeFileSync(options.stdoutFile, corrupt ? corrupt(found.bytes) : found.bytes, { flag: 'wx' });
      return '';
    }
    if (args[0] === 'release' && args[1] === 'create') { // legacy create: counted, reachable only by tag
      addDraft(args[2], args[args.indexOf('--target') + 1], args[args.indexOf('--title') + 1]); return '';
    }
    assert.equal(args[0], 'api', `tag-addressed gh ${args.slice(0, 2).join(' ')} is outside this fixture`);
    const endpoint = args[1]; const verb = method(args);
    const failure = mock.fail(endpoint, verb); if (failure) throw new Error(`gh failed; ${failure}`);
    assert.notEqual(verb, 'DELETE');
    let match;
    if ((match = new RegExp(`^repos/${REPO}/git/ref/tags/(.+)$`).exec(endpoint))) {
      const published = releases.find(r => !r.draft && r.tag_name === match[1]);
      return published ? json({ object: { type: 'commit', sha: published.target_commitish } }) : missing(options);
    }
    if (endpoint === `repos/${REPO}/commits/${COMMIT}`) return json({ sha: COMMIT });
    if ((match = new RegExp(`^${RELEASES}/tags/(.+)$`).exec(endpoint))) {
      const published = releases.find(r => !r.draft && r.tag_name === match[1]);
      return published ? json(published) : missing(options);
    }
    if (endpoint.startsWith(`${RELEASES}?`)) {
      const query = new URL(endpoint, 'https://api.github.com/').searchParams;
      const size = Number(query.get('per_page')); const page = Number(query.get('page'));
      assert.equal(size, 100); assert.ok(page >= 1);
      return json([...releases].sort((a, b) => b.id - a.id).slice((page - 1) * size, page * size));
    }
    if ((match = new RegExp(`^${RELEASES}/(\\d+)$`).exec(endpoint))) {
      const found = find(Number(match[1]));
      if (!found) return missing(options);
      if (verb === 'PATCH') {
        assert.deepEqual(args.slice(2), ['--method', 'PATCH', '-F', 'draft=false']);
        found.draft = false; found.html_url = `https://github.com/${REPO}/releases/tag/${found.tag_name}`;
      } else assert.equal(verb, 'GET');
      return json(found);
    }
    if (endpoint === RELEASES && verb === 'POST') {
      mock.intentAtCreate = structuredClone(f.state.githubRelease);
      const body = fields(args);
      assert.equal(body.draft, true); assert.equal(body.prerelease, false); assert.equal(body.body, NOTES);
      const draft = addDraft(body.tag_name, body.target_commitish, body.name); draft.body = body.body; return json(draft);
    }
    if ((match = new RegExp(`^${UPLOADS}/(\\d+)/assets\\?name=(.+)$`).exec(endpoint))) {
      assert.equal(verb, 'POST'); assert.ok(args.includes('Content-Type: application/octet-stream'));
      const target = find(Number(match[1])); const name = decodeURIComponent(match[2]);
      assert.ok(target?.draft, 'uploads only into an existing draft');
      if (target.assets.some(a => a.name === name)) throw new Error('gh failed; HTTP 422 already_exists');
      const bytes = fs.readFileSync(args[args.indexOf('--input') + 1]);
      const added = { id: nextAsset++, name, size: bytes.length, digest: `sha256:${hash(bytes)}`, state: 'uploaded', bytes };
      target.assets.push(added); return json(added);
    }
    return assert.fail(`unexpected GitHub call ${args.join(' ')}`);
  };
  return mock;
}

const noMutation = (m, label) => {
  assert.equal(m.count('create'), 0, label); assert.equal(m.count('upload'), 0, label);
  assert.equal(m.count('publish'), 0, label);
};
const notCheckpoint = pattern => error => !(error instanceof Checkpoint) && pattern.test(error.message);

test('1: created draft is resolved by its ID although a draft tag lookup would 404', async t => {
  const f = fixture(t); const m = github(f);
  const result = await publishGithub(f.state, m.run, f.save);
  assert.equal(m.count('create'), 1); assert.equal(m.count('upload'), 2); assert.equal(m.count('publish'), 1);
  assert.equal(m.count('tagRelease'), 0);
  assert.equal(m.intentAtCreate.status, 'requested'); assert.equal(m.intentAtCreate.id, undefined);
  assert.equal(m.intentAtCreate.tag, TAG); assert.equal(m.intentAtCreate.commit, COMMIT);
  assert.deepEqual(result, { url: `https://github.com/${REPO}/releases/tag/${TAG}`, releaseId: 500 });
  assert.equal(f.state.githubRelease.id, 500); assert.equal(f.state.githubRelease.status, 'published');
  assert.ok(f.saves.some(s => s?.id === 500 && s.status === 'created'));
  const kinds = m.calls.map(c => (KIND.readback(c) ? 'readback' : KIND.publish(c) ? 'publish' : ''));
  assert.equal(kinds.filter(k => k === 'readback').length, 2);
  assert.ok(kinds.lastIndexOf('readback') < kinds.indexOf('publish'));
  assert.equal(m.calls.find(KIND.publish).args[1], `${RELEASES}/500`);
  assert.ok(m.calls.every(c => c.tool === 'gh' && c.args[0] === 'api'));
  assert.ok(!m.calls.some(c => c.args.includes('--clobber')));
  assert.equal(m.releases[0].draft, false); assert.equal(m.releases[0].target_commitish, COMMIT);
});

test('2: a run interrupted after create resumes with the saved ID and never creates again', async t => {
  const f = fixture(t); const m = github(f, { fail: e => (e.startsWith(UPLOADS) ? 'HTTP 502' : null) });
  await assert.rejects(publishGithub(f.state, m.run, f.save), /502/);
  assert.equal(f.state.githubRelease.id, 500); assert.equal(f.state.githubRelease.status, 'created');
  assert.equal(m.count('create'), 1); assert.equal(m.count('publish'), 0);
  m.fail = () => null;
  const result = await publishGithub(f.state, m.run, f.save);
  assert.equal(m.count('create'), 1, 'resume adds no create'); assert.equal(m.count('upload'), 3);
  assert.equal(result.releaseId, 500); assert.equal(m.releases.length, 1);
  assert.equal(m.releases[0].assets.length, 2); assert.equal(m.releases[0].draft, false);
});

test('3: create intent without an ID adopts the one matching draft from the paginated list', async t => {
  const f = fixture(t);
  const others = Array.from({ length: 150 }, (_, i) => release(1000 + i, { tag: `v0.${i}.0`, commit: OTHER, draft: false }));
  const m = github(f, { releases: [...others, release(9)] });
  f.state.githubRelease = intent();
  const result = await publishGithub(f.state, m.run, f.save);
  assert.equal(m.count('create'), 0); assert.equal(m.count('upload'), 2); assert.equal(m.count('tagRelease'), 0);
  assert.equal(result.releaseId, 9); assert.equal(f.state.githubRelease.id, 9);
  assert.ok(m.calls.some(c => c.args[1] === `${RELEASES}?per_page=100&page=2`));
  const adopted = f.saves.findIndex(s => s?.id === 9);
  assert.ok(adopted >= 0 && f.saves[adopted].status === 'created');
  assert.equal(m.releases.find(r => r.id === 9).draft, false);
});

test('4: create intent without an ID and no visible draft is an operator checkpoint, never a re-create', async t => {
  const f = fixture(t); const m = github(f, { releases: [release(3, { tag: 'v1.2.2', commit: OTHER, draft: false })] });
  f.state.githubRelease = intent();
  for (let attempt = 0; attempt < 2; attempt++) {
    await assert.rejects(publishGithub(f.state, m.run, f.save), Checkpoint);
  }
  noMutation(m);
  assert.equal(f.state.githubRelease.status, 'requested'); assert.equal(f.state.githubRelease.id, undefined);
});

test('5: a same-tag release targeting another commit is foreign, with or without intent', async t => {
  for (const [label, releases, record] of [
    ['foreign draft', [release(4, { commit: OTHER })]],
    ['foreign draft with intent', [release(4, { commit: OTHER })], intent()],
    ['foreign draft beside ours', [release(4, { commit: OTHER }), release(5)], intent()],
    ['foreign published release', [release(4, { commit: OTHER, draft: false })]],
  ]) {
    const f = fixture(t); const m = github(f, { releases });
    if (record) f.state.githubRelease = record;
    await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(/foreign|frozen exact commit/i), label);
    noMutation(m, label);
  }
});

test('6: a saved ID whose release is not this tag/commit/state is rejected', async t => {
  const saved = intent({ status: 'created', id: 7 });
  for (const [label, releases, record, pattern] of [
    ['other tag', [release(7, { tag: 'v1.2.4' })], saved, /does not match/],
    ['other commit', [release(7, { commit: OTHER })], saved, /does not match/],
    ['abbreviated commit', [release(7, { commit: COMMIT.slice(0, 7) })], saved, /does not match/],
    ['deleted release', [], saved, /404/],
    ['record of another candidate', [release(7)], intent({ status: 'created', id: 7, commit: OTHER }), /another tag/],
    ['returned to draft after publish', [release(7)], intent({ status: 'published', id: 7 }), /returned to draft/],
  ]) {
    const f = fixture(t); const m = github(f, { releases }); f.state.githubRelease = structuredClone(record);
    await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(pattern), label);
    noMutation(m, label);
  }
});

test('7: an existing same-name asset with other bytes is never overwritten', async t => {
  for (const [label, extra] of [['size', 'signed fixture bytes plus'], ['digest', 'signed fixture bytez'], ['unexpected asset']]) {
    const f = fixture(t);
    const assets = extra ? [asset(70, f.facts[0], Buffer.from(extra))]
      : [...f.facts.map((fact, i) => asset(70 + i, fact)), { ...asset(79, f.facts[0]), name: 'other.bin' }];
    const m = github(f, { releases: [release(7, { assets })] });
    f.state.githubRelease = intent({ status: 'created', id: 7 });
    await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(/asset/), label);
    noMutation(m, label);
    assert.equal(m.releases[0].assets.length, assets.length, label); assert.equal(m.releases[0].draft, true, label);
  }
});

test('7b: a foreign asset stops before any upload, and a published release with an extra asset is not reused', async t => {
  for (const [label, draft, assets] of [['draft with only a foreign asset', true, f => [{ ...asset(79, f.facts[0]), name: 'notes.txt' }]],
    ['published with an extra asset', false, f => [...f.facts.map((fact, i) => asset(70 + i, fact)), { ...asset(79, f.facts[0]), name: 'extra.bin' }]]]) {
    for (const record of [undefined, intent({ status: 'created', id: 7 })]) {
      const f = fixture(t);
      const m = github(f, { releases: [release(7, { draft, assets: assets(f) })] });
      if (record) f.state.githubRelease = record;
      await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(/outside the frozen distribution set/), label);
      noMutation(m, label);
    }
  }
});

test('7c: without a saved record only the expected release name is adopted', async t => {
  const f = fixture(t);
  const m = github(f, { releases: [release(7, { name: 'Hand-made draft', assets: f.facts.map((fact, i) => asset(70 + i, fact)) })] });
  await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(/not the requested release/));
  noMutation(m, 'foreign name');
});

test('8: an already published release with identical bytes is reused without any mutation', async t => {
  for (const [label, record] of [['saved ID after a lost publish response', intent({ status: 'publishing', id: 7 })],
    ['found by the authenticated list']]) {
    const f = fixture(t);
    const m = github(f, { releases: [release(7, { draft: false, assets: f.facts.map((fact, i) => asset(70 + i, fact)) })] });
    if (record) f.state.githubRelease = record;
    const result = await publishGithub(f.state, m.run, f.save);
    noMutation(m, label);
    assert.equal(result.releaseId, 7, label); assert.equal(f.state.githubRelease.status, 'published', label);
    assert.deepEqual(m.calls.filter(KIND.readback).map(c => c.args[1]), [`${RELEASES}/assets/70`, `${RELEASES}/assets/71`], label);
  }
});

test('9: API failures and an unbounded listing throw and never fall back to create', async t => {
  const list = status => e => (e.startsWith(`${RELEASES}?`) ? status : null);
  const many = Array.from({ length: 1000 }, (_, i) => release(2000 + i, { tag: `v0.${i}.0`, commit: OTHER, draft: false }));
  for (const [label, record, fail, pattern, releases = []] of [
    ['list 403 without record', undefined, list('HTTP 403'), /403/],
    ['ID GET 500', intent({ status: 'created', id: 7 }), e => (e === `${RELEASES}/7` ? 'HTTP 500' : null), /500/, [release(7)]],
    ['list 500 with intent', intent(), list('HTTP 500'), /500/],
    ['listing beyond the bounded scan', undefined, () => null, /bounded/, many],
  ]) {
    const f = fixture(t); const m = github(f, { releases, fail });
    if (record) f.state.githubRelease = record;
    await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(pattern), label);
    noMutation(m, label);
  }
});

test('10: read-back bytes that differ from the frozen facts stop before the publish PATCH', async t => {
  const f = fixture(t);
  const flip = bytes => { const copy = Buffer.from(bytes); copy[0] ^= 1; return copy; };
  const m = github(f, { releases: [release(7, { assets: f.facts.map((fact, i) => asset(70 + i, fact)) })], corrupt: flip });
  f.state.githubRelease = intent({ status: 'created', id: 7 });
  await assert.rejects(publishGithub(f.state, m.run, f.save), /bytes/);
  noMutation(m); assert.equal(m.count('readback'), 1); assert.equal(m.releases[0].draft, true);
});

test('11: more than one matching same-tag release is ambiguous, with or without intent', async t => {
  for (const record of [undefined, intent()]) {
    const f = fixture(t); const m = github(f, { releases: [release(5), release(6)] });
    if (record) f.state.githubRelease = record;
    await assert.rejects(publishGithub(f.state, m.run, f.save), notCheckpoint(/ambiguous/i));
    noMutation(m);
  }
});

test('read-only reconciliation verifies public identity, notes, tag and every retained byte', async t => {
  const { verifyPublishedGithub } = await import('./hypha_release_github.mjs');
  const f = fixture(t), published = release(701, { draft: false, assets: f.facts.map((fact, i) => asset(800 + i, fact)) });
  published.body = NOTES; f.state.githubRelease = intent({ id: 701 });
  const mock = github(f, { releases: [published] });
  const result = await verifyPublishedGithub(f.state, mock.run);
  assert.equal(result.evidence.releaseId, 701); assert.deepEqual(result.facts, f.facts); noMutation(mock);
  published.body = 'different'; await assert.rejects(verifyPublishedGithub(f.state, mock.run), /metadata/i);
  published.body = NOTES; published.draft = true;
  await assert.rejects(verifyPublishedGithub(f.state, mock.run), /metadata/i);
  noMutation(mock);
});

test('an existing draft with different reviewed notes is refused before asset upload or publish', async t => {
  const { publicationBinding } = await import('./publication_input.mjs');
  const { makeHpRecord, hpReleaseNotice, installerFilename } = await import('./hypha_release_hp_data.mjs');
  const f = fixture(t);
  const packageFacts = ['macos', 'windows'].map(target => ({ path: installerFilename('1.2.3', target), bytes: 1, sha256: 'a'.repeat(64) }));
  const input = path.join(f.root, 'public.json');
  fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version: '1.2.3', date: '2026-10-01',
    notice: hpReleaseNotice(makeHpRecord('1.2.3', '2026-10-01', packageFacts)) }));
  f.state.publication = publicationBinding(f.state, input);
  const draft = { ...release(88), body: 'old notes' };
  const mock = github(f, { releases: [draft] });
  await assert.rejects(publishGithub(f.state, mock.run, f.save), /notes/i);
  assert.equal(mock.count('upload'), 0); assert.equal(mock.count('publish'), 0);
});


test('reviewed notes remain bound through draft creation and publication', async t => {
  const { publicationBinding } = await import('./publication_input.mjs');
  const { makeHpRecord, hpReleaseNotice, installerFilename } = await import('./hypha_release_hp_data.mjs');
  const f = fixture(t), packageFacts = ['macos', 'windows'].map(target => ({ path: installerFilename('1.2.3', target), bytes: 1, sha256: 'a'.repeat(64) }));
  const input = path.join(f.root, 'public.json');
  fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version: '1.2.3', date: '2026-10-01', notice: hpReleaseNotice(makeHpRecord('1.2.3', '2026-10-01', packageFacts)) }));
  f.state.publication = publicationBinding(f.state, input);
  const mock = github(f); await publishGithub(f.state, mock.run, f.save);
  assert.equal(mock.releases[0].body, NOTES); assert.equal(mock.count('publish'), 1);
});
