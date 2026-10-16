import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// UTF-16 units, excluding the terminating NUL. Count a possible JUCE staging suffix
// even though owned writers use short siblings, and leave room for future nesting.
export const windowsBuildPrefix = 'D:\\kh';
export const pathLimit = 259;
export const headroom = 40;
export const temporaryAllowance = '_temp12345678'.length;

export function windowsRelative(relative) {
  // Match KirinTestStorage / PlatformPaths::under_test_root: plug-in data is Local,
  // identity/settings are Roaming. Keep every other component (including test nesting).
  return relative.replace(/(^|\/)home\/Library\/Application Support\/Kirin OS(?=\/|$)/g,
    (match, prefix, offset, text) => `${prefix}home/AppData/${text.slice(offset + match.length).startsWith('/plugin_data/')
      || text.slice(offset + match.length) === '/plugin_data' ? 'Local' : 'Roaming'}/Kirin OS`);
}

export function projectedLength(relative, rootName = 't', buildPrefix = windowsBuildPrefix) {
  if (typeof relative !== 'string' || !relative || relative.includes('\\')
      || relative.includes('\0') || relative.includes(':')
      || relative.split('/').some(part => !part || part === '.' || part === '..'))
    throw new Error('invalid relative fixture path');
  if (!/^[a-z0-9-]+$/i.test(rootName)) throw new Error('invalid fixture root name');
  if (typeof buildPrefix !== 'string' || !path.win32.isAbsolute(buildPrefix) || buildPrefix.includes('\0'))
    throw new Error('invalid Windows build prefix');
  return `${path.win32.normalize(buildPrefix)}\\${rootName}\\${windowsRelative(relative).replaceAll('/', '\\')}`.length + temporaryAllowance;
}

export function scan(root) {
  const entries = [];
  const visit = (file, relative) => {
    const stat = fs.lstatSync(file);
    if (relative) entries.push(relative);
    // Measure links themselves; never follow a test link into user storage.
    if (stat.isDirectory())
      for (const child of fs.readdirSync(file).sort())
        visit(path.join(file, child), relative ? `${relative}/${child}` : child);
  };
  visit(root, '');
  return entries;
}

export function inspect(entries, rootName = 't', buildPrefix = windowsBuildPrefix) {
  const measured = [...new Set(entries)].map(relative => ({ relative, length: projectedLength(relative, rootName, buildPrefix) }));
  measured.sort((a, b) => b.length - a.length || a.relative.localeCompare(b.relative));
  return { count: measured.length, maximum: measured[0]?.length ?? 0,
    longest: measured.slice(0, 10), violations: measured.filter(entry => entry.length > pathLimit - headroom) };
}

// Windows measures its physical prefix. An old long build root or junction cannot
// be scored against the shorter cross-platform projection.
export function auditBuildPrefix(root, platform = process.platform, realpath = fs.realpathSync) {
  return platform === 'win32' ? path.win32.dirname(realpath(root)) : windowsBuildPrefix;
}

export function run(mode, root, ledger) {
  if (!['setup', 'cleanup'].includes(mode) || !root || !ledger) throw new Error('invalid arguments');
  root = path.resolve(root); ledger = path.resolve(ledger);
  // Both outputs are explicitly build-local siblings; never accept a filesystem root or link.
  if (path.dirname(root) !== path.dirname(ledger) || root === ledger || path.dirname(root) === root)
    throw new Error('expected separate build-local siblings');
  for (const destination of [root, ledger])
    if (fs.lstatSync(destination, { throwIfNoEntry: false })?.isSymbolicLink()) throw new Error('linked audit root');
  if (mode === 'setup') {
    fs.mkdirSync(root, { recursive: true });
    // Do not discard evidence from an interrupted/failed native run. Reports contain only
    // relative paths, so accumulating them is cheap and fails closed until a clean build.
    fs.mkdirSync(ledger, { recursive: true });
    fs.writeFileSync(path.join(ledger, 'ready'), 'native-path-budget-v1');
    return null;
  }
  if (fs.readFileSync(path.join(ledger, 'ready'), 'utf8') !== 'native-path-budget-v1')
    throw new Error('missing path audit setup');
  const entries = scan(root);
  for (const name of fs.readdirSync(ledger).filter(name => name.endsWith('.json'))) {
    const file = path.join(ledger, name);
    if (!fs.lstatSync(file).isFile()) throw new Error('invalid path ledger');
    const saved = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (!Array.isArray(saved)) throw new Error('invalid path ledger');
    entries.push(...saved);
  }
  return inspect(entries, path.basename(root), auditBuildPrefix(root));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 5) throw new Error('invalid arguments');
    const result = run(...process.argv.slice(2));
    if (result) {
      console.log(JSON.stringify(result, null, 2));
      if (result.violations.length) throw new Error('fixture path budget exceeded');
    }
    console.log(`Native path budget ${process.argv[2]}: PASS (limit ${pathLimit - headroom}, staging included)`);
  } catch (error) {
    console.error(`Native path budget: FAIL (${error.code || error.message})`);
    process.exitCode = 1;
  }
}
