import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { runReleaseCli } from './release_hypha.mjs';

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
    const action = () => runReleaseCli(['--state', state, '--init'], root);
    if (patchState === 'applied') { await action(); assert.ok(fs.existsSync(state)); }
    else { await assert.rejects(action(), /invalid-patch-state/); assert.equal(fs.existsSync(state), false); }
    assert.equal(fs.readFileSync(marker, 'utf8'), patchState);
    assert.equal(fs.existsSync(path.join(root, 'manufactured')), false);
  });
}
