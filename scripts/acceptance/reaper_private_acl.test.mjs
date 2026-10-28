import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { loadPrivateReaperConfig } from './reaper_run.mjs';
import { initOperationLog, appendOperation } from './reaper_operation_log.mjs';

test('private config and operation log reject access-granting ACL without removing it', t => {
  if (process.platform !== 'darwin') return t.skip('macOS ACL test');
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-acl-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const file = path.join(root, 'config.json'), log = path.join(root, 'operations.json');
  fs.writeFileSync(file, '{}', { mode: 0o600 });
  const candidate = { id: 'candidate', commit: 'a'.repeat(40) }, binding = 'b'.repeat(64);
  initOperationLog(log, candidate, binding);
  for (const target of [file, log]) {
    const result = spawnSync('/bin/chmod', ['+a', 'everyone allow read', target], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(fs.statSync(target).mode & 0o777, 0o600);
    if (target === file) assert.throws(() => loadPrivateReaperConfig(file), /private|ACL/i);
    else assert.throws(() => appendOperation(log, candidate, binding, 'start'), /private|ACL/i);
    assert.match(spawnSync('/bin/ls', ['-lde', target], { encoding: 'utf8' }).stdout, /allow read/);
  }
});

test('inherited read ACL prevents writing a new operation log', t => {
  if (process.platform !== 'darwin') return t.skip('macOS ACL test');
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-acl-inherit-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const result = spawnSync('/bin/chmod', ['+a', 'everyone allow read,file_inherit', root], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  const file = path.join(root, 'operations.json');
  assert.throws(() => initOperationLog(file, { id: 'candidate', commit: 'a'.repeat(40) }, 'b'.repeat(64)), /private|ACL/i);
  assert.equal(fs.existsSync(file), false);
});
