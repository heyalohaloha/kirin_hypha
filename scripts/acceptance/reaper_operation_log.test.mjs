import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-operation-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return { file: path.join(root, 'operations.json'), candidate: { id: 'candidate', commit: 'a'.repeat(40) }, binding: 'b'.repeat(64) };
}
const steps = ['start', 'insert', 'record-start', 'record-stop', 'save', 'close', 'reopen', 'export', 'finish'];

test('operation log preserves ordered events and refuses incomplete runs', async t => {
  const f = fixture(t), { initOperationLog, appendOperation, readCompletedOperations } = await import('./reaper_operation_log.mjs');
  initOperationLog(f.file, f.candidate, f.binding);
  assert.throws(() => initOperationLog(f.file, f.candidate, f.binding));
  assert.throws(() => appendOperation(f.file, f.candidate, f.binding, 'finish'), /order/i);
  for (let i = 0; i < steps.length - 1; i++) appendOperation(f.file, f.candidate, f.binding, steps[i], () => i + 1);
  assert.throws(() => readCompletedOperations(f.file, f.candidate, f.binding), /incomplete/i);
  appendOperation(f.file, f.candidate, f.binding, 'finish', () => 10);
  assert.deepEqual(readCompletedOperations(f.file, f.candidate, f.binding).map(event => event.name), steps);
  assert.throws(() => appendOperation(f.file, f.candidate, f.binding, 'finish'), /order/i);
});

test('operation logs reject a different candidate, binding and regressing timestamps', async t => {
  const f = fixture(t), { initOperationLog, appendOperation, readCompletedOperations } = await import('./reaper_operation_log.mjs');
  initOperationLog(f.file, f.candidate, f.binding);
  appendOperation(f.file, f.candidate, f.binding, 'start', () => 10);
  assert.throws(() => appendOperation(f.file, f.candidate, f.binding, 'insert', () => 9), /time/i);
  assert.throws(() => appendOperation(f.file, f.candidate, 'c'.repeat(64), 'insert'), /binding/i);
  assert.throws(() => readCompletedOperations(f.file, { ...f.candidate, commit: 'd'.repeat(40) }, f.binding), /binding/i);
});

test('operator CLI initializes a private log and records only the next event', async t => {
  const { spawnSync } = await import('node:child_process');
  const f = fixture(t), config = path.join(path.dirname(f.file), 'private-config.json');
  fs.writeFileSync(config, JSON.stringify({ candidate: f.candidate, bindingSha256: f.binding, operationsLog: f.file }), { mode: 0o600 });
  const script = new URL('./reaper_run.mjs', import.meta.url).pathname;
  const run = args => spawnSync(process.execPath, [script, ...args], { encoding: 'utf8', env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' } });
  assert.equal(run(['--init-operations', config]).status, 0);
  assert.equal(run(['--event', config, 'start']).status, 0);
  assert.notEqual(run(['--event', config, 'finish']).status, 0);
  const log = JSON.parse(fs.readFileSync(f.file, 'utf8'));
  assert.deepEqual(log.events.map(event => event.name), ['start']);
});

test('guided operation recording stays incomplete on early EOF and completes only after all steps', async t => {
  const { spawnSync } = await import('node:child_process');
  const f = fixture(t), root = path.dirname(f.file), config = path.join(root, 'private-config.json');
  fs.writeFileSync(config, JSON.stringify({ candidate: f.candidate, bindingSha256: f.binding, operationsLog: f.file }), { mode: 0o600 });
  const script = new URL('./reaper_run.mjs', import.meta.url).pathname;
  const run = input => spawnSync(process.execPath, [script, '--record-operations', config], { encoding: 'utf8', input, env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' } });
  assert.notEqual(run('\n\n').status, 0);
  const partial = JSON.parse(fs.readFileSync(f.file, 'utf8')); assert.equal(partial.events.length, 2);
  const next = path.join(root, 'complete.json');
  fs.writeFileSync(config, JSON.stringify({ candidate: f.candidate, bindingSha256: f.binding, operationsLog: next }), { mode: 0o600 });
  assert.equal(run('\n'.repeat(9)).status, 0);
  const complete = JSON.parse(fs.readFileSync(next, 'utf8')); assert.deepEqual(complete.events.map(event => event.name), steps);
});

test('operation logs reject a symlinked path and non-private file modes', async t => {
  const f = fixture(t), { initOperationLog, appendOperation } = await import('./reaper_operation_log.mjs');
  initOperationLog(f.file, f.candidate, f.binding);
  const link = path.join(path.dirname(f.file), 'link.json'); fs.symlinkSync(f.file, link);
  assert.throws(() => appendOperation(link, f.candidate, f.binding, 'start'), /private/i);
  if (typeof process.getuid === 'function') {
    const original = fs.lstatSync;
    fs.lstatSync = (file, ...args) => {
      const stat = original(file, ...args);
      return file === f.file ? new Proxy(stat, { get(target, key) { return key === 'uid' ? process.getuid() + 1 : Reflect.get(target, key); } }) : stat;
    };
    try { assert.throws(() => appendOperation(f.file, f.candidate, f.binding, 'start'), /private/i); }
    finally { fs.lstatSync = original; }
  }
  fs.chmodSync(f.file, 0o644);
  assert.throws(() => appendOperation(f.file, f.candidate, f.binding, 'start'), /private/i);
});
