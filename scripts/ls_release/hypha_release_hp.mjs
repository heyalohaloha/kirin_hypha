// Publication uses the HP repository's existing clean staged deployment, never a direct --prod.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileFact, hash, Checkpoint, readBudgetPolicy, checkFacts } from './hypha_release_contract.mjs';

export const GH_REPO = 'heyalohaloha/kirin_hypha';
export const HP_FILES = ['hypha.html', 'ja/hypha.html', 'scripts/__tests__/hypha-entry-links.test.mjs'];
const BASE = `https://github.com/${GH_REPO}/releases`;

export function updateHpDocuments(documents, version, date) {
  if (!/^\d+\.\d+\.\d+$/.test(version) || !/^\d{4}-\d{2}-\d{2}$/.test(date)) throw new Error('Invalid release version/date');
  const versions = documents.slice(0, 2).map(html =>
    html.match(/id="hypha-download-title"[^>]*>Hypha (\d+\.\d+\.\d+)</)?.[1]);
  if (!versions[0] || versions[0] !== versions[1]) throw new Error('HP language version mismatch');
  const old = versions[0];
  const oldEscaped = old.replaceAll('.', '\\.');
  const result = documents.map(text => text.replaceAll(old, version)
    .replaceAll(oldEscaped, version.replaceAll('.', '\\.')));
  for (let i = 0; i < 2; i++) {
    const html = result[i];
    const downloads = [...html.matchAll(/https:\/\/github\.com\/heyalohaloha\/kirin_hypha\/releases\/download\/v([^/]+)\/([^"\s<]+)/g)];
    const expected = [`Kirin-Hypha-${version}-macOS-Universal.pkg`, `Kirin-Hypha-${version}-Windows-x64-Setup.exe`];
    if (downloads.length !== 2 || downloads.some((m, n) => m[1] !== version || m[2] !== expected[n])) {
      throw new Error('HP must retain the PKG + signed Windows EXE primary links');
    }
    const oldDate = html.match(/<time datetime="(\d{4}-\d{2}-\d{2})">\1<\/time>/)?.[1];
    if (!oldDate) throw new Error('HP release date markup changed; review required');
    result[i] = html.replace(`<time datetime="${oldDate}">${oldDate}</time>`, `<time datetime="${date}">${date}</time>`)
      .replace('macOS 12+ (VST3 + Audio Unit)', 'macOS 12+ (VST3 + Audio Unit + AAX)')
      .replace('Windows 10/11 (VST3)', 'Windows 10/11 (VST3 + AAX)')
      .replace('Download for Windows VST3<', 'Download for Windows VST3 + AAX<')
      .replace('Windows VST3版をダウンロード<', 'Windows VST3 + AAX版をダウンロード<');
    if (!result[i].includes('Audio Unit + AAX') || !result[i].includes('Windows 10/11 (VST3 + AAX)')) {
      throw new Error('HP format copy changed; review required');
    }
    result[2] = result[2].replaceAll(oldDate, date);
  }
  return result;
}

export async function hpPreflight(state, run, { allowPlanned = false, allowUnpushed = false } = {}) {
  const root = state.inputs.hpRoot;
  if (!root || !fs.existsSync(path.join(root, 'README_DEPLOY.md'))) throw new Checkpoint('HP repository with standard deploy runbook required');
  fs.readFileSync(path.join(root, 'README_DEPLOY.md'), 'utf8');
  if (!fs.existsSync(path.join(root, '.vercel/project.json'))) throw new Checkpoint('Existing authorized HP Vercel project link required (no auto-link)');
  const projectFile = path.join(root, '.vercel/project.json');
  if (fileFact(projectFile).sha256 !== state.inputs.hpProjectSha256) throw new Checkpoint('HP Vercel project link differs from the pinned authorized link');
  if ((await run('git', ['branch', '--show-current'], { cwd: root, capture: true })).trim() !== 'main') {
    throw new Checkpoint('HP publication requires main');
  }
  const head = (await run('git', ['rev-parse', 'HEAD'], { cwd: root, capture: true })).trim();
  const expected = state.hpCommit || state.inputs.hpBaseCommit;
  if (!expected || head !== expected) throw new Checkpoint('HP source commit differs from the pinned baseline/publication commit');
  const dirty = (await run('git', ['status', '--porcelain'], { cwd: root, capture: true })).trim();
  if (dirty) {
    if (!allowPlanned || !state.hpPlanned) throw new Checkpoint('HP checkout is dirty; preserve other-session changes, do not publish them');
    const changed = (await run('git', ['diff', '--name-only', 'HEAD'], { cwd: root, capture: true })).trim().split('\n').filter(Boolean);
    const untracked = (await run('git', ['ls-files', '--others', '--exclude-standard'], { cwd: root, capture: true })).trim();
    if (untracked || changed.some(f => !HP_FILES.includes(f))) throw new Error('HP contains changes outside this publication');
    for (const f of state.hpPlanned) if (fileFact(path.join(root, f.path)).sha256 !== f.sha256) {
      throw new Error('Planned HP update changed; no automatic overwrite');
    }
  }
  const remote = (await run('git', ['ls-remote', 'origin', 'refs/heads/main'], { cwd: root, capture: true })).split(/\s/)[0];
  if (remote !== head && !(allowUnpushed && remote === state.inputs.hpBaseCommit)) {
    throw new Checkpoint('HP HEAD is not current origin/main');
  }
  // Existing linked context only. Never resolve by a guessed directory/project or auto-link.
  const project = JSON.parse(fs.readFileSync(projectFile, 'utf8'));
  const inspected = await run('vercel', ['project', 'inspect', '--non-interactive'], { cwd: root, capture: true, stderr: true });
  if (!inspected.includes(project.projectName) || !inspected.includes(project.projectId)) {
    throw new Checkpoint('Resolved Vercel project does not match the authorized project name/ID');
  }
  updateHpDocuments(HP_FILES.map(file => fs.readFileSync(path.join(root, file), 'utf8')),
    state.candidate.version, state.inputs.date);
}

async function api(run, endpoint, optional = false) {
  const output = await run('gh', ['api', endpoint], { capture: true, optional404: optional });
  return output ? JSON.parse(output) : null;
}

export async function checkTag(state, run, { optional = false } = {}) {
  let ref = await api(run, `repos/${GH_REPO}/git/ref/tags/v${state.candidate.version}`, optional);
  if (!ref) return;
  for (let depth = 0; depth < 5 && ref.object?.type === 'tag'; depth++) {
    ref = await api(run, `repos/${GH_REPO}/git/tags/${ref.object.sha}`);
  }
  if (ref.object?.type !== 'commit' || ref.object.sha !== state.candidate.commit) {
    throw new Error('Release tag does not resolve to the frozen exact commit');
  }
}

async function verifyReleaseBytes(state, run, release, facts) {
  for (const fact of facts) {
    const name = path.basename(fact.path);
    const assets = release.assets?.filter(a => a.name === name) || [];
    if (assets.length !== 1 || assets[0].size !== fact.bytes) throw new Error('GitHub Release asset set/size mismatch');
    if (assets[0].digest && assets[0].digest !== `sha256:${fact.sha256}`) throw new Error('GitHub Release asset digest mismatch');
  }
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-release-readback-'));
  try {
    await run('gh', ['release', 'download', `v${state.candidate.version}`, '--repo', GH_REPO,
      '--dir', temporary, ...facts.flatMap(f => ['--pattern', path.basename(f.path)])]);
    for (const fact of facts) {
      if (fileFact(path.join(temporary, path.basename(fact.path))).sha256 !== fact.sha256) {
        throw new Error('Downloaded GitHub Release bytes do not match the frozen distribution set');
      }
    }
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true }); // owned mkdtemp only
  }
}

export async function publishGithub(state, run, save) {
  const tag = `v${state.candidate.version}`;
  if (state.stages.provenance?.status !== 'PASS' || !state.stages.provenance.publicFacts?.length) {
    throw new Checkpoint('Verified exact-release Corresponding Source delivery required before publication');
  }
  checkFacts(state.stages.provenance.facts);
  const facts = [...state.stages.packages.facts, ...state.stages.provenance.publicFacts];
  await checkTag(state, run, { optional: true });
  // Candidate must already exist in public history; no implicit product push/branch fallback.
  const remoteCommit = await api(run, `repos/${GH_REPO}/commits/${state.candidate.commit}`);
  if (remoteCommit.sha !== state.candidate.commit) throw new Error('Product commit is not available in public history');
  let release = await api(run, `repos/${GH_REPO}/releases/tags/${tag}`, true);
  if (!release) {
    readBudgetPolicy(state.root);
    state.publicationStarted = true; save();
    await run('gh', ['release', 'create', tag, '--repo', GH_REPO, '--target', state.candidate.commit,
      '--draft', '--title', `Kirin Hypha ${state.candidate.version}`, '--notes-file', state.inputs.notes]);
    release = await api(run, `repos/${GH_REPO}/releases/tags/${tag}`);
  }
  if (release.draft) {
    if (release.target_commitish !== state.candidate.commit) throw new Error('Foreign draft: target commit mismatch');
    state.publicationStarted = true; save(); // also covers an imported/interrupted draft
    for (const fact of facts) {
      if (!release.assets?.some(a => a.name === path.basename(fact.path))) {
        readBudgetPolicy(state.root);
        await run('gh', ['release', 'upload', tag, fact.path, '--repo', GH_REPO]); // no --clobber
      }
    }
    release = await api(run, `repos/${GH_REPO}/releases/tags/${tag}`);
    await verifyReleaseBytes(state, run, release, facts);
    readBudgetPolicy(state.root);
    await run('gh', ['release', 'edit', tag, '--repo', GH_REPO, '--draft=false']);
    release = await api(run, `repos/${GH_REPO}/releases/tags/${tag}`);
  } else await verifyReleaseBytes(state, run, release, facts);
  if (release.draft || release.prerelease) throw new Error('Release is not public stable');
  await checkTag(state, run);
  return { url: release.html_url, releaseId: release.id };
}

export async function publicDownloadFacts(state, facts, fetcher = fetch) {
  const measured = [];
  for (const fact of facts) {
    const url = `${BASE}/download/v${state.candidate.version}/${encodeURIComponent(path.basename(fact.path))}`;
    const response = await fetcher(url, { signal: AbortSignal.timeout(120000) });
    if (!response.ok || !response.body) throw new Error('Public artifact download failed');
    const crypto = await import('node:crypto');
    const sha = crypto.createHash('sha256'); let bytes = 0;
    for await (const chunk of response.body) {
      sha.update(chunk); bytes += chunk.length;
      if (bytes > fact.bytes) throw new Error('Public artifact exceeded frozen size');
    }
    if (bytes !== fact.bytes || sha.digest('hex') !== fact.sha256) throw new Error('Public artifact bytes/hash mismatch');
    measured.push({ url, bytes, sha256: fact.sha256 });
  }
  return measured;
}

export async function verifyPublicHp(state, fetcher = fetch) {
  const base = 'https://kirinmastering.com';
  for (const [route, lang] of [['/hypha', 'en'], ['/ja/hypha', 'ja']]) {
    const response = await fetcher(`${base}${route}?hypha_verify=${state.candidate.id}`, { signal: AbortSignal.timeout(30000) });
    if (!response.ok) throw new Error('HP public route failed');
    const html = await response.text();
    if (!html.includes(`<html lang="${lang}">`) || !html.includes(`Hypha ${state.candidate.version}</`)
        || !html.includes('Audio Unit + AAX') || !html.includes('Windows 10/11 (VST3 + AAX)')) {
      throw new Error('HP public language/version/format mismatch');
    }
    for (const suffix of ['macOS-Universal.pkg', 'Windows-x64-Setup.exe']) {
      if (!html.includes(`${BASE}/download/v${state.candidate.version}/Kirin-Hypha-${state.candidate.version}-${suffix}`)) {
        throw new Error('HP public download link mismatch');
      }
    }
  }
  const source = await fetcher(`${base}/deployment-source.txt?hypha_verify=${state.candidate.id}`, { signal: AbortSignal.timeout(30000) });
  if (!source.ok || (await source.text()).trim() !== state.hpCommit) throw new Error('HP deployment source commit mismatch');
}

export async function publishHp(state, run, save, fetcher = fetch) {
  const root = state.inputs.hpRoot;
  if (state.hpCommit) {
    try { await verifyPublicHp(state, fetcher); return { hpCommit: state.hpCommit, reused: true }; } catch { /* verify before a necessary resume */ }
  }
  if (state.hpPlanned && !state.hpCommit) {
    const head = (await run('git', ['rev-parse', 'HEAD'], { cwd: root, capture: true })).trim();
    if (head !== state.inputs.hpBaseCommit) {
      const parent = (await run('git', ['rev-parse', 'HEAD^'], { cwd: root, capture: true })).trim();
      const subject = (await run('git', ['log', '-1', '--pretty=%s'], { cwd: root, capture: true })).trim();
      const changed = (await run('git', ['diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD'], { cwd: root, capture: true })).trim().split('\n');
      if (parent !== state.inputs.hpBaseCommit || subject !== `Publish Hypha ${state.candidate.version} (${state.candidate.id})`
          || changed.some(f => !HP_FILES.includes(f))) throw new Error('HP commit is not this interrupted publication');
      for (const f of state.hpPlanned) if (fileFact(path.join(root, f.path)).sha256 !== f.sha256) throw new Error('HP commit bytes mismatch');
      state.hpCommit = head; save();
    }
  }
  await hpPreflight(state, run, { allowPlanned: !!state.hpPlanned, allowUnpushed: !!state.hpCommit });
  if (!state.hpCommit) {
    const changes = updateHpDocuments(HP_FILES.map(f => fs.readFileSync(path.join(root, f), 'utf8')),
      state.candidate.version, state.inputs.date);
    const planned = changes.map((bytes, i) => ({ path: HP_FILES[i], sha256: hash(bytes) }));
    state.publicationStarted = true;
    if (!state.hpPlanned) {
      state.hpPlanned = planned; save();
      for (let i = 0; i < HP_FILES.length; i++) fs.writeFileSync(path.join(root, HP_FILES[i]), changes[i]);
    }
    await run('npm', ['test'], { cwd: root });
    const changed = (await run('git', ['diff', '--name-only', 'HEAD'], { cwd: root, capture: true })).trim().split('\n').filter(Boolean);
    if (changed.some(f => !HP_FILES.includes(f))) throw new Error('HP acquired unrelated changes; publication stopped');
    const indexed = (await run('git', ['diff', '--cached', '--name-only'], { cwd: root, capture: true })).trim().split('\n').filter(Boolean);
    if (indexed.some(f => !HP_FILES.includes(f))) {
      throw new Error('HP index contains other changes; do not commit another session\'s changes');
    }
    for (const fact of state.hpPlanned) {
      if (fileFact(path.join(root, fact.path)).sha256 !== fact.sha256) throw new Error('HP source changed during tests');
    }
    const beforePush = (await run('git', ['ls-remote', 'origin', 'refs/heads/main'], { cwd: root, capture: true })).split(/\s/)[0];
    if (beforePush !== state.inputs.hpBaseCommit) throw new Error('HP remote advanced during tests');
    await run('git', ['add', '--', ...HP_FILES], { cwd: root });
    await run('git', ['commit', '-m', `Publish Hypha ${state.candidate.version} (${state.candidate.id})`], { cwd: root });
    state.hpCommit = (await run('git', ['rev-parse', 'HEAD'], { cwd: root, capture: true })).trim(); save();
  }
  const remote = (await run('git', ['ls-remote', 'origin', 'refs/heads/main'], { cwd: root, capture: true })).split(/\s/)[0];
  if (remote !== state.hpCommit) {
    if (remote !== state.inputs.hpBaseCommit) throw new Error('HP remote advanced; no force push');
    readBudgetPolicy(state.root);
    await run('git', ['push', 'origin', 'HEAD:refs/heads/main'], { cwd: root });
  }
  await run('bash', ['scripts/deploy-production-clean.sh'], { cwd: root });
  await verifyPublicHp(state, fetcher);
  return { hpCommit: state.hpCommit, pageFacts: HP_FILES.map(f => fileFact(path.join(root, f))) };
}
