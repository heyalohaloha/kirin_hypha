import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

// A pull request that changes only text the build and the hosts never read skips the native,
// Rust and host jobs. Everything else, and every push or manual run, is full. The release
// tooling accepts a run as a candidate only when every phase actually ran, so a skipped run
// can never become a release.
const LIGHT = [
  /^docs\//,
  /^[^/]+\.md$/,
  /^\.github\/ISSUE_TEMPLATE\//,
  /^scripts\/acceptance\/[^/]+\.md$/,
];

export function classify(paths) {
  if (!Array.isArray(paths) || !paths.length) return { full: true, reason: 'no changed paths' };
  const heavy = paths.find(path => typeof path !== 'string' || !LIGHT.some(rule => rule.test(path)));
  return heavy === undefined ? { full: false, reason: 'text only' } : { full: true, reason: heavy };
}

export function scope(env = process.env, git = args => execFileSync('git', args, { encoding: 'utf8' })) {
  if (env.EVENT_NAME !== 'pull_request') return { full: true, reason: `event ${env.EVENT_NAME || 'unknown'}` };
  const sha = /^[0-9a-f]{40}$/;
  if (!sha.test(env.BASE_SHA || '') || !sha.test(env.HEAD_SHA || '')) return { full: true, reason: 'missing pull request range' };
  try {
    return classify(git(['diff', '--name-only', '--no-renames', `${env.BASE_SHA}...${env.HEAD_SHA}`]).split('\n').filter(Boolean));
  } catch (error) {
    // Any doubt runs everything.
    return { full: true, reason: `diff failed: ${error.message.split('\n')[0]}` };
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const result = scope();
  console.error(`scope: ${result.full ? 'full' : 'text only'} (${result.reason})`);
  console.log(`full=${result.full}`);
}
