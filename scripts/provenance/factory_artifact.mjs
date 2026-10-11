// Match the current-factory import predicate through promotion and the public coordinator.
import path from 'node:path';
import { fileFact, readJson } from '../ls_release/hypha_release_contract.mjs';

const WORKFLOW = '.github/workflows/hypha-windows-signing.yml';
const NAME = 'Hypha Windows signing factory';
const sha = text => /^[0-9a-f]{40}$/.test(text || '');
export function assertFactoryRun(run, commit, id, repository) {
  if (!sha(commit) || String(run.id) !== String(id) || !/^\d+$/.test(String(id))
      || run.head_sha !== commit || run.head_repository?.full_name !== repository
      || run.path !== WORKFLOW || run.name !== NAME || run.event !== 'workflow_dispatch'
      || run.status !== 'completed' || run.conclusion !== 'success') {
    throw new Error('Factory run differs from the pinned reviewed factory/dispatch/workflow');
  }
}
export function selectFactoryArtifact(artifacts, name, run) {
  const rows = artifacts.filter(a => a.name === name), row = rows[0];
  if (rows.length !== 1 || row.expired !== false || !/^\d+$/.test(String(row.id))
      || !/^sha256:[0-9a-f]{64}$/.test(row.digest || '')
      || String(row.workflow_run?.id) !== String(run.id) || row.workflow_run?.head_sha !== run.head_sha) {
    throw new Error('Factory artifact is missing, ambiguous, expired or has a different identity/digest');
  }
  return row;
}
export function assertInstallerJobs(jobs, selected) {
  const names = ['Hypha unsigned Windows VST3','Hypha signed Windows installer','Hypha validated Windows installer promotion'];
  if (!names.slice(1).includes(selected)) throw new Error('Unknown candidate transition job');
  for (const name of names) {
    const rows = jobs.filter(j => j.name === name);
    if (rows.length !== 1 || rows[0].status !== 'completed'
        || rows[0].conclusion !== (name === selected ? 'success' : 'skipped')) {
      throw new Error('Factory candidate/promotion job separation is not proved');
    }
  }
}
async function qualifiedArtifact(run, id, commit, repository, name, job) {
  if (!/^\d+$/.test(String(id || '')) || !sha(commit)) throw new Error('Invalid factory run ID/commit');
  const base = `repos/${repository}/actions/runs/${id}`;
  const api = async endpoint => JSON.parse(await run('gh', ['api', endpoint], { capture: true }));
  const record = await api(base);
  assertFactoryRun(record, commit, id, repository);
  assertInstallerJobs((await api(`${base}/jobs?per_page=100`)).jobs || [], job);
  const artifacts = [];
  for (let page = 1; ; page++) {
    const rows = (await api(`${base}/artifacts?per_page=100&page=${page}`)).artifacts || [];
    artifacts.push(...rows); if (rows.length < 100) break;
  }
  return { run: record, artifact: selectFactoryArtifact(artifacts, name, record) };
}
export function requireFactoryInputs(state) {
  const { windowsFactoryCommit: commit, windowsPromotionRun: id, windowsArtifactArchive: archive, windowsFactoryRepository: repository } = state.inputs;
  if (!sha(commit) || !/^\d+$/.test(String(id || '')) || !archive || !/^[A-Za-z0-9][A-Za-z0-9-]{0,38}\/[A-Za-z0-9._-]+$/.test(repository || '')) {
    throw new Error('Pin reviewed Windows factory repository/commit, promotion run and retained artifact ZIP in the private profile');
  }
  return { commit, id, archive, repository };
}
export async function verifyFactoryDelivery(state, run, manifest, directory) {
  const { commit, id, archive, repository } = requireFactoryInputs(state);
  const full = await qualifiedArtifact(run, id, commit, repository, 'KirinHypha-Windows-signed-full', 'Hypha validated Windows installer promotion');
  const archiveFile = path.resolve(state.root, archive), archiveFact = fileFact(archiveFile);
  if (`sha256:${archiveFact.sha256}` !== full.artifact.digest) throw new Error('Signed-full artifact archive digest mismatch');
  // Verify the entire locally delivered file set against the exact GitHub archive; do not trust its sidecar alone.
  await run('python3', ['scripts/provenance/artifact_archive.py', archiveFile, directory], { capture: true });
  const origin = manifest.factoryImport, candidate = origin?.candidate, promotion = origin?.promotion;
  if (promotion?.factoryCommit !== commit || promotion.repository !== repository || promotion.event !== 'workflow_dispatch'
      || promotion.workflow !== WORKFLOW || String(promotion.runId) !== String(id)
      || candidate?.factoryCommit !== commit || candidate.repository !== repository
      || candidate.event !== 'workflow_dispatch' || candidate.workflow !== WORKFLOW
      || candidate.artifactName !== 'KirinHypha-Windows-signed-candidate'
      || candidate.archiveSha256 !== candidate.digest?.slice(7)) throw new Error('Promotion factory/artifact origin mismatch');
  const signed = await qualifiedArtifact(run, candidate.runId, commit, repository, candidate.artifactName, 'Hypha signed Windows installer');
  if (String(signed.artifact.id) !== String(candidate.artifactId) || signed.artifact.digest !== candidate.digest) {
    throw new Error('Signed candidate artifact digest/ID mismatch');
  }
  const url = `https://github.com/${repository}/actions/runs/${signed.run.id}`;
  if (manifest.signing.workflow_run !== url || manifest.external_validation.candidate_workflow_run !== url) {
    throw new Error('Installer manifest refers to a different signed candidate run');
  }
  if (origin.candidateManifest?.path !== 'provenance/candidate-manifest.json') throw new Error('Original candidate manifest path is invalid');
  const originalFile = path.join(directory, origin.candidateManifest.path);
  if (fileFact(originalFile).sha256 !== origin.candidateManifest.sha256) throw new Error('Original candidate manifest changed');
  const original = readJson(originalFile), promoted = structuredClone(manifest);
  if (original.external_validation?.status !== 'pending' || original.distribution?.public_ready !== false) {
    throw new Error('Original candidate was not pending validation');
  }
  // Promotion may only add provenance and replace the retained host-validation status.
  delete promoted.factoryImport; promoted.external_validation = original.external_validation;
  promoted.distribution.public_ready = false;
  const canonical = value => value && typeof value === 'object' ? (Array.isArray(value) ? value.map(canonical)
    : Object.fromEntries(Object.keys(value).sort().map(k => [k, canonical(value[k])]))) : value;
  if (JSON.stringify(canonical(original)) !== JSON.stringify(canonical(promoted))) throw new Error('Promotion changed candidate payload/qualification metadata');
  return [archiveFact, fileFact(originalFile)];
}
