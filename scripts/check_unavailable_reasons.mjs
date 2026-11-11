#!/usr/bin/env node

// A control that cannot be used says why (2026-11-11). In the JUCE sources that draw the screen, a
// control is disabled through hypha::unavailable::set (juce_shell/src/HyphaUnavailableReason.h), which
// keeps the reason on the control and shows it when the control is clicked. A direct setEnabled call is
// allowed only where scripts/unavailable_reason_kept.tsv says why the screen already explains it, or
// why the call is not a control the user presses. A new direct call fails here until it takes a reason.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const callPattern = /(?:\.|->)setEnabled\s*\(/g;

export function directCalls(text) {
  const lines = text.split('\n');
  const found = [];
  lines.forEach((line, index) => {
    const code = line.replace(/\/\/.*$/, '');
    callPattern.lastIndex = 0;
    if (callPattern.test(code)) found.push({ line: index + 1, text: line.trim() });
  });
  return found;
}

export function readKept(text) {
  const kept = new Map();
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const [file, count, reason] = raw.split('\t');
    if (!file || !count || !reason || !reason.trim())
      throw new Error(`kept row needs file, count and reason: ${raw}`);
    kept.set(file, { count: Number(count), reason: reason.trim() });
  }
  return kept;
}

export function check(root, files, kept) {
  const failures = [];
  const seen = new Set();
  for (const file of files) {
    if (file === 'juce_shell/src/HyphaUnavailableReason.h') continue;
    const calls = directCalls(fs.readFileSync(path.join(root, file), 'utf8'));
    if (calls.length === 0) continue;
    seen.add(file);
    const allowed = kept.get(file);
    if (!allowed) {
      failures.push(`${file}: ${calls.length} direct setEnabled call(s) without a reason; use hypha::unavailable::set`
        + calls.map((call) => `\n  ${file}:${call.line}: ${call.text}`).join(''));
    } else if (calls.length > allowed.count) {
      failures.push(`${file}: ${calls.length} direct setEnabled calls, ${allowed.count} kept; use hypha::unavailable::set`);
    } else if (calls.length < allowed.count) {
      failures.push(`${file}: ${calls.length} direct setEnabled calls, ${allowed.count} kept; lower the kept count`);
    }
  }
  for (const file of kept.keys())
    if (!seen.has(file)) failures.push(`${file}: kept but has no direct setEnabled call; remove the row`);
  return failures;
}

function sources(root) {
  const result = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
      const relative = `${dir}/${entry.name}`;
      if (entry.isDirectory()) walk(relative);
      else if (/\.(cpp|h|mm)$/.test(entry.name)) result.push(relative);
    }
  };
  walk('juce_shell/src');
  return result.sort();
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  const kept = readKept(fs.readFileSync(path.join(root, 'scripts/unavailable_reason_kept.tsv'), 'utf8'));
  const failures = check(root, sources(root), kept);
  if (failures.length) {
    for (const failure of failures) console.error(failure);
    process.exit(1);
  }
  console.log('unavailable reasons: PASS (every disabled control says why)');
}
