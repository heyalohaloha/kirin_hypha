// GitHub Release publication addressed by the created release ID. A draft has no tag ref until it is
// published, so it is never re-resolved by tag; an uncertain create is reconciled, never repeated.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileFact, Checkpoint, readBudgetPolicy, checkFacts, resolveInput } from './hypha_release_contract.mjs';

export const GH_REPO = 'heyalohaloha/kirin_hypha';
const RELEASES = `repos/${GH_REPO}/releases`;
const UPLOADS = `https://uploads.github.com/${RELEASES}`;
const LIST_PAGES = 10; // 1000 releases; a longer history needs operator review, not a guess

const positiveId = value => Number.isSafeInteger(value) && value > 0;
const parse = output => (output ? JSON.parse(output) : null);

async function api(run, endpoint, optional = false) {
  return parse(await run('gh', ['api', endpoint], { capture: true, optional404: optional }));
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

function identity(state) {
  const { version, commit } = state.candidate;
  if (!/^[0-9a-f]{40}$/.test(commit || '')) throw new Error('Release candidate commit must be a full 40-hex SHA');
  return { tag: `v${version}`, commit, name: `Kirin Hypha ${version}` };
}

function own(release, id, want) {
  if (release?.id !== id || release.tag_name !== want.tag || release.target_commitish !== want.commit
      || typeof release.draft !== 'boolean' || release.prerelease) {
    throw new Error('GitHub release ID does not match the frozen tag/commit or is a prerelease');
  }
  return release;
}

async function byId(run, id, want) {
  return own(await api(run, `${RELEASES}/${id}`), id, want);
}

// Authenticated listing: drafts are visible to push-access callers, unlike the tag endpoint.
async function sameTag(run, tag) {
  const found = [];
  for (let page = 1; page <= LIST_PAGES; page++) {
    const batch = await api(run, `${RELEASES}?per_page=100&page=${page}`);
    if (!Array.isArray(batch)) throw new Error('GitHub release listing is not an array');
    found.push(...batch.filter(r => r.tag_name === tag));
    if (batch.length < 100) return found;
  }
  throw new Error('GitHub release listing exceeds the bounded scan; operator review required');
}

async function locate(state, run, save, want) {
  const record = state.githubRelease;
  if (record && (record.tag !== want.tag || record.commit !== want.commit)) {
    throw new Error('Saved GitHub release record belongs to another tag/commit');
  }
  if (record && record.id !== undefined) {
    if (!positiveId(record.id)) throw new Error('Saved GitHub release ID is malformed');
    return record.id;
  }
  const candidates = await sameTag(run, want.tag);
  if (candidates.some(r => r.target_commitish !== want.commit)) {
    throw new Error('Foreign GitHub release: the same tag targets another commit');
  }
  if (candidates.length > 1) throw new Error('Ambiguous GitHub releases share this tag; operator review required');
  const [found] = candidates;
  if (record && !found) {
    throw new Checkpoint('GitHub release create result is unknown and no matching draft is visible; '
      + 'operator review required, no automatic re-create');
  }
  if (!found) return null;
  if (!positiveId(found.id)) throw new Error('Listed GitHub release ID is malformed');
  if (found.name !== want.name || found.prerelease || (record && found.draft !== true)) {
    throw new Error('Same-tag release is not the requested release; operator review required');
  }
  state.githubRelease = { ...(record || want), id: found.id, status: 'created' }; save();
  return found.id;
}

async function create(state, run, save, want) {
  readBudgetPolicy(state.root);
  state.publicationStarted = true;
  state.githubRelease = { ...want, status: 'requested', requestedAt: new Date().toISOString() };
  save(); // a lost create response is reconciled by listing on resume
  const created = parse(await run('gh', ['api', RELEASES, '--method', 'POST', '-f', `tag_name=${want.tag}`,
    '-f', `target_commitish=${want.commit}`, '-f', `name=${want.name}`, '-F', 'draft=true',
    '-F', 'prerelease=false', '-F', `body=@${resolveInput(state, state.inputs.notes)}`], { capture: true }));
  if (!positiveId(created?.id) || created.draft !== true || created.name !== want.name) {
    throw new Error('GitHub release create response is not the requested draft');
  }
  own(created, created.id, want);
  state.githubRelease = { ...state.githubRelease, id: created.id, status: 'created' }; save();
  return created.id;
}

// Every asset must belong to the frozen set before anything is uploaded or a release is reused.
function onlyFrozenAssets(release, facts) {
  const names = new Set(facts.map(f => path.basename(f.path)));
  if ((release.assets || []).some(a => !names.has(a.name))) {
    throw new Error('GitHub release carries assets outside the frozen distribution set');
  }
}

function matchAsset(release, fact) {
  const same = release.assets?.filter(a => a.name === path.basename(fact.path)) || [];
  if (same.length !== 1 || same[0].size !== fact.bytes || !positiveId(same[0].id)
      || (same[0].state && same[0].state !== 'uploaded')) {
    throw new Error('GitHub Release asset set/size mismatch; no overwrite');
  }
  if (same[0].digest && same[0].digest !== `sha256:${fact.sha256}`) {
    throw new Error('GitHub Release asset digest mismatch; no overwrite');
  }
  return same[0];
}

async function uploadMissing(state, run, release, facts) {
  // Existing same-name assets must already be the frozen bytes; only absent ones are uploaded (no clobber).
  const missing = facts.filter(f => !release.assets?.some(a => a.name === path.basename(f.path)));
  for (const fact of facts) if (!missing.includes(fact)) matchAsset(release, fact);
  for (const fact of missing) {
    const name = path.basename(fact.path);
    readBudgetPolicy(state.root);
    const uploaded = parse(await run('gh', ['api', `${UPLOADS}/${release.id}/assets?name=${encodeURIComponent(name)}`,
      '--method', 'POST', '-H', 'Content-Type: application/octet-stream', '--input', fact.path], { capture: true }));
    if (uploaded?.name !== name || uploaded.size !== fact.bytes) throw new Error('GitHub Release upload response mismatch');
  }
}

async function verifyReleaseBytes(run, release, facts) {
  const assets = facts.map(fact => matchAsset(release, fact));
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-release-readback-'));
  try {
    for (const [i, fact] of facts.entries()) {
      const file = path.join(temporary, String(assets[i].id));
      // The runner writes the raw asset bytes into this new owned file.
      await run('gh', ['api', `${RELEASES}/assets/${assets[i].id}`, '-H', 'Accept: application/octet-stream'],
        { stdoutFile: file });
      const actual = fileFact(file);
      if (actual.sha256 !== fact.sha256 || actual.bytes !== fact.bytes) {
        throw new Error('Downloaded GitHub Release bytes do not match the frozen distribution set');
      }
    }
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true }); // owned mkdtemp only
  }
}

