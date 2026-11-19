import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawn } from 'node:child_process';

// One deadline covers the entire contract. CTest's 60 seconds leaves cleanup headroom.
export const contractTimeoutMs = 45_000;
const exited = child => child.exitCode !== null || child.signalCode !== null;
const ownedChildren = new WeakMap();
export function waitChild(child, event) {
  if (exited(child)) return event === 'exit' ? Promise.resolve([child.exitCode, child.signalCode])
    : Promise.reject(Error('fixture child exited before ' + event));
  return new Promise((resolve, reject) => {
    const finish = (error, value) => {
      child.off('message', message); child.off('exit', exit); child.off('error', fail);
      if (error) reject(error); else resolve(value);
    };
    const message = value => { if (event === 'message') finish(null, [value]); };
    const exit = (code, signal) => event === 'exit' ? finish(null, [code, signal])
      : finish(Error('fixture child exited before ' + event));
    const fail = error => finish(error);
    child.on('message', message); child.on('exit', exit); child.on('error', fail);
  });
}
export async function cleanupChild(child) {
  if (!child || !child.pid || exited(child)) return;
  const done = waitChild(child, 'exit'); child.kill('SIGKILL'); await done;
}
export function spawnChild(t, args, options) {
  if (t.signal.aborted) throw t.signal.reason;
  const child = spawn(process.execPath, args, options);
  if (!ownedChildren.has(t)) ownedChildren.set(t, new Set());
  ownedChildren.get(t).add(child);
  // The test runner aborts this signal when the enclosing contract is cancelled.
  // Kill only this fixture's owned child, so a silent IPC peer cannot keep Node alive.
  const cancel = () => { if (!exited(child)) child.kill('SIGKILL'); };
  t.signal.addEventListener('abort', cancel, { once: true });
  t.after(async () => {
    t.signal.removeEventListener('abort', cancel);
    await cleanupChild(child);
  });
  return child;
}
export async function stopSocket(child) {
  const [message, exit] = await Promise.all([waitChild(child, 'message'), waitChild(child, 'exit'),
    new Promise((resolve, reject) => child.send('stop', error => error ? reject(error) : resolve()))]);
  assert.deepEqual(message[0], { connections: 0 });
  assert.equal(exit[0], 0);
}
export function fixture(t) {
  // Always below the working build/checkout, never in the real account home.
  const parent = path.join(process.cwd(), 'target', 'native-storage-guard-fixtures');
  fs.mkdirSync(parent, { recursive: true });
  const root = fs.mkdtempSync(path.join(parent, 'case-'));
  t.after(async () => {
    await Promise.all([...(ownedChildren.get(t) || [])].map(cleanupChild));
    fs.rmSync(root, { recursive: true, force: true });
  });
  const storageRoot = path.join(root, 'protected');
  const protectedRoot = path.join(storageRoot, 'plugin_data', 'fixture-project');
  fs.mkdirSync(protectedRoot, { recursive: true });
  return { root, protectedRoot, storageRoot, roots: [storageRoot], state: path.join(root, 'baseline.json') };
}

// Exercise the real guard on bounded, non-empty owned source for storage fixtures.
// The contract separately checks the real checkout before/after and tests source changes.
export async function isolatedStorageGuard(t) {
  const f = fixture(t);
  const checkout = path.join(f.root, 'checkout');
  const scripts = path.join(checkout, 'scripts');
  const source = path.join(checkout, 'src');
  fs.mkdirSync(scripts, { recursive: true });
  fs.mkdirSync(source);
  const sentinel = 'pub fn harmless_fixture() {}\n';
  fs.writeFileSync(path.join(source, 'harmless.rs'), sentinel);
  for (const name of ['native_storage_guard.mjs', 'native_storage_exclusions.mjs']) {
    const original = new URL(name, import.meta.url);
    const copy = path.join(scripts, name);
    fs.copyFileSync(original, copy);
    assert.deepEqual(fs.readFileSync(copy), fs.readFileSync(original));
  }
  const ownership = await import(pathToFileURL(path.join(scripts, 'native_storage_exclusions.mjs')).href);
  // Discover by walking the copied checkout, not by passing an injected empty map.
  assert.deepEqual([...ownership.readOwnedSources()], [['src/harmless.rs', sentinel]]);
  assert.equal(ownership.assertExclusionOwnership(), 1);
  const { run } = await import(pathToFileURL(path.join(scripts, 'native_storage_guard.mjs')).href);
  return { run, checkout };
}
