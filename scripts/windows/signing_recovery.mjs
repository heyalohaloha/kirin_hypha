// Preserve outputs of a failed or uncertain signing request; never save credentials or error text.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export function createSigningAttempt(root, kind) {
  if (!/^[a-zA-Z0-9_.-]+$/.test(kind)) throw new Error('Invalid signing attempt kind');
  fs.mkdirSync(root, { recursive: true });
  if (fs.lstatSync(root).isSymbolicLink()) throw new Error('Signing recovery root must not be a symlink');
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    if (!entry.isDirectory()) throw new Error('Unexpected signing recovery entry');
    const prior = path.join(root, entry.name, 'attempt.json');
    if (!fs.existsSync(prior)) throw new Error('Unreconciled signing recovery output');
    const record = JSON.parse(fs.readFileSync(prior, 'utf8'));
    if (record.kind === kind) throw new Error('Existing signing attempt requires reconciliation; automatic resend refused');
  }
  const directory = path.join(root, `${kind}-${crypto.randomUUID()}`);
  fs.mkdirSync(directory);
  fs.writeFileSync(path.join(directory, 'attempt.json'), JSON.stringify({
    schema: 'hypha-signing-attempt-v1', kind, state: 'prepared',
  }) + '\n', { flag: 'wx' });
  return { directory, kind };
}

export function finishSigningAttempt(attempt, state) {
  if (!['completed', 'failed-or-uncertain'].includes(state)) throw new Error('Invalid signing attempt state');
  const files = [];
  const walk = (directory, prefix = '') => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const relative = prefix + entry.name, file = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) throw new Error('Signing output symlink is not accepted');
      if (entry.isDirectory()) walk(file, relative + '/');
      else if (entry.isFile() && relative !== 'attempt.json' && relative !== 'totp-window.state') {
        files.push({ path: relative, bytes: fs.statSync(file).size,
          sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') });
      }
    }
  };
  walk(attempt.directory);
  fs.rmSync(path.join(attempt.directory, 'totp-window.state'), { force: true });
  fs.writeFileSync(path.join(attempt.directory, 'attempt.json'), JSON.stringify({
    schema: 'hypha-signing-attempt-v1', kind: attempt.kind, state, files: files.sort((a, b) => a.path.localeCompare(b.path)),
  }, null, 2) + '\n');
}
