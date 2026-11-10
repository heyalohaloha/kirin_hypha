// The public manual is README.md plus the topic pages in docs/manual/. Checks that make claims about
// the public text read all of it, and every relative link and anchor in it must resolve.
import fs from 'node:fs';
import path from 'node:path';

export const MANUAL_DIR = 'docs/manual';

export function publicManualFiles(repoRoot) {
  const pages = fs.readdirSync(path.join(repoRoot, MANUAL_DIR))
    .filter((name) => name.endsWith('.md'))
    .sort()
    .map((name) => `${MANUAL_DIR}/${name}`);
  return ['README.md', ...pages].map((relative) => ({
    relative,
    text: fs.readFileSync(path.join(repoRoot, relative), 'utf8'),
  }));
}

// Lines outside fenced code blocks, with their 1-based line numbers.
function proseLines(text) {
  const lines = [];
  let fenced = false;
  for (const [index, line] of text.split('\n').entries()) {
    if (/^\s*```/.test(line)) { fenced = !fenced; continue; }
    if (!fenced) lines.push({ line, number: index + 1 });
  }
  return lines;
}

// GitHub's heading anchor: rendered text, lower case, punctuation and symbols removed, spaces to hyphens.
export function headingSlug(heading) {
  return heading
    .replace(/^#{1,6}\s+/, '')
    .trim()
    .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/[*`]/g, '')
    .toLowerCase()
    .replace(/[^\p{L}\p{M}\p{N}\s_-]/gu, '')
    .replace(/ /g, '-');
}

export function anchorsOf(text) {
  const anchors = new Set();
  const seen = new Map();
  for (const { line } of proseLines(text)) {
    if (/^#{1,6}\s/.test(line)) {
      const base = headingSlug(line);
      const count = seen.get(base) ?? 0;
      seen.set(base, count + 1);
      anchors.add(count === 0 ? base : `${base}-${count}`);
    }
    for (const match of line.matchAll(/<a (?:id|name)="([^"]+)"/g)) anchors.add(match[1]);
  }
  return anchors;
}

export function linkTargets(text) {
  const targets = [];
  for (const { line, number } of proseLines(text)) {
    for (const match of line.matchAll(/\]\(([^)\s]+)\)|(?:src|href)="([^"]+)"/g)) {
      targets.push({ target: match[1] ?? match[2], line: number });
    }
  }
  return targets;
}

const isExternal = (target) => /^[a-z][a-z0-9+.-]*:/i.test(target);

function resolveTarget(fromFile, target) {
  const [pathPart, fragment] = target.split('#');
  const file = pathPart
    ? path.posix.normalize(path.posix.join(path.posix.dirname(fromFile), pathPart))
    : fromFile;
  return { file: file.replace(/\/$/, ''), fragment };
}

// Repository paths of every local file the public manual links or shows.
export function referencedPaths(files) {
  const paths = new Set();
  for (const { relative, text } of files) {
    for (const { target } of linkTargets(text)) {
      if (!isExternal(target) && !target.startsWith('#')) paths.add(resolveTarget(relative, target).file);
    }
  }
  return paths;
}

export function manualLinkProblems(repoRoot, files = publicManualFiles(repoRoot)) {
  const anchors = new Map(files.map(({ relative, text }) => [relative, anchorsOf(text)]));
  const problems = [];
  for (const { relative, text } of files) {
    for (const { target, line } of linkTargets(text)) {
      if (isExternal(target)) continue;
      const { file, fragment } = resolveTarget(relative, target);
      const where = `${relative}:${line} ${target}`;
      if (file.startsWith('../') || !fs.existsSync(path.join(repoRoot, file))) {
        problems.push(`${where}: missing file`);
      } else if (fragment !== undefined && anchors.has(file) && !anchors.get(file).has(fragment)) {
        problems.push(`${where}: missing anchor`);
      }
    }
  }
  return problems;
}
