#!/usr/bin/env node
// Public text contract. Hypha is public open source: the text a change adds and the messages of its
// commits carry no internal records. It looks for the shapes such records take (an attribution to a
// person, an internal branch, a work-item or plan number, a local path, Kirin OS's internal paths or
// private repository), plus the names on a private list kept outside the repository. A bare "owner" is
// not a pattern: code says owner->getProperties() and the like.
//
// Scope: lines added since the base (a line moved unchanged within the change is not new) (PUBLIC_TEXT_BASE_REF, else SOURCE_LINE_BUDGET_BASE_REF as CI sets
// it, else the merge base with origin/main), and the messages of commits after the base. Messages of
// commits reachable from scripts/public_text_baseline.txt were published before this check and can
// only be fixed by rewriting history: they are reported as remaining, not failed. The tree as a whole
// is not scanned (older text predates the check).
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SELF = new Set(['scripts/check_public_text.mjs', 'scripts/check_public_text.test.mjs']);

export const PATTERNS = [
  { kind: 'attribution', expression: /\((?:the )?owner, \d{4}-/i },
  { kind: 'attribution', expression: /\bby the owner on\b/i },
  { kind: 'attribution', expression: /持ち主の(?:実素材|素材|発言|指示)/ },
  { kind: 'internal branch', expression: /(?:^|[\s'"(`])(?:claude|codex)\/[A-Za-z0-9]/ },
  { kind: 'work item', expression: /\bW-?\d{3,4}\b/ },
  { kind: 'review item', expression: /\bX[1-4]\b/ },
  { kind: 'plan number', expression: /\b(?:HY-\d+|F-[HK]\d{2}|TH-\d{2}|KO-\d)\b/ },
  { kind: 'local path', expression: /\/Users\// },
  { kind: 'Kirin OS internal', expression: /\bnative\/src\/|kirin_sense_lens|\bsense_lens\b/ },
];

const escape = (text) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// Names from the private list: whole words for Latin names, as written for the others.
export function namePatterns(names) {
  return names
    .map((name) => name.trim())
    .filter((name) => name && !name.startsWith('#'))
    .map((name) => ({
      kind: 'private name',
      expression: /^[\x20-\x7e]+$/.test(name) ? new RegExp(`\\b${escape(name)}\\b`, 'i') : new RegExp(escape(name)),
    }));
}

export function findRecords(text, patterns) {
  const found = [];
  for (const [index, line] of text.split('\n').entries())
    for (const { kind, expression } of patterns)
      if (expression.test(line)) found.push({ line: index + 1, kind, text: line.trim().slice(0, 160) });
  return found;
}

// The `+` lines of a unified diff, with their file and line in the new version.
export function addedLines(diff) {
  const lines = [];
  let file = null, next = 0;
  for (const line of diff.split('\n')) {
    if (line.startsWith('+++ ')) { file = line.startsWith('+++ b/') ? line.slice(6) : null; continue; }
    const hunk = line.match(/^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@/);
    if (hunk) { next = Number(hunk[1]); continue; }
    if (file === null) continue;
    if (line.startsWith('+')) { lines.push({ file, line: next, text: line.slice(1) }); ++next; }
    else if (!line.startsWith('-')) ++next;
  }
  return lines;
}

// The `-` lines of a unified diff, inside its hunks.
export function removedLines(diff) {
  const lines = [];
  let inHunk = false;
  for (const line of diff.split('\n')) {
    if (line.startsWith('diff --git ')) { inHunk = false; continue; }
    if (/^@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@/.test(line)) { inHunk = true; continue; }
    if (inHunk && line.startsWith('-')) lines.push(line.slice(1));
  }
  return lines;
}

// The added lines that are new text. A line moved unchanged within the same change (removed in one
// place, added in another) publishes nothing new, so it is not checked again.
export function newLines(diff) {
  const moved = new Map();
  for (const text of removedLines(diff)) moved.set(text, (moved.get(text) ?? 0) + 1);
  return addedLines(diff).filter((added) => {
    const count = moved.get(added.text) ?? 0;
    if (count === 0) return true;
    moved.set(added.text, count - 1);
    return false;
  });
}

function git(args) {
  return execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
}

function privateNames() {
  if (process.env.PUBLIC_TEXT_PRIVATE_NAMES) return { names: process.env.PUBLIC_TEXT_PRIVATE_NAMES.split(/[\n,]/), from: 'environment' };
  const common = path.resolve(ROOT, git(['rev-parse', '--git-common-dir']).trim());
  const file = path.join(common, 'hypha-public-text-names.txt');
  return fs.existsSync(file) ? { names: fs.readFileSync(file, 'utf8').split('\n'), from: 'private list' } : null;
}

function baseRef() {
  for (const variable of ['PUBLIC_TEXT_BASE_REF', 'SOURCE_LINE_BUDGET_BASE_REF'])
    if (process.env[variable]) return process.env[variable];
  return git(['merge-base', 'HEAD', 'origin/main']).trim();
}

function baseline() {
  const file = path.join(ROOT, 'scripts', 'public_text_baseline.txt');
  const sha = fs.readFileSync(file, 'utf8').split('\n').map((line) => line.trim()).find((line) => /^[0-9a-f]{40}$/.test(line));
  if (!sha) return null;
  try { git(['cat-file', '-e', `${sha}^{commit}`]); return sha; } catch { return null; }
}

function main() {
  const base = baseRef();
  const listed = privateNames();
  const patterns = [...PATTERNS, ...(listed ? namePatterns(listed.names) : [])];
  const findings = [];
  const diff = git(['diff', '--unified=0', '--no-color', '--no-ext-diff', `${base}...HEAD`, '--', '.', ':(exclude)juce_shell/JUCE']);
  for (const added of newLines(diff)) {
    if (SELF.has(added.file)) continue;
    for (const record of findRecords(added.text, patterns))
      findings.push(`${added.file}:${added.line}: ${record.kind}: ${record.text}`);
  }
  const before = baseline();
  const messages = git(['log', '--no-merges', '--format=%H%x1f%B%x1e', `${base}..HEAD`]).split('\x1e').map((item) => item.trim()).filter(Boolean)
    .map((item) => { const [sha, body] = item.split('\x1f'); return { sha: sha.trim(), body: body ?? '' }; });
  const old = new Set(before ? git(['rev-list', before]).split('\n').map((line) => line.trim()).filter(Boolean) : []);
  let remaining = 0;
  for (const { sha, body } of messages) {
    const records = findRecords(body, patterns);
    if (records.length === 0) continue;
    if (old.has(sha)) { remaining += records.length; continue; }
    for (const record of records) findings.push(`commit ${sha.slice(0, 8)} message line ${record.line}: ${record.kind}: ${record.text}`);
  }
  const names = listed ? `names from the ${listed.from}` : 'names: SKIPPED (no private list)';
  const rest = remaining ? `; ${remaining} records remain in messages published before ${before.slice(0, 8)} (history is not rewritten)` : '';
  if (findings.length === 0) {
    console.log(`public text: PASS (since ${base.slice(0, 8)}; ${names}${rest})`);
    return;
  }
  console.error(`public text: FAIL (since ${base.slice(0, 8)}; ${names}${rest})`);
  for (const finding of findings) console.error(`- ${finding}`);
  process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
