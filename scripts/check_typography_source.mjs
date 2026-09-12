#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const sourceExtensions = new Set(['.cpp', '.h', '.mm', '.m']);
const scanRoots = ['juce_shell/src', 'juce_shell/tools', 'juce_shell/tests'];
const fontFactories = new Map([
  ['labelFont', 2],
  ['monoFont', 2],
  ['nativeTextFont', 2],
  ['displayTextFont', 3],
]);

function stripCommentsAndStrings(source) {
  let result = '';
  let state = 'code';
  for (let index = 0; index < source.length; ++index) {
    const current = source[index];
    const next = source[index + 1];
    if (state === 'code' && current === '/' && next === '/') {
      result += '  '; index += 1; state = 'line'; continue;
    }
    if (state === 'code' && current === '/' && next === '*') {
      result += '  '; index += 1; state = 'block'; continue;
    }
    if (state === 'code' && (current === '"' || current === '\'')) {
      result += ' '; state = current === '"' ? 'string' : 'character'; continue;
    }
    if (state === 'line') {
      result += current === '\n' ? '\n' : ' ';
      if (current === '\n') state = 'code';
      continue;
    }
    if (state === 'block') {
      if (current === '*' && next === '/') {
        result += '  '; index += 1; state = 'code';
      } else result += current === '\n' ? '\n' : ' ';
      continue;
    }
    if (state === 'string' || state === 'character') {
      const terminator = state === 'string' ? '"' : '\'';
      if (current === '\\' && next !== undefined) {
        result += '  '; index += 1;
      } else {
        result += current === '\n' ? '\n' : ' ';
        if (current === terminator) state = 'code';
      }
      continue;
    }
    result += current;
  }
  return result;
}

function lineAt(source, index) {
  return source.slice(0, index).split('\n').length;
}

function callArguments(source, openIndex) {
  let depth = 1;
  let commas = 0;
  for (let index = openIndex + 1; index < source.length; ++index) {
    if (source[index] === '(' || source[index] === '{' || source[index] === '[') depth += 1;
    else if (source[index] === ')' || source[index] === '}' || source[index] === ']') depth -= 1;
    else if (source[index] === ',' && depth === 1) commas += 1;
    if (depth === 0) {
      const content = source.slice(openIndex + 1, index).trim();
      return { count: content ? commas + 1 : 0, content };
    }
  }
  return { count: 0, content: '', unclosed: true };
}

function recordMatches(violations, source, pattern, reason) {
  for (const match of source.matchAll(pattern))
    violations.push({ line: lineAt(source, match.index), reason });
}

