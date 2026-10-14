// The shipping file set is shared by producer, import, installer and fallback ZIP.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';

export function qualifiedWindowsVst3Files(bundle, role) {
  if (!['PRE', 'POST'].includes(role) || path.basename(bundle) !== `Kirin Hypha ${role}.vst3`) {
    throw new Error('Windows VST3 bundle role/name mismatch');
  }
  const required = [`Contents/x86_64-win/Kirin Hypha ${role}.vst3`, 'Contents/Resources/moduleinfo.json'];
  const icons = ['desktop.ini', 'Plugin.ico'];
  const allowed = new Set([...required, ...icons]), files = [];
  const walk = (folder, prefix = '') => {
    if (!fs.lstatSync(folder).isDirectory() || fs.lstatSync(folder).isSymbolicLink()) throw new Error('VST3 folder must be regular');
    for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
      const relative = prefix + entry.name, full = path.join(folder, entry.name);
      if (entry.isDirectory()) {
        if (!['Contents', 'Contents/x86_64-win', 'Contents/Resources'].includes(relative)) throw new Error('Unexpected VST3 directory');
        walk(full, relative + '/');
      } else {
        if (!entry.isFile() || !allowed.has(relative)) throw new Error(`Unexpected VST3 shipping file: ${relative}`);
        const bytes = fs.readFileSync(full);
        if (!bytes.length) throw new Error(`Empty VST3 shipping file: ${relative}`);
        files.push({ path: relative, sha256: createHash('sha256').update(bytes).digest('hex') });
      }
    }
  };
  walk(bundle);
  if (required.some(name => !files.some(file => file.path === name))) throw new Error('Missing VST3 shipping file');
  if (icons.some(name => files.some(file => file.path === name)) && !icons.every(name => files.some(file => file.path === name))) {
    throw new Error('Incomplete VST3 icon file pair');
  }
  return files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}