export async function publishGithub(state, run, save) {
  if (state.stages.provenance?.status !== 'PASS' || !state.stages.provenance.publicFacts?.length) {
    throw new Checkpoint('Verified exact-release Corresponding Source delivery required before publication');
  }
  checkFacts(state.stages.provenance.facts);
  const facts = [...state.stages.packages.facts, ...state.stages.provenance.publicFacts];
  const want = identity(state);
  await checkTag(state, run, { optional: true });
  // Candidate must already exist in public history; no implicit product push/branch fallback.
  const remoteCommit = await api(run, `repos/${GH_REPO}/commits/${state.candidate.commit}`);
  if (remoteCommit.sha !== state.candidate.commit) throw new Error('Product commit is not available in public history');
  const id = (await locate(state, run, save, want)) ?? (await create(state, run, save, want));
  let release = await byId(run, id, want);
  onlyFrozenAssets(release, facts);
  if (release.draft) {
    if (state.githubRelease.status === 'published') throw new Error('Published GitHub release returned to draft; operator review required');
    state.publicationStarted = true; save(); // also covers an imported/interrupted draft
    await uploadMissing(state, run, release, facts);
    release = await byId(run, id, want);
    onlyFrozenAssets(release, facts);
    await verifyReleaseBytes(run, release, facts);
    readBudgetPolicy(state.root);
    state.githubRelease = { ...state.githubRelease, status: 'publishing' }; save();
    await run('gh', ['api', `${RELEASES}/${id}`, '--method', 'PATCH', '-F', 'draft=false'], { capture: true });
    release = await byId(run, id, want);
  } else await verifyReleaseBytes(run, release, facts);
  if (release.draft || release.prerelease) throw new Error('Release is not public stable');
  await checkTag(state, run);
  state.githubRelease = { ...state.githubRelease, status: 'published' }; save();
  return { url: release.html_url, releaseId: release.id };
}
