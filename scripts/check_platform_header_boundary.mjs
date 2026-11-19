import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
export function violations(root) {
  const found = [];
  function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(file);
      else if (entry.isFile() && file.endsWith('.h')) {
        const text = fs.readFileSync(file, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
        if (/^\s*#\s*include\s*[<"](?:windows|shlobj)\.h[>"]/im.test(text)) found.push(path.relative(root, file));
      }
    }
  }
  walk(path.join(root, 'juce_shell/src'));
  return found.sort();
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = fileURLToPath(new URL('../', import.meta.url));
  const bad = violations(root);
  if (bad.length) { console.error('Windows SDK header boundary failed:\n' + bad.join('\n')); process.exitCode = 1; }
  else console.log('Windows SDK header boundary: PASS');
}
