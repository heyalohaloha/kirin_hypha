import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import test from 'node:test';
import { assertFactoryRun, selectFactoryArtifact, assertInstallerJobs, verifyFactoryDelivery } from './factory_artifact.mjs';
import { fileFact } from '../ls_release/hypha_release_contract.mjs';
import { writeSourceZip } from './source_zip_writer.mjs';

const commit = 'a'.repeat(40), repository = 'fixture/factory';
const runRecord = id => ({ id, head_sha: commit, head_repository: { full_name: repository },
  path: '.github/workflows/hypha-windows-signing.yml', name: 'Hypha Windows signing factory',
  event: 'workflow_dispatch', status: 'completed', conclusion: 'success' });
const jobs = selected => ['Hypha unsigned Windows VST3','Hypha signed Windows installer','Hypha validated Windows installer promotion']
  .map(name => ({ name, status: 'completed', conclusion: name === selected ? 'success' : 'skipped' }));
const artifact = (name, id, run, digest) => ({ name, id, expired: false, digest, workflow_run: {id:run.id, head_sha:run.head_sha} });

test('coordinator uses the full factory/run/artifact predicate and separated signing/promotion jobs', () => {
  const run = runRecord(10); assertFactoryRun(run, commit, '10', repository);
  for (const field of ['id','head_sha','path','name','event','status','conclusion','head_repository']) {
    assert.throws(() => assertFactoryRun({ ...run, [field]: 'wrong' }, commit, 10, repository), /Factory run/);
  }
  const row = artifact('full', 20, run, `sha256:${'b'.repeat(64)}`);
  selectFactoryArtifact([row], 'full', run);
  for (const field of ['id','expired','digest','workflow_run']) {
    assert.throws(() => selectFactoryArtifact([{ ...row, [field]: field === 'expired' ? true : null }], 'full', run), /Factory artifact/);
  }
  assert.throws(() => selectFactoryArtifact([row,row], 'full', run), /ambiguous/);
  for (const selected of ['Hypha signed Windows installer','Hypha validated Windows installer promotion']) {
    const rows = jobs(selected); assertInstallerJobs(rows, selected);
    assert.throws(() => assertInstallerJobs([...rows,rows[0]], selected), /separation/);
    rows[0].conclusion = 'success'; assert.throws(() => assertInstallerJobs(rows, selected), /separation/);
  }
});

