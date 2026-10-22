#!/usr/bin/env node
// Scan publishable working-tree text and paths for private environment identifiers.
// Site-specific names stay in an ignored/private list, never in this checker.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { namePatterns, isRetainedUpstreamNotice } from './check_public_text.mjs';

const patterns = [
  { kind: 'local user path', expression: /(?<![A-Za-z0-9_.-])(?:\/Users\/|\/home\/|[A-Za-z]:\\+Users\\+)(?![<{$])([A-Za-z0-9_.-]+)(?=[\\/])/gi },
  { kind: 'LAN address', expression: /(?<![§\d.])\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b/g },
  { kind: 'local hostname', expression: /(?:https?:\/\/|smb:\/\/|["'`])([a-z0-9][a-z0-9-]*\.local)(?=[\/\s"'`]|$)/gi },
];
// Public documentation placeholders and OS-provided shared folders are not private identities.
const placeholders = /^(?:user|username|name|someone|example|runner|public|default|shared|test|fixture|USERNAME)$/i;
// 証明書名は署名した配布物に入る公開の情報。
const certificateDeclarations = new Map([
  ['scripts/ls_release/aax_bundle_verify.mjs', /^export const AAX_APPLE_AUTHORITY = 'Developer ID Application: [^'\\\r\n]+ \([A-Z0-9]{10}\)';$/],
  ['scripts/windows/windows-aax-bundles.mjs', /^const AUTHENTICODE_SUBJECT = 'CN=[^'\\\r\n]+';$/],
  ['xtask/src/notarize.rs', /^const DEFAULT_IDENTITY: &str = "Developer ID Application: [^"\\\r\n]+ \([A-Z0-9]{10}\)";$/],
]);
export function findEnvironmentText(text, privateNames = []) {
  const findings = [];
  for (const [index, line] of text.split('\n').entries()) {
    for (const pattern of patterns) {
      pattern.expression.lastIndex = 0;
      for (const match of line.matchAll(pattern.expression)) {
        if (pattern.kind === 'local user path' && placeholders.test(match[1])) continue;
        if (pattern.kind === 'LAN address' && /^#{1,6}\s+$/.test(line.slice(0, match.index))) continue;
        findings.push({ line: index + 1, kind: pattern.kind });
      }
    }
    for (const pattern of namePatterns(privateNames))
      if (pattern.expression.test(line)) findings.push({ line: index + 1, kind: 'private environment name' });
  }
  return findings;
}
export function scanEnvironment(root, privateNames = []) {
  const paths = execFileSync('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z'],
    { cwd: root, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }).split('\0').filter(Boolean);
  const findings = [];
  for (const relative of new Set(paths)) {
    // Git submodules/dependency trees have separate upstream ownership. Retained notices must
    // match their reviewed original bytes to qualify for the existing upstream boundary.
    if (/^(?:vendor|juce_shell\/JUCE)\//.test(relative) || isRetainedUpstreamNotice(root, relative)) continue;
    const file = path.join(root, relative), stat = fs.lstatSync(file, { throwIfNoEntry: false });
    if (!stat || stat.isDirectory()) continue;
    const bytes = stat.isSymbolicLink() ? Buffer.from(fs.readlinkSync(file)) : fs.readFileSync(file);
    const texts = [{ text: relative, location: 'path' }];
    if (!bytes.includes(0)) texts.push({ text: bytes.toString('utf8'), location: 'content' });
    for (const { text, location } of texts) {
      const lines = text.split('\n');
      for (const item of findEnvironmentText(text, privateNames)) {
        if (!stat.isSymbolicLink() && location === 'content' && item.kind === 'private environment name'
            && certificateDeclarations.get(relative)?.test(lines[item.line - 1])) continue;
        findings.push({ path: relative, location, ...item });
      }
    }
  }
  return findings;
}
export function privateEnvironmentNames(root, env = process.env) {
  const common = path.resolve(root, execFileSync('git', ['rev-parse', '--git-common-dir'], { cwd: root, encoding: 'utf8' }).trim());
  const names = [];
  for (const file of ['hypha-public-text-names.txt', 'hypha-public-environment-names.txt']) {
    const absolute = path.join(common, file);
    if (fs.existsSync(absolute)) names.push(...fs.readFileSync(absolute, 'utf8').split('\n'));
  }
  names.push(...(env.PUBLIC_REPO_PRIVATE_NAMES || '').split(/[\n,]/));
  return names.filter(name => name.trim() && !name.trim().startsWith('#'));
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const root = path.resolve(import.meta.dirname, '..');
  const names = privateEnvironmentNames(root), findings = scanEnvironment(root, names);
  console.log(`public environment: ${findings.length ? 'FAIL' : 'PASS'} (${findings.length} findings; private list ${names.length ? 'loaded' : 'unavailable'})`);
  if (!names.length) console.warn('WARN: private environment name list unavailable; structural checks only');
  // Do not echo the matching private text; locations suffice for a local reviewer.
  for (const item of findings) console.error(`${item.path}:${item.line}: ${item.location}: ${item.kind}`);
  if (findings.length) process.exitCode = 1;
}
