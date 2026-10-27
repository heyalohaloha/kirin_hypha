import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import * as release from '../release_hypha.mjs';
import { hpReleaseNotice, makeHpRecord, installerFilename } from './hypha_release_hp_data.mjs';

test('public copy and notes have their own approval digest; altered copy needs a new approval', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-public-input-'));
  try {
    const version = '1.2.3', date = '2026-10-01';
    const facts = ['macos', 'windows'].map((target, index) => ({ path: installerFilename(version, target), bytes: 10, sha256: String(index + 1).repeat(64) }));
    const notice = hpReleaseNotice(makeHpRecord(version, date, facts));
    notice.en.summary += ' Known issue.'; notice.ja.summary += ' 既知の点。';
    const input = path.join(root, 'publication.json'), notes = path.join(root, 'notes.md');
    fs.writeFileSync(notes, 'reviewed release notes');
    fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version, date, notice }));
    const state = { root, candidate: { id: 'candidate', version }, inputs: { date, notes } };
    const binding = release.publicationBinding(state, input);
    state.publication = binding;
    release.authorizePublication(state, { publishApproved: 'candidate', publicationApproved: binding.sha256 });
    notice.en.summary += ' changed'; fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version, date, notice }));
    assert.throws(() => release.authorizePublication(state, { publishApproved: 'candidate', publicationApproved: binding.sha256 }), /changed|approval/i);
    const next = release.publicationBinding(state, input);
    assert.notEqual(next.sha256, binding.sha256);
    assert.throws(() => release.authorizePublication({ ...state, publication: next }, { publishApproved: 'candidate', publicationApproved: binding.sha256 }), /approval/i);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
});

test('failed HP dependency audit stops init before profile and producers', async () => {
  let manufactured = 0;
  await assert.rejects(release.auditBeforeInit({ hpRoot: '/fixture' }, async (tool, args) => {
    assert.equal(tool, 'npm'); assert.deepEqual(args, ['run', 'security:audit']); throw new Error('audit failed');
  }, () => { manufactured++; }), /audit failed/);
  assert.equal(manufactured, 0);
});

test('binding corrected wording preserves the manufacturing freeze and revokes the old approval', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-rebind-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'release_state'));
  const notes = path.join(root, 'notes.md'), input = path.join(root, 'copy.json'), profile = path.join(root, 'release_state/profile.json');
  fs.writeFileSync(notes, 'first');
  const facts = ['macos', 'windows'].map(target => ({ path: installerFilename('1.2.3', target), bytes: 1, sha256: 'a'.repeat(64) }));
  const notice = hpReleaseNotice(makeHpRecord('1.2.3', '2026-10-01', facts));
  const write = () => fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version: '1.2.3', date: '2026-10-01', notice }));
  write();
  const state = { root, candidate: { id: 'candidate', version: '1.2.3' }, inputs: { date: '2026-10-01', notes }, stages: {}, freeze: { sha256: 'manufactured' } };
  state.publication = release.publicationBinding(state, input); const old = state.publication.sha256;
  fs.writeFileSync(profile, JSON.stringify(state));
  notice.ja.summary += ' 更新'; write(); fs.writeFileSync(notes, 'updated');
  const result = await release.bindPublication(profile, input, { validate: () => {} });
  assert.equal(result.freeze.sha256, 'manufactured'); assert.notEqual(result.publication.sha256, old);
  assert.throws(() => release.authorizePublication(result, { publishApproved: 'candidate', publicationApproved: old }), /approval/i);
  release.authorizePublication(result, { publishApproved: 'candidate', publicationApproved: result.publication.sha256 });
  result.publicationStarted = true; result.stages.github = { status: 'PENDING' };
  fs.writeFileSync(profile, JSON.stringify(result));
  const afterLs = await release.bindPublication(profile, input, { validate: () => {} });
  afterLs.githubRelease = { status: 'requested' }; fs.writeFileSync(profile, JSON.stringify(afterLs));
  await assert.rejects(release.bindPublication(profile, input, { validate: () => {} }), /publication.*started/i);
});
