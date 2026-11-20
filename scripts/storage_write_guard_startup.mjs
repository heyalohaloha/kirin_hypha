import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

// Only disposable roots. Each subprocess has fresh, irreversible guard state.
assert.ok(process.argv.length === 4 || process.argv.length === 5);
const root = fs.mkdtempSync(path.join(process.cwd(), 'guard-startup-'));
const allowed = path.join(root, 'A'), destination = path.join(root, 'B');
fs.mkdirSync(allowed); fs.mkdirSync(destination);
let checks = 0;
try {
  const base = { ...process.env };
  delete base.KIRIN_HYPHA_PATH_LEDGER;
  delete base.KIRIN_HYPHA_PATH_BUDGET_ROOT;
  const invoke = (exe, mode, configured, expected) => {
    const env = { ...base, KIRIN_HYPHA_TEST_DISABLED_DESTINATION: destination };
    if (configured === undefined) delete env.KIRIN_HYPHA_TEST_STORAGE_ROOT;
    else env.KIRIN_HYPHA_TEST_STORAGE_ROOT = configured;
    const result = spawnSync(exe, [mode], { env, encoding: 'utf8', timeout: 5000 });
    assert.ifError(result.error);
    assert.equal(result.status, expected, result.stdout + result.stderr);
    assert.deepEqual(fs.readdirSync(root).sort(), ['A', 'B'], 'only disposable roots remain');
    for (const dir of [allowed, destination]) assert.deepEqual(fs.readdirSync(dir), [], 'fixture must leave no writes');
    ++checks;
  };
  for (const exe of process.argv.slice(2)) {
    for (const configured of [undefined, '', 'relative', path.join(root, 'missing')])
      invoke(exe, '--guard-startup-only', configured, 78);
    invoke(exe, '--guard-startup-only', allowed, 0);
    // Normal mode must also reject before fixture creation, not just diagnostic mode.
    invoke(exe, '--no-special-mode', undefined, 78);
  }
  invoke(process.argv[2], '--guard-failed-enable', allowed, 0);
  for (const exe of process.argv.slice(2)) invoke(exe, '--guard-shipping-disabled', allowed, 0);
  console.log(`STORAGE_GUARD_STARTUP_ASSERTIONS=${checks}`);
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}