function recordFontHeightMutations(violations, source) {
  const fontVariables = new Set();
  for (const match of source.matchAll(
    /\b(?:const\s+)?(?:juce\s*::\s*)?Font\s*(?:const\s*)?[&*]?\s*([A-Za-z_]\w*)/g,
  )) fontVariables.add(match[1]);
  for (const match of source.matchAll(
    /\b(?:auto|(?:juce\s*::\s*)?Font)\s+([A-Za-z_]\w*)\s*=\s*(?:labelFont|monoFont|nativeTextFont|displayTextFont)\s*\(/g,
  )) fontVariables.add(match[1]);

  for (const match of source.matchAll(
    /\b([A-Za-z_]\w*)\s*\.\s*(?:withHeight|setHeight)\s*\(/g,
  )) {
    if (/font/i.test(match[1]) || fontVariables.has(match[1])) {
      violations.push({
        line: lineAt(source, match.index),
        reason: 'direct height mutation bypasses the semantic typography contract',
      });
    }
  }
  recordMatches(
    violations,
    source,
    /\b(?:labelFont|monoFont|nativeTextFont|displayTextFont)\s*\([^;]*?\)\s*\.\s*(?:withHeight|setHeight)\s*\(/g,
    'direct height mutation bypasses the semantic typography contract',
  );
}

export function findTypographyViolations(source, relativePath = 'fixture.cpp') {
  const clean = stripCommentsAndStrings(source);
  const violations = [];
  const adapter = relativePath.split(path.sep).join('/') === 'juce_shell/src/HyphaTypography.cpp';
  if (!adapter) {
    recordMatches(violations, clean, /\bjuce\s*::\s*Font\s*[({]/g,
      'direct juce::Font construction is restricted to HyphaTypography.cpp');
    recordMatches(violations, clean, /\b(?:juce\s*::\s*)?FontOptions\s*[({]/g,
      'direct FontOptions construction bypasses the semantic typography contract');
    recordFontHeightMutations(violations, clean);
  }
  recordMatches(violations, clean, /\.\s*withHorizontalScale\s*\(/g,
    'horizontal font compression is prohibited');
  recordMatches(violations, clean, /\bdrawFittedText\s*\(/g,
    'drawFittedText may compress text; use HyphaTextStyle overflow handling');

  for (const match of clean.matchAll(/\b(labelFont|monoFont|nativeTextFont|displayTextFont)\s*\(/g)) {
    const name = match[1];
    const openIndex = clean.indexOf('(', match.index + name.length);
    const args = callArguments(clean, openIndex);
    if (args.unclosed || args.count < fontFactories.get(name))
      violations.push({ line: lineAt(clean, match.index),
        reason: `${name} requires presentation context and semantic text role` });
  }

  for (const match of clean.matchAll(/\bsetMinimumHorizontalScale\s*\(/g)) {
    const openIndex = clean.indexOf('(', match.index);
    const args = callArguments(clean, openIndex);
    if (!/^(?:1(?:\.0*)?f?)$/.test(args.content))
      violations.push({ line: lineAt(clean, match.index),
        reason: 'minimum horizontal scale must remain 1.0 (no compression)' });
  }
  if (!adapter) {
    for (const match of clean.matchAll(/\bsetFont\s*\(/g)) {
      const openIndex = clean.indexOf('(', match.index);
      const args = callArguments(clean, openIndex);
      if (!/\b(?:labelFont|monoFont|nativeTextFont|displayTextFont)\s*\(/.test(args.content))
        violations.push({ line: lineAt(clean, match.index),
          reason: 'setFont must consume a semantic typography factory in the same call' });
    }
  }
  return violations.sort((left, right) => left.line - right.line
    || left.reason.localeCompare(right.reason));
}

export function scanTypographySources(root) {
  const findings = [];
  const visit = (absoluteDirectory, relativeDirectory) => {
    if (!fs.existsSync(absoluteDirectory)) return;
    for (const entry of fs.readdirSync(absoluteDirectory, { withFileTypes: true })) {
      const absolute = path.join(absoluteDirectory, entry.name);
      const relative = path.join(relativeDirectory, entry.name);
      if (entry.isDirectory() && !entry.isSymbolicLink()) visit(absolute, relative);
      else if (entry.isFile() && sourceExtensions.has(path.extname(entry.name))) {
        for (const violation of findTypographyViolations(fs.readFileSync(absolute, 'utf8'), relative))
          findings.push({ path: relative.split(path.sep).join('/'), ...violation });
      }
    }
  };
  for (const relative of scanRoots) visit(path.join(root, relative), relative);
  return findings.sort((left, right) => left.path.localeCompare(right.path)
    || left.line - right.line || left.reason.localeCompare(right.reason));
}

function main() {
  const directory = path.dirname(fileURLToPath(import.meta.url));
  const root = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(directory, '..');
  const findings = scanTypographySources(root);
  if (findings.length === 0) {
    console.log('typography source contract: PASS (tracked and untracked JUCE source trees)');
    return;
  }
  console.error('typography source contract: FAIL');
  for (const finding of findings)
    console.error(`- ${finding.path}:${finding.line}: ${finding.reason}`);
  process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
