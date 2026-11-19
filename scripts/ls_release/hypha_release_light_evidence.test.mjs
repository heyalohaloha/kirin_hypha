import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { validateReport, digest, fileFact, acceptanceCriteria } from './hypha_release_contract.mjs';
import { LIGHT_FIX_ROWS_SCHEMA } from './hypha_release_light_policy.mjs';
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'light-host-proof-'));
  t.after(() => fs.rmSync(root, {recursive:true, force:true}));
  const candidate = {id:'fixture-candidate', version:'1.1.58', commit:'a'.repeat(40)};
  const expected = {readable:true, newIssues:0};
  const plan = {id:'labels', kind:'host', daw:'REAPER', os:'macos', chain:'PRE -> POST',
    hosts:['macOS/x86_64/AU','macOS/x86_64/VST3'], expected};
  const state = {root, candidate, requiredHostTests:[], expected:{labels:expected},
    fixRows:{schema:LIGHT_FIX_ROWS_SCHEMA, rows:[plan]}};
  const binding = 'b'.repeat(64); state.freeze = {sha256:binding, criteriaSha256:digest(acceptanceCriteria(state))};
  const write = (name, value) => {const p=path.join(root,name); fs.writeFileSync(p,JSON.stringify(value)); return fileFact(p);};
  const ref = (run,i) => ({kind:'host-run', host:run.host,...write(`run${i}.json`,run)});
  const runs=plan.hosts.map((host,i) => ({schema:'hypha-real-daw-run-v1', execution:'real-daw',result:'PASS',
    runId:`run-${i}`, candidate, bindingSha256:binding,rowId:plan.id,daw:plan.daw,os:plan.os,host,chain:plan.chain,
    processId:100+i,hostVersion:'fixture-host',observer:'fixture observer',oracle:'fixture label measurement',
    startedAt:'2026-11-20T00:00:00Z',finishedAt:'2026-11-20T00:00:10Z',expected,measured:{...expected},
    observations:[{kind:'host-log',...write(`host${i}.json`,{fixture:true,host})}]}));
  const row = {id:'labels',result:'PASS',expected,measured:{...expected},oracle:'fixture oracle',evidence:runs.map(ref)};
  const report = {schema:'kirin-hypha-release-evidence-v1',candidate,gate:'A4',bindingSha256:binding,tests:[row]};
  return {root,state,runs,row,report,ref,run:()=>{const f=write('report.json',report);return validateReport(state,f.path,'A4',binding,['labels']);}};
}
test('light A4 requires separately retained actual AU and VST3 observations', t=> {
  const f=fixture(t); assert.ok(f.run().length>=5);
  f.row.evidence.pop(); assert.throws(f.run,/one actual DAW run/);
});
test('light A4 rejects CI-only evidence and wrong host run identity',t=> {
  const f=fixture(t), original=structuredClone(f.runs);
  for(const patch of [{execution:'ci'}, {schema:'ci-log-v1'}, {host:'macOS/x86_64/AU'}, {daw:'Logic Pro'},
    {os:'windows'}, {candidate:{...f.state.candidate,id:'other'}}, {bindingSha256:'c'.repeat(64)},
    {rowId:'other'}, {runId:'run-0'}, {processId:0}, {hostVersion:''}, {result:'PENDING'},
    {measured:{newIssues:0}}, {measured:{readable:false,newIssues:0}}, {observations:[]}]) {
    const runs=structuredClone(original); Object.assign(runs[1],patch); f.row.evidence=runs.map(f.ref);
    assert.throws(f.run,/Light|host observation/);
  }
  f.row.evidence=[{kind:'attachment',...fileFact(path.join(f.root,'host0.json'))}];
  assert.throws(f.run,/one actual DAW run/);
});
test('light A4 rejects pending, observation tampering and reused format proofs',t=> {
  const f=fixture(t); f.row.result='PENDING'; assert.throws(f.run,/not PASS/); f.row.result='PASS';
  fs.appendFileSync(path.join(f.root,'host1.json'),' '); assert.throws(f.run,/observation hash/);
  f.row.evidence[1]={...f.row.evidence[0],host:'macOS/x86_64/VST3'}; assert.throws(f.run,/hash mismatch or reuse/);
});
test('old v2 reports keep their existing acceptance contract',t=> {
  const f=fixture(t); delete f.state.fixRows.schema;
  f.state.freeze.criteriaSha256=digest(acceptanceCriteria(f.state));
  f.row.evidence=[{kind:'attachment',...fileFact(path.join(f.root,'host0.json'))}];
  assert.ok(f.run().length>0);
});

test('frozen v2 cannot silently acquire the light policy',t=> {
  const f=fixture(t); delete f.state.fixRows.schema;
  f.state.freeze.criteriaSha256=digest(acceptanceCriteria(f.state));
  f.state.fixRows.schema=LIGHT_FIX_ROWS_SCHEMA;
  assert.throws(f.run,/frozen criteria changed/);
});
test('light policy tests are wired into the actual lightweight runner',()=> {
  const runner=fs.readFileSync(new URL('../test_lightweight_contract.sh',import.meta.url),'utf8');
  for(const name of ['hypha_release_light_policy.test.mjs','hypha_release_light_evidence.test.mjs'])
    assert.ok(runner.split('\n').some(line=>line.startsWith('run node --test ') && line.includes(name)));
});
