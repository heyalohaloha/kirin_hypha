#!/usr/bin/env node

// Every piece of English prose Hypha can put on its screen has Japanese (INV-S40): a sentence, or
// a status of three or more upper-case words, written in a JUCE source that draws the screen must
// be in the Japanese catalog (juce_shell/src/HyphaJapanese*.cpp), be a piece the catalog's
// entries are built from, or be listed in scripts/screen_text_kept.tsv with the reason it stays
// English (a label, a name, a legend, text that never reaches the screen).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const sourceExtensions = new Set(['.cpp', '.h', '.mm']);
// Sources whose strings never reach the screen as prose: the catalog itself, preference files,
// wire protocols, file formats, and host and diagnostic plumbing.
const offScreen = [
  /^juce_shell\/src\/HyphaJapanese[A-Za-z]*\.(cpp|h)$/,
  /^juce_shell\/src\/HyphaLanguage\.(cpp|h)$/,
  /^juce_shell\/src\/HyphaUiPreferences\.(cpp|h)$/,
  /^juce_shell\/src\/HyphaHoverHelpPreference\.cpp$/,
  /^juce_shell\/src\/HyphaUiContract\.h$/,
  /^juce_shell\/src\/HyphaTypography\.cpp$/,
  /^juce_shell\/src\/HyphaUpdateContract\.h$/,
  /^juce_shell\/src\/PluginProcessorValidation\.cpp$/,
  /^juce_shell\/src\/CaptureWorkAttachment\.cpp$/,
  /^juce_shell\/src\/appearance\//,
  /^juce_shell\/src\/local_blind\//,
  /^juce_shell\/src\/reference_audition\/(?!ReferenceACaptureSession\.cpp|ReferenceACaptureRestore\.cpp|ReferenceComparisonController\.cpp|ReferenceRuntimeV2Workspace\.cpp)/,
  /^juce_shell\/src\/pre_display\/(?!PreDisplayProjection\.cpp)/,
];

function unescape(body) {
  return body.replace(/\\(x[0-9A-Fa-f]{2}|n|t|"|'|\\)/g, (match, code) => {
    if (code === 'n') return '\n';
    if (code === 't') return '\t';
    if (code.startsWith('x')) return String.fromCharCode(parseInt(code.slice(1), 16));
    return code;
  });
}

