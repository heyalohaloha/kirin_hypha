// Publication uses the HP repository's existing clean staged deployment, never a direct --prod.
import fs from 'node:fs';
import path from 'node:path';
import { fileFact, hash, Checkpoint, readBudgetPolicy, checkFacts } from './hypha_release_contract.mjs';
import { GH_REPO } from './hypha_release_github.mjs';
import { HP_FILES, updateHpDocuments, makeHpRecord, assertHpPage, HP_TARGETS, verifyHpAsset,
  assertHpPublicationDate } from './hypha_release_hp_data.mjs';
export { HP_FILES, updateHpDocuments } from './hypha_release_hp_data.mjs';
export { GH_REPO, checkTag, publishGithub } from './hypha_release_github.mjs';
const BASE = `https://github.com/${GH_REPO}/releases`;

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
  checkFacts(state.stages.packages?.facts);
  assertHpPublicationDate(state.inputs.date);
  updateHpDocuments(HP_FILES.map(file => fs.readFileSync(path.join(root, file), 'utf8')),
    state.candidate.version, state.inputs.date, state.stages.packages?.facts);
}

export async function publicDownloadFacts(state, facts, fetcher = fetch) {
  const measured = [];
  for (const fact of facts) {
    const url = `${BASE}/download/v${state.candidate.version}/${encodeURIComponent(path.basename(fact.path))}`;
    measured.push(await verifyHpAsset(fetcher, url, fact));
  }
  return measured;
}

export async function verifyPublicHp(state, fetcher = fetch) {
  const facts = state.stages.packages?.facts;
  checkFacts(facts);
  const record = makeHpRecord(state.candidate.version, state.inputs.date, facts);
  const base = 'https://kirinmastering.com';
  for (const [route, lang] of [['/hypha', 'en'], ['/ja/hypha', 'ja']]) {
    const response = await fetcher(`${base}${route}?hypha_verify=${state.candidate.id}`, { signal: AbortSignal.timeout(30000) });
    if (!response.ok) throw new Error('HP public route failed');
    const html = await response.text(); assertHpPage(html, lang, record);
    if (!html.includes('Audio Unit + AAX') || !html.includes('Windows 10/11 (VST3 + AAX)')) {
      throw new Error('HP public format mismatch');
    }
  }
  for (const target of HP_TARGETS) {
    const url = `${base}/downloads/kirin-hypha/${target}`;
    const redirect = await fetcher(url, { redirect: 'manual', signal: AbortSignal.timeout(30000) });
    if (redirect.status !== 302 || redirect.headers.get('location') !== record.downloads[target].url
        || redirect.headers.get('cache-control') !== 'no-store') {
      throw new Error('HP public installer redirect/version/cache mismatch');
    }
    await verifyHpAsset(fetcher, url, record.downloads[target]);
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
      state.candidate.version, state.inputs.date, state.stages.packages?.facts);
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
