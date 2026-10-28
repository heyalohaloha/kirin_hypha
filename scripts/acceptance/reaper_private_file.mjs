import fs from 'node:fs';
import { spawnSync } from 'node:child_process';

// Reject all extended ACLs: a restrictive one is safe, but cannot be assumed portable.
export function requirePrivateFile(file) {
  const before = fs.lstatSync(file);
  if (!before.isFile() || (before.mode & 0o077) !== 0
      || (typeof process.getuid === 'function' && before.uid !== process.getuid())
      || fs.realpathSync(file) !== file) throw new Error('Private file must be owned and accessible only by its owner');
  let result;
  if (process.platform === 'darwin') {
    result = spawnSync('/bin/ls', ['-lde', file], { encoding: 'utf8', env: { ...process.env, LC_ALL: 'C' } });
    if (result.status !== 0 || /^\s*\d+:\s/m.test(result.stdout)) throw new Error('Private file ACL is unavailable or present; use a safe location');
  } else {
    result = spawnSync('/usr/bin/getfacl', ['-cp', file], { encoding: 'utf8', env: { ...process.env, LC_ALL: 'C' } });
    if (result.status !== 0 || result.stdout.split('\n').some(line => line && !/^(user|group|other)::[rwx-]{3}$/.test(line))) {
      throw new Error('Private file ACL is unavailable or present; use a safe location');
    }
  }
  const after = fs.lstatSync(file);
  if (!after.isFile() || before.dev !== after.dev || before.ino !== after.ino || before.mode !== after.mode
      || before.uid !== after.uid || before.ctimeMs !== after.ctimeMs || fs.realpathSync(file) !== file) {
    throw new Error('Private file changed during ACL inspection');
  }
  return after;
}

export function readPrivateFile(file) {
  const first = requirePrivateFile(file);
  const fd = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0));
  try {
    const opened = fs.fstatSync(fd);
    const same = (a, b) => a.isFile() && b.isFile() && a.dev === b.dev && a.ino === b.ino && a.mode === b.mode
      && a.uid === b.uid && a.size === b.size && a.mtimeMs === b.mtimeMs && a.ctimeMs === b.ctimeMs;
    if (!same(first, opened)) throw new Error('Private file changed before reading');
    const contents = fs.readFileSync(fd, 'utf8');
    if (!same(opened, fs.fstatSync(fd)) || !same(opened, requirePrivateFile(file))) throw new Error('Private file changed while reading');
    return contents;
  } finally { fs.closeSync(fd); }
}

export function writeNewPrivateFile(file, contents) {
  const fd = fs.openSync(file, 'wx', 0o600), opened = fs.fstatSync(fd);
  let success = false;
  try {
    const checked = requirePrivateFile(file);
    if (checked.dev !== opened.dev || checked.ino !== opened.ino) throw new Error('Private file changed before writing');
    fs.writeFileSync(fd, contents);
    const after = requirePrivateFile(file);
    if (after.dev !== opened.dev || after.ino !== opened.ino) throw new Error('Private file changed while writing');
    success = true;
  } finally {
    fs.closeSync(fd);
    if (!success) {
      const current = fs.lstatSync(file, { throwIfNoEntry: false });
      if (current?.dev === opened.dev && current.ino === opened.ino) fs.unlinkSync(file);
    }
  }
}
