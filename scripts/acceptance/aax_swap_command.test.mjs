import test from 'node:test';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { buildAuthorization, verifyAuthorization } from './aax_swap_command.mjs';
import { shellTreeHash } from './aax_swap_shell_plan.mjs';
const hash = value => createHash('sha256').update(value).digest('hex');
const snapshot = { entries: [{ path: '', kind: 'directory', mode: 0o700 }], files: [], attributes: [{ path: '', values: [] }] };
snapshot.sha256 = hash(JSON.stringify(snapshot.entries)); snapshot.metadataSha256 = hash(JSON.stringify(snapshot.attributes));
const identity = { uid: 501, parentPid: 1234, token: 'a'.repeat(32) };
function request() { return { candidates: ['PRE', 'POST'].map(role => ({ role,
  target: `/Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${role}.aaxplugin`,
  sha256: snapshot.sha256, metadataSha256: snapshot.metadataSha256, treeSha256: shellTreeHash(snapshot),
  originalHash: 'a'.repeat(64), originalMetadataSha256: 'b'.repeat(64), originalTreeSha256: 'c'.repeat(64), snapshot })) }; }

test('fixed system script uses env-i, a fixed PATH and one standard authorization', () => {
  const command = buildAuthorization(request(), identity);
  assert.equal(command.args.join('\n').match(/with administrator privileges/g)?.length, 1);
  assert.match(command.shellScript, /^\/usr\/bin\/env -i PATH=\/usr\/bin:\/bin:\/usr\/sbin:\/sbin /);
  assert.doesNotMatch(command.shellScript, /\b(?:python\d*|perl|node)\b/i);
  assert.ok(!command.shellScript.includes(command.payload));
  assert.doesNotMatch(command.recoveryDirectory, /\/tmp|\/var\/folders/);
  assert.equal(verifyAuthorization(command), true);
});

test('only session identity changes the wrapper; altered text or payload fails before auth', () => {
  const first = buildAuthorization(request(), identity), second = buildAuthorization(request(), { ...identity, token: 'b'.repeat(32) });
  assert.equal(first.rootScript, second.rootScript); assert.equal(first.scriptSha256, second.scriptSha256);
  assert.notEqual(first.commandSha256, second.commandSha256);
  assert.throws(() => verifyAuthorization({ ...first, rootScript: first.rootScript + '\n:' }), /changed/);
  assert.throws(() => verifyAuthorization({ ...first, payload: first.payload + 'x' }), /changed/);
  assert.throws(() => buildAuthorization(request(), { ...identity, token: "' $(bad)\n" }), /token/);
  const outside = request(); outside.candidates[0].target = '/Library/Other.aaxplugin';
  assert.throws(() => buildAuthorization(outside, identity), /target/);
  const extra = request(); extra.candidates[0].source = '/untrusted';
  assert.throws(() => buildAuthorization(extra, identity), /fields/);
});

test('system shell syntax is checked without executing or authenticating', { skip: process.platform === 'win32' }, () => {
  const command = buildAuthorization(request(), identity);
  const result = spawnSync('/bin/sh', ['-n'], { input: command.shellScript, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
});

test('shell command compiles as an AppleScript literal without executing', { skip: process.platform !== 'darwin' }, t => {
  const command = buildAuthorization(request(), identity), root = fs.mkdtempSync(path.join(os.tmpdir(), 'aax-command-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const result = spawnSync('/usr/bin/osacompile', ['-o', path.join(root, 'inspect.scpt')], {
    input: `return ${command.args[1].match(/do shell script (".*") with administrator privileges/)[1]}`, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
});
