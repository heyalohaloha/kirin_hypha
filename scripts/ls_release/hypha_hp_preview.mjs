// Verify a fresh local public-site build before any HP publication action.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { isDeepStrictEqual } from 'node:util';
import { hash, Checkpoint } from './hypha_release_contract.mjs';
import { HP_FILES, makeHpRecord, validateHpRecord, assertHpPage, hpReleaseNotice } from './hypha_release_hp_data.mjs';
import { publicationNotice } from './publication_input.mjs';

export const PREVIEW_PAGES = Object.freeze(['updates/hypha.json', 'updates/hypha-news.json', 'hypha.html', 'ja/hypha.html', 'index.html', 'ja/index.html']);
export function deploymentReceipt(state, output) {
  if (typeof output !== 'string' || !/^[0-9a-f]{40}$/.test(state.hpCommit || '') || !state.candidate?.id) throw new Error('Deployment output or identity invalid');
  const stages=[...output.matchAll(/^Staged production deployment: (https:\/\/[a-z0-9.-]+\.vercel\.app)\r?$/gm)];
  const complete=[...output.matchAll(/^Production deploy verified: https:\/\/kirinmastering\.com \((https:\/\/[a-z0-9.-]+\.vercel\.app), ([0-9a-f]{40})\)\r?$/gm)];
  if(stages.length!==1 || complete.length!==1 || stages[0][1]!==complete[0][1] || complete[0][2]!==state.hpCommit || stages[0].index>=complete[0].index) throw new Error('Staged deployment verification receipt missing or differs');
  return {candidateId:state.candidate.id,hpCommit:state.hpCommit,publicationSha256:state.publication?.sha256 || null,
    previewSha256:hash(JSON.stringify(state.hpPreview)),deploymentUrl:stages[0][1]};
}
export function requireHpDeploymentEvidence(state) {
  const receipt=state.hpDeployment;
  if(!receipt || receipt.candidateId!==state.candidate?.id || receipt.hpCommit!==state.hpCommit
      || receipt.publicationSha256!==(state.publication?.sha256 || null) || !state.hpPreview
      || receipt.previewSha256!==hash(JSON.stringify(state.hpPreview))
      || !/^https:\/\/[a-z0-9.-]+\.vercel\.app$/.test(receipt.deploymentUrl || '')
      || (state.stages?.hp?.status==='PASS' && !isDeepStrictEqual(state.stages.hp.evidence?.deploymentEvidence,receipt))) {
    throw new Checkpoint('Retained HP deployment evidence missing or changed; reconcile before reuse');
  }
  return receipt;
}
const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function assertNews(html, lang, notice, ids) {
  const entries = [...html.matchAll(/<li\b[^>]*class="news__item"[^>]*data-news-id="([^"]+)"[^>]*>[\s\S]*?<\/li>/g)];
  if (!isDeepStrictEqual(entries.map(match => match[1]), ids)) throw new Error('Preview news IDs differ from public archive');
  const entry = entries.find(match => match[1] === notice.id);
  if (!entry) return;
  const copy = notice[lang], title = lang === 'ja' ? copy.titlePhrases.map(phrase => {
    const leading=phrase.match(/^\s*/u)[0], trailing=phrase.match(/\s*$/u)[0];
    return `${escape(leading)}<span class="r1-phrase-unit">${escape(phrase.slice(leading.length,phrase.length-trailing.length))}</span>${escape(trailing)}`;
  }).join('<wbr>') : escape(copy.title);
  if (!entry[0].includes(`<time datetime="${escape(notice.date)}">`)
      || !entry[0].includes(`<a href="${escape(copy.url)}">${title}</a>`)
      || !entry[0].includes(`<p class="news__summary">${escape(copy.summary)}</p>`)) throw new Error('Preview news text differs');
}
export function requireHpPreviewEvidence(state) {
  const preview=state.hpPreview;
  if (!preview || preview.hpCommit!==state.hpCommit || preview.version!==state.candidate.version
      || preview.publicationSha256!==(state.publication?.sha256||null)
      || !Array.isArray(state.hpPlanned) || !isDeepStrictEqual(state.hpPlanned.map(f=>f.path),HP_FILES)
      || state.hpPlanned.some(f=>!/^[0-9a-f]{64}$/.test(f.sha256||''))
      || !isDeepStrictEqual(preview.sourceFacts,state.hpPlanned) || !Array.isArray(preview.pages)
      || !isDeepStrictEqual(preview.pages.map(page=>page.path),PREVIEW_PAGES)
      || preview.pages.some(page=>!/^[0-9a-f]{64}$/.test(page.sha256||''))
      || (state.stages?.hp?.status==='PASS' && !isDeepStrictEqual(state.stages.hp.evidence?.previewEvidence,preview))) {
    throw new Checkpoint('Retained HP preview evidence missing or changed; reconcile before reuse');
  }
  return preview;
}
export async function verifyHpPreview(state, run) {
  const output=fs.mkdtempSync(path.join(os.tmpdir(),'hypha-hp-preview-'));
  try {
    const program="const {buildPublicSite}=await import(process.env.KIRIN_HYPHA_PREVIEW_BUILDER); await buildPublicSite(process.env.KIRIN_HYPHA_PREVIEW_DIR);";
    const builder=pathToFileURL(path.resolve(state.inputs.hpRoot,'scripts/build-public-site.mjs')).href;
    await run('node',['--input-type=module','-e',program],{cwd:state.inputs.hpRoot,env:{KIRIN_HYPHA_PREVIEW_DIR:output,KIRIN_HYPHA_PREVIEW_BUILDER:builder}});
    const data=Object.fromEntries(PREVIEW_PAGES.map(name=>{
      const file=path.join(output,name), stat=fs.lstatSync(file);
      if (!stat.isFile() || stat.isSymbolicLink() || !fs.realpathSync(file).startsWith(fs.realpathSync(output) + path.sep)) throw new Error(`Invalid preview file: ${name}`);
      return [name,fs.readFileSync(file,'utf8')];
    }));
    const expected=makeHpRecord(state.candidate.version,state.inputs.date,state.stages.packages?.facts);
    if (!isDeepStrictEqual(validateHpRecord(JSON.parse(data['updates/hypha.json'])),expected)) throw new Error('Preview release record differs');
    const notice=state.publication ? publicationNotice(state) : hpReleaseNotice(expected);
    const archive=JSON.parse(data['updates/hypha-news.json']);
    if (archive?.schema !== 'kirin-hypha-public-news-v1' || !Array.isArray(archive.items) || !Array.isArray(archive.topIds)
        || archive.topIds.length>3 || archive.topIds.some(id=>typeof id!=='string') || new Set(archive.topIds).size!==archive.topIds.length) throw new Error('Invalid preview news archive');
    const matching=archive.items.filter(item=>item?.version===notice.version && Array.isArray(item.products) && item.products.includes('hypha'));
    const {id,category,target,products,date,version,en,ja}=notice;
    if (matching.length!==1 || !isDeepStrictEqual(matching[0],{id,category,target,products,date,version,en,ja})) throw new Error('Preview notice differs or is duplicate');
    for (const [file,lang] of [['hypha.html','en'],['ja/hypha.html','ja']]) {
      if (/\{\{KIRIN_[A-Z_]+\}\}/.test(data[file])) throw new Error('Unexpanded preview token');
      assertHpPage(data[file],lang,expected);
      if (!data[file].includes('Audio Unit + AAX') || !data[file].includes('Windows 10/11 (VST3 + AAX)')) throw new Error('Preview format mismatch');
    }
    for (const file of ['index.html','ja/index.html']) if (/\{\{KIRIN_[A-Z_]+\}\}/.test(data[file])) throw new Error('Unexpanded preview token');
    assertNews(data['index.html'],'en',notice,archive.topIds); assertNews(data['ja/index.html'],'ja',notice,archive.topIds);
    return {version:state.candidate.version,publicationSha256:state.publication?.sha256 || null,
      pages:PREVIEW_PAGES.map(name=>({path:name,sha256:hash(data[name])}))};
  } finally { fs.rmSync(output,{recursive:true,force:true}); }
}
