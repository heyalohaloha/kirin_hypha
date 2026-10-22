import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { assertExclusionOwnership, excluded, exclusionParent, exclusionRules } from './native_storage_exclusions.mjs';

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

function metadata(stat, container = false) {
  // Do not compare atime: the guard itself only reads, which can change access times.
  return (container ? [stat.mode, stat.ino, stat.dev]
    : [stat.mode, stat.size, stat.mtimeNs, stat.ctimeNs, stat.ino, stat.dev]).map(String);
}

export function snapshot(root) {
  // Absence is observed without creating the root. Also detect a root created during the suite.
  if (!fs.lstatSync(root, { bigint: true, throwIfNoEntry: false })) return null;
  const entries = [];
  function visit(file, relative, ancestors) {
    const container = exclusionParent(relative === '.' ? '' : relative.slice(2));
    const optional = container && relative !== '.';
    const start = entries.length;
    const stat = fs.lstatSync(file, { bigint: true, throwIfNoEntry: false });
    if (!stat) {
      if (optional) return;
      throw new Error('protected entry disappeared while reading');
    }
    const entry = { name: relative, metadata: metadata(stat, container && stat.isDirectory()) };
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
      entry.target = metadata(target, container && target.isDirectory());
    }
    if (target.isDirectory()) {
      let names;
      try { names = fs.readdirSync(file, { withFileTypes: true })
        .sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0); }
      catch (error) {
        if (optional && !stat.isSymbolicLink() && error.code === 'ENOENT') {
          entries.length = start; return;
        }
        throw error;
      }
      if (optional && stat.isDirectory()
          && names.every(child => excluded(`${relative}/${child.name}`.slice(2), !child.isFile()))) {
        entries.length = start; return;
      }
      const real = fs.realpathSync(file);
      if (ancestors.has(real)) { entry.cycle = true; return; }
      const next = new Set([...ancestors, real]);
      for (const child of names) {
        const childRelative = `${relative}/${child.name}`;
        // Do not stat app-owned entries: they may vanish or be replaced during this walk.
        if (!excluded(childRelative.slice(2), !child.isFile())) visit(path.join(file, child.name), childRelative, next);
      }
    } else if (!target.isFile() && !target.isSocket()) {
      throw new Error('unsupported protected entry type');
    }
    // A running app may own a Unix socket here. Observe its metadata like a file,
    // without opening or connecting to it; replacement/removal still fails comparison.
    // Record each file's size, mtime and ctime instead of reading large audio payloads. ctime
    // also detects an overwrite followed by restoring mtime. Exclusion parents omit changing
    // timestamps/size, but retain mode/inode/device. See the documented transient-write limit.
    // A live writer or disappearing entry must not produce a partial baseline that passes.
    // An ordinary ancestor containing only excluded entries is app scaffolding.
    // Omit it even when initially created or later removed. A symlink or any
    // unexcluded child keeps the ancestor and its authority in the snapshot.
    const scaffolding = optional && stat.isDirectory() && entries.length === start + 1;
    const after = fs.lstatSync(file, { bigint: true, throwIfNoEntry: false });
    if (!after && scaffolding) { entries.length = start; return; }
    if (!after || JSON.stringify(metadata(after, container && stat.isDirectory())) !== JSON.stringify(entry.metadata))
      throw new Error('protected entry changed while reading');
    if (scaffolding) entries.length = start;
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
    // Even relative filenames may contain private titles. Keep the baseline local.
    throw new Error(`protected Kirin OS storage changed (${changed} roots); inspect the local baseline`);
  }
}

export function run(mode, state, roots = protectedRoots()) {
  if (!['setup', 'cleanup'].includes(mode) || !state) throw new Error('expected setup|cleanup and baseline file');
  assertExclusionOwnership();
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
    fs.writeFileSync(destination, JSON.stringify({ schema: 'hypha-storage-guard-v4', exclusions: exclusionRules, before }), { mode: 0o600 });
    return before.filter(item => item.entries !== null).length;
  }
  const baseline = JSON.parse(fs.readFileSync(destination, 'utf8'));
  if (baseline.schema !== 'hypha-storage-guard-v4'
      || JSON.stringify(baseline.exclusions) !== JSON.stringify(exclusionRules))
    throw new Error('missing or incompatible storage baseline exclusions');
  verify(baseline.before, roots);
  return baseline.before.filter(item => item.entries !== null).length;
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
