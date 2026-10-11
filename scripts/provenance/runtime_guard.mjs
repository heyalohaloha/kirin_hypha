import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { RUST_RUNTIME_VERSION } from './bundled_components.mjs';

export function assertRustRuntime(version) {
  if (!new RegExp(`^rustc ${RUST_RUNTIME_VERSION.replaceAll('.', '\\.')} \\([^\\r\\n]+\\)$`).test(version.trim())) {
    throw new Error(`Rust runtime must match retained notices (${RUST_RUNTIME_VERSION}) before producing or freezing payloads`);
  }
  return version.trim();
}
export function readRustRuntime(root) {
  return assertRustRuntime(execFileSync('rustc', ['--version'], { cwd: root, encoding: 'utf8' }));
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { console.log(readRustRuntime(process.cwd())); }
  catch (error) { console.error(`[runtime-guard] ${error.message}`); process.exitCode = 1; }
}