// A literal written with byte escapes ("\xCE\x94") holds UTF-8: read its bytes back as text.
function literalText(body) {
  if (!/\\x[89A-Fa-f][0-9A-Fa-f]/.test(body)) return unescape(body);
  const bytes = [];
  for (let index = 0; index < body.length;) {
    const escape = body.slice(index).match(/^\\(x[0-9A-Fa-f]{2}|n|t|"|'|\\)/);
    if (escape) {
      const code = escape[1];
      bytes.push(code.startsWith('x') ? parseInt(code.slice(1), 16)
        : code === 'n' ? 10 : code === 't' ? 9 : code.charCodeAt(0));
      index += escape[0].length;
    } else {
      bytes.push(...Buffer.from(body[index], 'utf8'));
      index += 1;
    }
  }
  return Buffer.from(bytes).toString('utf8');
}

// The string literals of a C++ source with their lines, adjacent literals joined, comments and
// character literals skipped.
export function literals(source) {
  const found = [];
  let state = 'code';
  let pending = null;
  let codeSince = '';
  for (let index = 0; index < source.length; ++index) {
    const current = source[index];
    const next = source[index + 1];
    if (state === 'line') { if (current === '\n') state = 'code'; continue; }
    if (state === 'block') { if (current === '*' && next === '/') { state = 'code'; index += 1; } continue; }
    if (current === '/' && next === '/') { state = 'line'; index += 1; continue; }
    if (current === '/' && next === '*') { state = 'block'; index += 1; continue; }
    if (current === '\'') {
      const separator = /[0-9A-Fa-f]/.test(source[index - 1] ?? '') && /[0-9A-Fa-f]/.test(next ?? '');
      if (!separator) {
        index += 1;
        while (index < source.length && source[index] !== '\'') index += source[index] === '\\' ? 2 : 1;
      }
      codeSince += 'x';
      continue;
    }
    if (current !== '"') { codeSince += current; continue; }
    const start = index;
    let body = '';
    index += 1;
    while (index < source.length && source[index] !== '"') {
      if (source[index] === '\\') { body += source.slice(index, index + 2); index += 2; }
      else { body += source[index]; index += 1; }
    }
    const text = literalText(body);
    const line = source.slice(0, start).split('\n').length;
    const before = source.slice(Math.max(0, start - 200), start);
    if (pending && /^\s*(?:u8|u|U|L)?$/.test(codeSince)) pending.text += text;
    else {
      if (pending) found.push(pending);
      pending = { text, line, context: before };
    }
    codeSince = '';
  }
  if (pending) found.push(pending);
  return found;
}

// The catalog's English: exact entries and the fixed pieces of its patterns.
export function catalogEnglish(root) {
  const exact = new Set();
  const pieces = [];
  const patterns = [];
  const english = [];
  const directory = path.join(root, 'juce_shell', 'src');
  for (const name of fs.readdirSync(directory)) {
    if (!/^HyphaJapanese[A-Za-z]+\.cpp$/.test(name)) continue;
    const source = fs.readFileSync(path.join(directory, name), 'utf8');
    for (const match of source.matchAll(/\{\s*((?:(?:u8)?"(?:[^"\\]|\\.)*"\s*)+),\s*(?:(?:u8)?"(?:[^"\\]|\\.)*"\s*)+\}/g)) {
      const entry = [...match[1].matchAll(/(?:u8)?"((?:[^"\\]|\\.)*)"/g)]
        .map(part => unescape(part[1])).join('');
      english.push(entry);
      if (/%[1-3]/.test(entry)) {
        patterns.push(new RegExp('^' + entry.split(/%[1-3]/)
          .map(piece => piece.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('(.+)') + '$', 's'));
        for (const piece of entry.split(/%[1-3]/)) if (piece.trim()) pieces.push(piece);
      } else {
        exact.add(entry);
        pieces.push(entry);
      }
    }
  }
  return { exact, pieces, patterns, english };
}

export function looksLikeProse(text) {
  // Text already in Japanese (a Japanese link, a Kirin OS name) needs no entry.
  if (/[\u3000-\u30ff\u4e00-\u9fff\uff00-\uffef]/.test(text)) return false;
  const words = text.match(/[A-Za-z']+/g) ?? [];
  const sentence = /[a-z]{3,}/.test(text) && /\S\s+\S/.test(text);
  const capitals = words.filter(word => word.length >= 2 && word === word.toUpperCase()).length;
  return sentence || capitals >= 3;
}

function covered(text, catalog, kept) {
  const trimmed = text.trim();
  if (!trimmed || kept.has(text) || kept.has(trimmed)) return true;
  if (catalog.exact.has(text) || catalog.exact.has(trimmed)) return true;
  if (catalog.patterns.some(pattern => pattern.test(trimmed))) return true;
  // A piece the catalog's entries are composed from ("Open Kirin OS.\n", " · ABSOLUTE").
  const bare = trimmed.replace(/^[\s/·]+|[\s/·]+$/g, '');
  if (bare && catalog.pieces.some(piece => piece.includes(bare))) return true;
  // Facts joined by " / ", "  ·  " or a new line translate one by one.
  const parts = trimmed.split(/\s+[/·]\s+|\n/).map(part => part.trim()).filter(Boolean);
  if (parts.length > 1)
    return parts.every(part => !looksLikeProse(part) || covered(part, catalog, kept));
  return false;
}

export function readKept(root) {
  const file = path.join(root, 'scripts', 'screen_text_kept.tsv');
  const kept = new Set();
  for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
    if (!line.trim() || line.startsWith('#')) continue;
    const [text, reason] = line.split('\t');
    if (!reason?.trim()) throw new Error(`screen_text_kept.tsv: no reason for "${text}"`);
    kept.add(unescape(text));
  }
  return kept;
}

// The text of every string literal in the screen sources and in the Rust engine, whose Keep,
// Record and Mark notices reach the screen too.
function allLiteralText(root) {
  const texts = [];
  const visit = (absolute, extensions, read) => {
    if (!fs.existsSync(absolute)) return;
    for (const entry of fs.readdirSync(absolute, { withFileTypes: true })) {
      const child = path.join(absolute, entry.name);
      // The catalog's own English is what is being looked for, not a place it is used.
      if (/^HyphaJapanese[A-Za-z]*\.(cpp|h)$/.test(entry.name)) continue;
      if (entry.isDirectory()) visit(child, extensions, read);
      else if (extensions.has(path.extname(entry.name))) texts.push(...read(fs.readFileSync(child, 'utf8')));
    }
  };
  visit(path.join(root, 'juce_shell', 'src'), sourceExtensions,
        source => literals(source).map(literal => literal.text));
  for (const crate of ['kirin_hypha_ffi', 'kirin_measure'])
    visit(path.join(root, 'crates', crate, 'src'), new Set(['.rs']),
          source => [...source.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(match => unescape(match[1])));
  return texts;
}

// A catalog entry, or a kept line, whose English no longer appears in the source is dead. English
// built from two literals ("PAIR " + "SELECT PRE") still counts as present.
export function findStale(root) {
  const texts = allLiteralText(root);
  const present = piece => {
    if (texts.some(text => text.includes(piece))) return true;
    for (let split = 1; split < piece.length; ++split)
      if (texts.some(text => text.includes(piece.slice(0, split)))
          && texts.some(text => text.includes(piece.slice(split))))
        return true;
    return false;
  };
  const stale = [];
  const catalog = catalogEnglish(root);
  for (const english of catalog.english)
    for (const piece of english.split(/%[1-3]|\s+[/·]\s+|\n/).map(part => part.trim()))
      if (piece.length >= 3 && !present(piece)) { stale.push({ kind: 'catalog', english, piece }); break; }
  for (const english of readKept(root))
    if (!present(english)) stale.push({ kind: 'kept', english, piece: english });
  return stale;
}

export function findUntranslated(root) {
  const catalog = catalogEnglish(root);
  const kept = readKept(root);
  const findings = [];
  const visit = (absolute, relative) => {
    for (const entry of fs.readdirSync(absolute, { withFileTypes: true })) {
      const childAbsolute = path.join(absolute, entry.name);
      const childRelative = path.posix.join(relative, entry.name);
      if (entry.isDirectory()) { visit(childAbsolute, childRelative); continue; }
      if (!sourceExtensions.has(path.extname(entry.name))) continue;
      if (offScreen.some(pattern => pattern.test(childRelative))) continue;
      for (const literal of literals(fs.readFileSync(childAbsolute, 'utf8'))) {
        if (/(?:static_assert|jassert|#include|Thread\s*\(|getChildFile|getEnvironmentVariable)\s*\(?\s*[^;]*$/
              .test(literal.context.split('\n').pop()))
          continue;
        if (!looksLikeProse(literal.text) || covered(literal.text, catalog, kept)) continue;
        findings.push({ path: childRelative, line: literal.line, text: literal.text });
      }
    }
  };
  visit(path.join(root, 'juce_shell', 'src'), 'juce_shell/src');
  return findings;
}

function main() {
  const directory = path.dirname(fileURLToPath(import.meta.url));
  const root = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(directory, '..');
  const findings = findUntranslated(root);
  const stale = findStale(root);
  if (findings.length === 0 && stale.length === 0) {
    console.log('screen text contract: PASS (every screen sentence has Japanese or a kept reason)');
    return;
  }
  console.error('screen text contract: FAIL');
  for (const finding of findings)
    console.error(`- ${finding.path}:${finding.line}: no Japanese for ${JSON.stringify(finding.text)}`);
  for (const entry of stale)
    console.error(`- ${entry.kind} entry no longer in the source: ${JSON.stringify(entry.english)}`);
  process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
