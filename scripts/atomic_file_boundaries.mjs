import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

// Intentional MAX_PATH boundary I/O is separate from the ordinary fixture budget. Like
// the budget's negative self-tests, it cannot claim to be a normal, within-budget layout.
assert.equal(process.argv.length, 3);
const root = fs.mkdtempSync(path.join(process.cwd(), 'boundary-'));
try {
  const env = { ...process.env, KIRIN_HYPHA_TEST_STORAGE_ROOT: root };
  delete env.KIRIN_HYPHA_PATH_LEDGER;
  delete env.KIRIN_HYPHA_PATH_BUDGET_ROOT;
  const result = spawnSync(process.argv[2], ['--boundary-io'], { env, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.deepEqual(fs.readdirSync(root), [], 'boundary native code must remove its files');
  console.log('Atomic file boundary I/O: PASS (text: 246, 247, 249, 259; adoption: 243 characters)');
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}
