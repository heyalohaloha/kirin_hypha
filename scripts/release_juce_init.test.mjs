import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { runReleaseCli } from './release_hypha.mjs';
import { digest } from './ls_release/hypha_release_contract.mjs';
import { hpReleaseNotice, makeHpRecord, installerFilename } from './ls_release/hypha_release_hp_data.mjs';

for (const patchState of ['unapplied', 'partial', 'applied', 'unknown']) {
  test(`init checks ${patchState} JUCE before creating a profile`, async t => {
    const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-juce-init-')));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    fs.mkdirSync(path.join(root, 'scripts'), { recursive: true });
    fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
    fs.writeFileSync(path.join(root, '.gitignore'), 'release_state/\n');
    fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), '[package]\nversion = "1.2.3"\n');
    const marker = path.join(root, 'juce-state'); fs.writeFileSync(marker, patchState);
    fs.writeFileSync(path.join(root, 'scripts/verify_juce_patch_state.sh'),
      `#!/bin/bash\n[[ "$(cat juce-state)" == applied ]] || { echo invalid-patch-state >&2; exit 1; }\n`);
    childProcess.execFileSync('git', ['init', '-q'], { cwd: root });
    childProcess.execFileSync('git', ['add', '.'], { cwd: root });
    childProcess.execFileSync('git', ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'B-1 fixture'], { cwd: root });
    const state = path.join(root, 'release_state/test.json');
    fs.mkdirSync(path.dirname(state), { recursive: true });
    const notes = path.join(root, 'release_state/notes.md'); fs.writeFileSync(notes, 'reviewed notes');
    const copy = path.join(root, 'release_state/publication.json');
    const facts = ['macos', 'windows'].map(target => ({ path: installerFilename('1.2.3', target), bytes: 1, sha256: 'a'.repeat(64) }));
    const notice = hpReleaseNotice(makeHpRecord('1.2.3', '2026-10-01', facts));
    fs.writeFileSync(copy, JSON.stringify({ schema: 'hypha-publication-v1', version: '1.2.3', date: '2026-10-01', notice }));
    const args = ['--state', state, '--init', '--hp-root', root, '--date', '2026-10-01', '--notes', notes, '--publication-input', copy];
    let audits = 0;
    const action = () => runReleaseCli(args, root, { auditRunner: async (tool, args) => { assert.equal(tool, 'npm'); assert.deepEqual(args, ['run', 'security:audit']); audits++; } });
    if (patchState === 'applied') {
      const profile = await action(); assert.ok(fs.existsSync(state));
      profile.freeze = { inputs: digest(profile.inputs), payloads: ['retained fixture payload'] };
      profile.freeze.sha256 = digest(profile.freeze); fs.writeFileSync(state, JSON.stringify(profile));
      const freezeBefore = JSON.stringify(profile.freeze), priorApproval = profile.publication.sha256;
      notice.en.summary += ' Known issue.'; fs.writeFileSync(copy, JSON.stringify({ schema: 'hypha-publication-v1', version: '1.2.3', date: '2026-10-01', notice }));
      fs.writeFileSync(notes, 'updated reviewed notes');
      const rebound = await runReleaseCli(['--state', state, '--bind-publication', copy], root);
      assert.equal(JSON.stringify(rebound.freeze), freezeBefore);
      assert.notEqual(rebound.publication.sha256, priorApproval);
    }
    else { await assert.rejects(action(), /invalid-patch-state/); assert.equal(fs.existsSync(state), false); }
    assert.equal(audits, patchState === 'applied' ? 1 : 0);
    if (patchState === 'applied') {
      const refused = path.join(root, 'release_state/refused.json');
      await assert.rejects(runReleaseCli(args.map(v => v === state ? refused : v), root, { auditRunner: async () => { throw new Error('fixture audit failure'); } }), /fixture audit failure/);
      assert.equal(fs.existsSync(refused), false);
    }
    assert.equal(fs.readFileSync(marker, 'utf8'), patchState);
    assert.equal(fs.existsSync(path.join(root, 'manufactured')), false);
  });
}
