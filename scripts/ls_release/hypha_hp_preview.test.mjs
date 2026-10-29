import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { verifyHpPreview, deploymentReceipt, requireHpDeploymentEvidence } from './hypha_hp_preview.mjs';
import { makeHpRecord, hpReleaseNotice, renderHpPage, installerFilename } from './hypha_release_hp_data.mjs';
import { fileFact } from './hypha_release_contract.mjs';
import { publishHp, HP_FILES } from './hypha_release_hp.mjs';
import { renderedNews } from './fixtures/news-rendered-fixture.mjs';

test('deployment receipt binds the staged and promoted URL, commit, candidate and publication',()=>{
  const state={candidate:{id:'candidate'},hpCommit:'a'.repeat(40),publication:{sha256:'b'.repeat(64)},hpPreview:{fixture:'preview'}};
  const output=`Staged production deployment: https://example.vercel.app\nProduction deploy verified: https://kirinmastering.com (https://example.vercel.app, ${state.hpCommit})\n`;
  const receipt=deploymentReceipt(state,output);assert.equal(receipt.deploymentUrl,'https://example.vercel.app');assert.equal(receipt.publicationSha256,state.publication.sha256);
  state.hpDeployment=receipt; assert.equal(requireHpDeploymentEvidence(state),receipt);
  state.hpPreview={fixture:'changed'};assert.throws(()=>requireHpDeploymentEvidence(state));state.hpPreview={fixture:'preview'};
  state.hpDeployment={...receipt,hpCommit:'c'.repeat(40)};assert.throws(()=>requireHpDeploymentEvidence(state));
  for(const bad of [output.replace('example.vercel.app,','other.vercel.app,'),output.replace(state.hpCommit,'c'.repeat(40)),output.replace('Production deploy verified:','Production deploy pending:'),output+output]) assert.throws(()=>deploymentReceipt(state,bad));
});

function fixture(t) {
  const root=fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(),'hypha-hp-preview-test-')));
  t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
  const facts=['macos','windows'].map(target=>{const file=path.join(root,installerFilename('1.2.3',target));fs.writeFileSync(file,'fixture');return fileFact(file)});
  const state={root, candidate:{id:'candidate',version:'1.2.3'}, inputs:{date:'2026-10-01',hpRoot:root}, stages:{packages:{facts}}};
  const record=makeHpRecord('1.2.3','2026-10-01',facts), notice=hpReleaseNotice(record);
  const template=lang=>`<html lang="${lang}"><h2 id="hypha-download-title">Hypha {{KIRIN_HYPHA_LATEST_VERSION}}</h2><time datetime="{{KIRIN_HYPHA_PUBLISHED_AT}}">{{KIRIN_HYPHA_PUBLISHED_AT}}</time><a href="{{KIRIN_HYPHA_RELEASE_NOTES_URL}}">notes</a><a href="/downloads/kirin-hypha/macos">mac</a><a href="/downloads/kirin-hypha/windows">win</a>Audio Unit + AAX Windows 10/11 (VST3 + AAX)</html>`;
  let lastOutput; const build=async(_tool,_args,opts)=>{
    assert.match(opts.env.KIRIN_HYPHA_PREVIEW_BUILDER,/^file:.*\/scripts\/build-public-site\.mjs$/);
    lastOutput=opts.env.KIRIN_HYPHA_PREVIEW_DIR;
    const write=(name,value)=>{let target=path.join(lastOutput,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,value)};
    write('updates/hypha.json',JSON.stringify(record));write('hypha.html',renderHpPage(template('en'),record));write('ja/hypha.html',renderHpPage(template('ja'),record));
    const {id,category,target,products,date,version,en,ja}=notice;
    write('updates/hypha-news.json',JSON.stringify({schema:'kirin-hypha-public-news-v1',topIds:[id],items:[{id,category,target,products,date,version,en,ja}]}));
    write('index.html',renderedNews([notice],'en'));write('ja/index.html',renderedNews([notice],'ja'));
  };
  return {state,record,notice,build,getOutput:()=>lastOutput};
}
test('preview binds generated pages, record and approved bilingual notice and removes owned output',async t=>{
 const f=fixture(t);const receipt=await verifyHpPreview(f.state,f.build);assert.equal(receipt.version,'1.2.3');assert.equal(receipt.pages.length,6);assert.equal(fs.existsSync(f.getOutput()),false);
});
for(const mutation of ['missing','page','notice','duplicate','record','token','news','formats','news-token']) test(`preview rejects ${mutation}`,async t=>{
 const f=fixture(t);const run=async(...args)=>{await f.build(...args);const dir=f.getOutput(),file=path.join(dir,'updates/hypha-news.json');
  if(mutation==='missing')fs.unlinkSync(file);
  if(mutation==='news-token')fs.appendFileSync(path.join(dir,'index.html'),'{{KIRIN_UNKNOWN}}');
  if(mutation==='formats'){let p=path.join(dir,'ja/hypha.html');fs.writeFileSync(p,fs.readFileSync(p,'utf8').replace('Windows 10/11 (VST3 + AAX)','Windows 10/11 (VST3)'))}
  if(mutation==='news')fs.writeFileSync(path.join(dir,'ja/index.html'),'broken');
  if(mutation==='page')fs.writeFileSync(path.join(dir,'ja/hypha.html'),'broken');
  if(mutation==='token')fs.appendFileSync(path.join(dir,'hypha.html'),'{{KIRIN_UNKNOWN}}');
  if(mutation==='record')fs.writeFileSync(path.join(dir,'updates/hypha.json'),'{}');
  if(mutation==='notice'||mutation==='duplicate'){let data=JSON.parse(fs.readFileSync(file));if(mutation==='notice')data.items[0].ja.summary='changed';else data.items.push({...data.items[0],id:'another-id'});fs.writeFileSync(file,JSON.stringify(data))}
 };
 await assert.rejects(verifyHpPreview(f.state,run));assert.equal(fs.existsSync(f.getOutput()),false);
});

