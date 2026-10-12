import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// os.userInfo() reads the OS account, unlike os.homedir(), which accepts HOME overrides.
// https://nodejs.org/api/os.html#osuserinfooptions
export function protectedRoots(home = os.userInfo().homedir, platform = process.platform,
                               env = process.env) {
  const roots = [path.join(home, 'Documents', 'Kirin OS')];
  if (platform === 'win32') {
    roots.push(path.join(env.APPDATA || path.join(home, 'AppData', 'Roaming'), 'Kirin OS'),
      path.join(env.LOCALAPPDATA || path.join(home, 'AppData', 'Local'), 'Kirin OS'));
  } else {
    roots.push(path.join(home, 'Library', 'Application Support', 'Kirin OS'));
  }
  return [...new Set(roots)];
}

function metadata(stat) {
  // Do not compare atime: the guard itself only reads, which can change access times.
  return [stat.mode, stat.size, stat.mtimeNs, stat.ctimeNs, stat.ino, stat.dev].map(String);
}

export function snapshot(root) {
  // Absence is observed without creating the root. Also detect a root created during the suite.
  if (!fs.lstatSync(root, { bigint: true, throwIfNoEntry: false })) return null;
  const entries = [];
  function visit(file, relative, ancestors) {
    const stat = fs.lstatSync(file, { bigint: true });
    const entry = { name: relative, metadata: metadata(stat) };
    entries.push(entry);
    let target = stat;
    if (stat.isSymbolicLink()) {
      entry.link = fs.readlinkSync(file);
      try { target = fs.statSync(file, { bigint: true }); }
      catch (error) {
        if (error.code !== 'ENOENT') throw error;
        entry.dangling = true;
        return;
      }
      entry.target = metadata(target);
    }
    if (target.isDirectory()) {
      const real = fs.realpathSync(file);
      if (ancestors.has(real)) { entry.cycle = true; return; }
      const next = new Set([...ancestors, real]);
      for (const name of fs.readdirSync(file).sort()) visit(path.join(file, name), `${relative}/${name}`, next);
    } else if (!target.isFile()) {
      throw new Error('unsupported protected entry type');
    }
    // Record each file's size, mtime and ctime instead of reading large audio payloads. ctime
    // also detects an overwrite followed by restoring mtime. Directory metadata catches leases
    // created and removed between snapshots; this is a mutation guard, not a content backup.
    // A live writer or disappearing entry must not produce a partial baseline that passes.
    if (JSON.stringify(metadata(fs.lstatSync(file, { bigint: true }))) !== JSON.stringify(entry.metadata))
      throw new Error('protected entry changed while reading');
  }
  visit(root, '.', new Set());
  return entries;
}

export function capture(roots) {
  return roots.map(root => ({ root, entries: snapshot(root) }));
}

export function verify(before, roots) {
  if (!Array.isArray(before) || before.length !== roots.length
      || before.some((item, index) => item.root !== roots[index]))
    throw new Error('missing or incompatible storage baseline');
  const after = capture(roots);
  const changed = after.filter((item, index) =>
    JSON.stringify(item.entries) !== JSON.stringify(before[index].entries)).length;
  if (changed) {
    // Name the changed entries (relative names only, never contents) so the writer can be found.
    const names = [];
    after.forEach((item, index) => {
      const old = new Map((before[index].entries || []).map(entry => [entry.name, JSON.stringify(entry)]));
      for (const entry of item.entries || []) {
        if (old.get(entry.name) !== JSON.stringify(entry)) names.push(entry.name);
        old.delete(entry.name);
      }
      names.push(...old.keys());
    });
    console.error(`changed entries: ${names.slice(0, 20).join(', ')}`);
    throw new Error(`protected Kirin OS storage changed (${changed} roots); inspect the local baseline`);
  }
}

export function run(mode, state, roots = protectedRoots()) {
  if (!['setup', 'cleanup'].includes(mode) || !state) throw new Error('expected setup|cleanup and baseline file');
  // The only output is an explicitly supplied build-local baseline. Never restore protected data.
  const destination = path.resolve(state);
  const realParent = fs.realpathSync(path.dirname(destination));
  const realDestination = path.join(realParent, path.basename(destination));
  for (const root of roots) {
    const candidates = [path.resolve(root)];
    if (fs.existsSync(root)) candidates.push(fs.realpathSync(root));
    for (const candidate of candidates) {
      const relative = path.relative(candidate, realDestination);
      if (relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative)))
        throw new Error('baseline must be outside protected storage');
    }
  }
  if (fs.lstatSync(destination, { throwIfNoEntry: false })?.isSymbolicLink())
    throw new Error('baseline must not be a symbolic link');
  if (mode === 'setup') {
    const before = capture(roots);
    fs.writeFileSync(destination, JSON.stringify(before), { mode: 0o600 });
    return before.filter(item => item.entries !== null).length;
  }
  const before = JSON.parse(fs.readFileSync(destination, 'utf8'));
  verify(before, roots);
  return before.filter(item => item.entries !== null).length;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 4) throw new Error('invalid arguments');
    const count = run(process.argv[2], process.argv[3]);
    console.log(`Kirin OS storage guard ${process.argv[2]}: PASS (${count} existing roots, read-only)`);
  } catch (error) {
    // Avoid exposing private filenames, song titles or local account paths in CI output.
    console.error(`Kirin OS storage guard: FAIL (${error.code || 'snapshot mismatch or invalid baseline'})`);
    process.exitCode = 1;
  }
}