test('signed-full delivery binds original candidate, both run identities, API digest and actual archive bytes', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-factory-delivery-'));
  t.after(() => fs.rmSync(root, {recursive:true,force:true}));
  const directory = path.join(root,'delivery'); fs.mkdirSync(path.join(directory,'provenance'), {recursive:true});
  const originalFile = path.join(directory,'provenance/candidate-manifest.json');
  const original = { signing: { workflow_run:`https://github.com/${repository}/actions/runs/10` },
    source: { commit:'c'.repeat(40) }, installer:{sha256:'d'.repeat(64)},
    external_validation:{status:'pending'}, distribution:{public_ready:false} };
  fs.writeFileSync(originalFile, JSON.stringify(original));
  const manifest = { ...structuredClone(original), external_validation:{status:'complete',candidate_workflow_run:original.signing.workflow_run},
    distribution:{public_ready:true}, factoryImport:{
      candidate:{ factoryCommit:commit,repository,event:'workflow_dispatch',workflow:'.github/workflows/hypha-windows-signing.yml',
        runId:10,artifactId:20,artifactName:'KirinHypha-Windows-signed-candidate',digest:`sha256:${'b'.repeat(64)}`,archiveSha256:'b'.repeat(64) },
      promotion:{factoryCommit:commit,repository,event:'workflow_dispatch',workflow:'.github/workflows/hypha-windows-signing.yml',runId:11},
      candidateManifest:{path:'provenance/candidate-manifest.json',sha256:fileFact(originalFile).sha256} } };
  const manifestFile = path.join(directory,'installer.json'), archive = path.join(root,'full.zip');
  const save = () => { fs.writeFileSync(manifestFile,JSON.stringify(manifest)); if (fs.existsSync(archive)) fs.rmSync(archive);
    writeSourceZip(archive,[{archive:'installer.json',file:manifestFile},{archive:'provenance/candidate-manifest.json',file:originalFile}]); };
  save();
  const state = { root, inputs:{windowsFactoryCommit:commit,windowsFactoryRepository:repository,windowsPromotionRun:'11',windowsArtifactArchive:archive} };
  const signed = runRecord(10), promoted = runRecord(11);
  const candidateRow = artifact('KirinHypha-Windows-signed-candidate',20,signed,manifest.factoryImport.candidate.digest);
  let fullDigest = `sha256:${fileFact(archive).sha256}`;
  const invoke = async (tool,args) => {
    if (tool === 'python3') return execFileSync(tool,args,{cwd:path.resolve(import.meta.dirname,'../..'),encoding:'utf8'});
    assert.equal(tool,'gh'); assert.equal(args[0],'api');
    const endpoint = args[1], isPromotion = endpoint.includes('/runs/11');
    if (endpoint.includes('/jobs?')) return JSON.stringify({jobs:jobs(isPromotion ? 'Hypha validated Windows installer promotion':'Hypha signed Windows installer')});
    if (endpoint.includes('/artifacts?')) return JSON.stringify({artifacts:[isPromotion ? artifact('KirinHypha-Windows-signed-full',21,promoted,fullDigest) : candidateRow]});
    return JSON.stringify(isPromotion ? promoted : signed);
  };
  assert.equal((await verifyFactoryDelivery(state,invoke,manifest,directory)).length,2);
  for (const field of ['windowsFactoryCommit','windowsFactoryRepository','windowsPromotionRun','windowsArtifactArchive']) {
    await assert.rejects(verifyFactoryDelivery({...state,inputs:{...state.inputs,[field]:''}},invoke,manifest,directory), /private profile/);
  }
  signed.head_sha = 'f'.repeat(40); await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/Factory run/); signed.head_sha=commit;
  promoted.event = 'push'; await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/Factory run/); promoted.event='workflow_dispatch';
  candidateRow.digest=`sha256:${'f'.repeat(64)}`; await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/digest\/ID/); candidateRow.digest=manifest.factoryImport.candidate.digest;
  fs.appendFileSync(archive,'changed'); await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/archive digest/); save();
  fs.appendFileSync(manifestFile,'changed'); await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory)); save();
  fs.writeFileSync(path.join(directory,'extra'),'not archived'); await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory)); fs.rmSync(path.join(directory,'extra'));
  manifest.installer.sha256='e'.repeat(64); save(); fullDigest=`sha256:${fileFact(archive).sha256}`;
  await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/Promotion changed/);
  manifest.installer.sha256='d'.repeat(64); manifest.factoryImport.promotion.factoryCommit='f'.repeat(40); save(); fullDigest=`sha256:${fileFact(archive).sha256}`;
  await assert.rejects(verifyFactoryDelivery(state,invoke,manifest,directory),/origin mismatch/);
});

test('artifact archive rejects traversal, duplicate names, symlinks and changed delivery bytes', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(),'hypha-archive-')); t.after(() => fs.rmSync(root,{recursive:true,force:true}));
  const directory=path.join(root,'files'), archive=path.join(root,'bad.zip'); fs.mkdirSync(directory); fs.writeFileSync(path.join(directory,'file'),'inert');
  const script=path.resolve(import.meta.dirname,'artifact_archive.py');
  for (const kind of ['traversal','duplicate','symlink','case']) {
    execFileSync('python3',['-c',`import zipfile,sys\nz=zipfile.ZipFile(sys.argv[1],'w')\nk=sys.argv[2]\nif k=='traversal': z.writestr('../file','inert')\nif k=='duplicate': z.writestr('file','inert');z.writestr('file','inert')\nif k=='case': z.writestr('file','inert');z.writestr('FILE','inert')\nif k=='symlink':\n i=zipfile.ZipInfo('file');i.external_attr=0o120777<<16;z.writestr(i,'inert')\nz.close()`,archive,kind],{stdio:'pipe'});
    assert.throws(() => execFileSync('python3',[script,archive,directory],{stdio:'pipe'}));
  }
});