test('preview verification tests are included in the lightweight source gate',()=>{
 const gate=fs.readFileSync(new URL('../test_lightweight_contract.sh',import.meta.url),'utf8');
 assert.match(gate,/node --test[^\n]*scripts\/ls_release\/hypha_hp_preview\.test\.mjs/);
});

test('updated HP preview failure prevents git commit or push', async t => {
 const f=fixture(t), root=path.join(f.state.root,'hp'); fs.mkdirSync(path.join(root,'.vercel'),{recursive:true});
 fs.writeFileSync(path.join(root,'README_DEPLOY.md'),'standard deploy');
 fs.writeFileSync(path.join(root,'.vercel/project.json'),JSON.stringify({projectName:'expected',projectId:'expected-id'}));
 f.state.inputs.hpRoot=root;f.state.inputs.hpBaseCommit='c'.repeat(40);f.state.inputs.hpProjectSha256=fileFact(path.join(root,'.vercel/project.json')).sha256;
 const source=lang=>`<html lang="${lang}"><h2 id="hypha-download-title">Hypha {{KIRIN_HYPHA_LATEST_VERSION}}</h2><time datetime="{{KIRIN_HYPHA_PUBLISHED_AT}}">{{KIRIN_HYPHA_PUBLISHED_AT}}</time><a href="{{KIRIN_HYPHA_RELEASE_NOTES_URL}}">notes</a><a href="/downloads/kirin-hypha/macos">mac</a><a href="/downloads/kirin-hypha/windows">win</a>Audio Unit + AAX Windows 10/11 (VST3 + AAX)</html>`;
 const old=makeHpRecord('1.1.1','2026-09-01',f.state.stages.packages.facts.map((x,i)=>({...x,path:installerFilename('1.1.1',i===0?'macos':'windows')})));
 const data=[source('en'),source('ja'),JSON.stringify(old),JSON.stringify({schema:'kirin-news-v1',items:[hpReleaseNotice(old)]})];
 HP_FILES.forEach((name,i)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data[i])});
 const calls=[];const run=async(tool,args)=>{calls.push([tool,args]);if(tool==='node')throw new Error('preview fixture failure');
  if(tool==='vercel')return 'expected expected-id';if(tool==='npm')return '';if(args[0]==='branch')return 'main';
  if(args[0]==='rev-parse'||args[0]==='ls-remote')return f.state.inputs.hpBaseCommit;
  if(args[0]==='status'||args[0]==='ls-files')return '';if(args[0]==='diff')return args.includes('--cached')?'':HP_FILES.join('\n');return ''};
 await assert.rejects(publishHp(f.state,run,()=>{},async()=>new Response('',{status:503})),/preview fixture failure/);
 assert.equal(calls.some(([tool,args])=>tool==='git'&&['add','commit','push'].includes(args[0])),false);
});

test('retained preview must belong to the same candidate, publication and complete page inventory',async()=>{
 const { requireHpPreviewEvidence,PREVIEW_PAGES }=await import('./hypha_hp_preview.mjs');
 const state={candidate:{version:'1.2.3'},hpCommit:'a'.repeat(40),publication:{sha256:'b'.repeat(64)},hpPlanned:HP_FILES.map(path=>({path,sha256:'c'.repeat(64)}))};
 assert.throws(()=>requireHpPreviewEvidence(state),/preview evidence/i);
 state.hpPreview={version:'1.2.3',hpCommit:state.hpCommit,publicationSha256:state.publication.sha256,sourceFacts:state.hpPlanned,pages:PREVIEW_PAGES.map(path=>({path,sha256:'d'.repeat(64)}))};
 assert.doesNotThrow(()=>requireHpPreviewEvidence(state));
 state.stages={hp:{status:'PASS',evidence:{previewEvidence:structuredClone(state.hpPreview)}}};
 state.hpPreview.pages[0].sha256='e'.repeat(64);assert.throws(()=>requireHpPreviewEvidence(state),/preview evidence/i);
 state.hpPreview=structuredClone(state.stages.hp.evidence.previewEvidence);
 state.hpPreview.pages[0].path='wrong';assert.throws(()=>requireHpPreviewEvidence(state),/preview evidence/i);
});
