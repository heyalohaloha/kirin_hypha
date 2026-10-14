// Disposable cross-runtime fixture. Only inert files and mocked GitHub reads; no install/signing.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { manifestFor, VERSION } from '../windows/build-installer.mjs';
import { verifyFactoryDelivery } from './factory_artifact.mjs';
import { fileFact } from '../ls_release/hypha_release_contract.mjs';
import { writeSourceZip } from './source_zip_writer.mjs';

export function createFixture(directory, repository) {
  const installer = path.join(directory, `Kirin-Hypha-${VERSION}-Windows-x64-Setup.exe`);
  const binary = path.join(directory, 'inert-payload.bin');
  fs.writeFileSync(binary, 'KirinHyphaUpdateKeySha256=disabled;');
  const aaxManifest = {source:{commit:'a'.repeat(40),b_number:'B-1',state:'clean'},
    release:{mode:'native-only',kimera_embedded:false,native_only:true,audio_suite_enabled:false}};
  const aaxFile = path.join(directory,`Kirin-Hypha-${VERSION}-Windows-x64-AAX.json`);
  fs.writeFileSync(aaxFile,JSON.stringify(aaxManifest));
  const payload = format => ['PRE','POST'].map(role => ({role,name:`Kirin Hypha ${role}.${format}`,binary}));
  const manifest = manifestFor({ opts: { commit: 'a'.repeat(40), bNumber: 'B-1', signing: 'signed',
    runUrl: 'https://github.com/heyalohaloha/kirin_hypha/actions/runs/100', externalValidation: 'pending', updatePublicKey: '',
    unsignedBuild:{factoryCommit:'a'.repeat(40),factoryRun:'122',ciRun:'100',binaries:payload('vst3')} },
    installer, payloadRecords: payload('vst3'), aaxPayloadRecords:payload('aaxplugin'),
    aaxProvenance:{manifest:aaxManifest,manifestSha256:fileFact(aaxFile).sha256} });
  manifest.signing.workflow_run = `https://github.com/${repository}/actions/runs/123`;
  // Unknown retained fields must also survive; strings are never interpreted as dates.
  manifest.retainedFixture = { timestamps: ['2026-10-11T05:00:00.0000000Z', '2026-10-11T14:00:00.1234560+09:00',
    '2026-10-11T05:00:00.1234567Z'], empty: [], one: [1], nested: { nullable: null, yes: true, no: false, number: 1.25 } };
  fs.writeFileSync(installer + '.json', JSON.stringify(manifest, null, 2) + '\n');
}
export async function verifyFixture(directory) {
  const installer = path.join(directory, `Kirin-Hypha-${VERSION}-Windows-x64-Setup.exe`);
  const manifestFile = installer + '.json';
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8').replace(/^\uFEFF/, ''));
  const original = JSON.parse(fs.readFileSync(path.join(directory, 'provenance/candidate-manifest.json'), 'utf8').replace(/^\uFEFF/, ''));
  assert.equal(original.ci_validation.verified_at, '2026-10-11T05:00:00.1234560Z');
  assert.equal(manifest.ci_validation.verified_at, original.ci_validation.verified_at);
  assert.equal(manifest.generated_at, original.generated_at);
  assert.deepEqual(manifest.retainedFixture, original.retainedFixture);
  assert.ok(original.ci_validation.prior_public_upgrade);
  assert.equal(original.installer.aax_payload.length,2);
  assert.equal(original.distribution.aax_included,true);
  assert.ok(original.source.producer);
  const { candidate, promotion } = manifest.factoryImport;
  const archive = path.join(directory, '../coordinator-signed-full.zip');
  const files = fs.readdirSync(directory, { recursive: true }).filter(name => fs.statSync(path.join(directory,name)).isFile())
    .map(name => ({ archive: name.split(path.sep).join('/'), file: path.join(directory,name) }));
  writeSourceZip(archive, files);
  let fullDigest = `sha256:${fileFact(archive).sha256}`;
  const state = { root: directory, inputs: { windowsFactoryRepository: candidate.repository,
    windowsFactoryCommit: candidate.factoryCommit, windowsPromotionRun: String(promotion.runId), windowsArtifactArchive: archive } };
  const invoke = async (tool, args) => {
    if (tool === 'python3') return execFileSync(tool,args,{ cwd: path.resolve(import.meta.dirname,'../..'),encoding:'utf8' });
    assert.equal(tool, 'gh'); assert.equal(args[0], 'api');
    const full = args[1].includes(`/runs/${promotion.runId}`), id = full ? promotion.runId : candidate.runId;
    const run = { id, head_sha: candidate.factoryCommit, head_repository: {full_name:candidate.repository},
      path: promotion.workflow, name: 'Hypha Windows signing factory', event: 'workflow_dispatch', status:'completed', conclusion:'success' };
    const selected = full ? 'Hypha validated Windows installer promotion' : 'Hypha signed Windows installer';
    if (args[1].includes('/jobs?')) return JSON.stringify({jobs:['Hypha unsigned Windows VST3','Hypha signed Windows installer',
      'Hypha validated Windows installer promotion'].map(name => ({name,status:'completed',conclusion:name===selected?'success':'skipped'}))});
    if (args[1].includes('/artifacts?')) return JSON.stringify({artifacts:[{name: full ? 'KirinHypha-Windows-signed-full' : candidate.artifactName,
      id:full?789:candidate.artifactId, expired:false, digest:full?fullDigest:candidate.digest, workflow_run:{id,head_sha:run.head_sha}}]});
    return JSON.stringify(run);
  };
  await verifyFactoryDelivery(state,invoke,manifest,directory);
  // A normalized datetime must still fail the strict coordinator after matching ZIP/API hashes.
  const changed = structuredClone(manifest); changed.ci_validation.verified_at = '2026-10-11T05:00:00.123456Z';
  const before = fs.readFileSync(manifestFile); fs.writeFileSync(manifestFile,JSON.stringify(changed));
  fs.rmSync(archive); writeSourceZip(archive,files); fullDigest=`sha256:${fileFact(archive).sha256}`;
  try { await assert.rejects(verifyFactoryDelivery(state,invoke,changed,directory), /Promotion changed/); }
  finally { fs.writeFileSync(manifestFile,before); fs.rmSync(archive); }
  console.log(JSON.stringify({status:'passed',timestampsPreserved:true,normalizedTimestampRejected:true}));
}
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
  const [mode,directory,repository] = process.argv.slice(2);
  if (mode === 'create') createFixture(directory,repository);
  else if (mode === 'verify') await verifyFixture(directory);
  else throw new Error('Unknown cross-runtime fixture mode');
}
