import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { inspectBundle } from './aax_swap_preflight.mjs';
import { runSession } from './aax_swap_session.mjs';
const posixTest = process.platform === 'win32' ? test.skip : test;


function setup(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'aax-session-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const manifest = ['PRE', 'POST'].map(role => ({ role,
    install_relative: `Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${role}.aaxplugin` }));
  const sources = manifest.map(({ role }) => {
    const source = path.join(root, `${role}-quote'$().aaxplugin`);
    fs.mkdirSync(source);
    fs.writeFileSync(path.join(source, 'payload'), `candidate-${role}`);
    return { role, source, sha256: inspectBundle(source).sha256 };
  });
  const installed = Object.fromEntries(manifest.map(({ role }) => {
    const directory = path.join(root, `installed-${role}`);
    fs.mkdirSync(directory);
    fs.writeFileSync(path.join(directory, 'payload'), `original-${role}`);
    return [role, directory];
  }));
  const inspectInstalled = async ({ role }) => inspectBundle(installed[role]);
  return { root, manifest, sources, inspectInstalled };
}

posixTest('one authorisation owns install and restore in the same fixture session', async t => {
  const f = setup(t);
  let prompts = 0, stage = 'original', interacted = false;
  const result = await runSession({ ...f,
    authorize: async request => {
      prompts += 1;
      assert.equal(request.candidates.length, 2);
      assert.ok(request.candidates.every(candidate => !('source' in candidate)));
      assert.ok(request.candidates.every(candidate => /^[a-f0-9]{64}$/.test(candidate.originalHash)));
      assert.ok(request.candidates.every(candidate => /^[a-f0-9]{64}$/.test(candidate.originalMetadataSha256)));
      assert.ok(request.candidates.every(candidate => candidate.metadataSha256 === candidate.snapshot.metadataSha256));
      return {
        install: async () => { assert.equal(stage, 'original'); stage = 'candidate'; },
        restore: async () => { assert.equal(stage, 'candidate'); stage = 'original'; },
      };
    },
    interact: async () => { assert.equal(stage, 'candidate'); interacted = true; },
  });
  assert.equal(prompts, 1);
  assert.equal(stage, 'original');
  assert.equal(interacted, true);
  assert.equal(result.status, 'restored');
});

posixTest('mismatched hash or linked source is refused before requesting authorisation', async t => {
  const f = setup(t);
  let prompts = 0;
  const authorize = async () => { prompts += 1; throw new Error('must not request'); };
  await assert.rejects(runSession({ ...f, sources: [{ ...f.sources[0], sha256: '0'.repeat(64) }, f.sources[1]], authorize, interact: async () => {} }), /hash mismatch/);
  const link = path.join(f.root, 'link');
  fs.symlinkSync(f.sources[0].source, link);
  await assert.rejects(runSession({ ...f, sources: [{ ...f.sources[0], source: link }, f.sources[1]], authorize, interact: async () => {} }), /symlink/);
  assert.equal(prompts, 0);
});

posixTest('operator failure still asks the same worker to restore', async t => {
  const f = setup(t);
  let prompts = 0, restored = 0;
  const authorize = async () => { prompts += 1; return { install: async () => {}, restore: async () => { restored += 1; } }; };
  await assert.rejects(runSession({ ...f, authorize, interact: async () => { throw new Error('operator stopped'); } }), /operator stopped/);
  assert.equal(prompts, 1);
  assert.equal(restored, 1);
});

posixTest('unsupported role is refused before requesting authorisation', async t => {
  const f = setup(t);
  let prompts = 0;
  await assert.rejects(runSession({ ...f,
    sources: [f.sources[0], { ...f.sources[1], role: 'OTHER' }],
    authorize: async () => { prompts += 1; }, interact: async () => {},
  }), /role/);
  assert.equal(prompts, 0);
});

posixTest('operator and restore failures are both retained for recovery', async t => {
  const f = setup(t);
  await assert.rejects(runSession({ ...f,
    authorize: async () => ({ install: async () => {}, restore: async () => { throw new Error('restore failed'); } }),
    interact: async () => { throw new Error('operator failed'); },
  }), error => error instanceof AggregateError && error.errors.map(item => item.message).join(',') === 'operator failed,restore failed');
});


posixTest('installed bundle inspection fails before requesting authorisation', async t => {
  const f = setup(t);
  let prompts = 0;
  await assert.rejects(runSession({ ...f,
    inspectInstalled: async () => { throw new Error('installed link refused'); },
    authorize: async () => { prompts += 1; }, interact: async () => {},
  }), /installed link refused/);
  assert.equal(prompts, 0);
});

posixTest('falsy thrown values are not mistaken for successful operation', async t => {
  const f = setup(t);
  let caught = false, restored = 0;
  try {
    await runSession({ ...f,
      authorize: async () => ({ install: async () => {}, restore: async () => { restored += 1; } }),
      interact: async () => { throw null; },
    });
  } catch (error) {
    caught = true;
    assert.equal(error, null);
  }
  assert.equal(restored, 1);
  assert.equal(caught, true);
});
