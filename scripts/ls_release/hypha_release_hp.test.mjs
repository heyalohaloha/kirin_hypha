import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { updateHpDocuments, publishGithub, checkTag, publicDownloadFacts, verifyPublicHp,
  hpPreflight, publishHp, HP_FILES } from './hypha_release_hp.mjs';
import { makeHpRecord, renderHpPage, validateHpRecord, installerFilename, hpReleaseNotice, hpPublicationDay,
  assertHpPublicationDate } from './hypha_release_hp_data.mjs';
import { fileFact, hash, Checkpoint } from './hypha_release_contract.mjs';
import { publicationBinding } from './publication_input.mjs';
import { renderedNews } from './fixtures/news-rendered-fixture.mjs';

const base = 'https://github.com/heyalohaloha/kirin_hypha/releases';
function page(lang) {
  return `<html lang="${lang}"><section id="downloads"><h2 id="hypha-download-title">Hypha {{KIRIN_HYPHA_LATEST_VERSION}}</h2>
<time datetime="{{KIRIN_HYPHA_PUBLISHED_AT}}">{{KIRIN_HYPHA_PUBLISHED_AT}}</time>
<a href="/downloads/kirin-hypha/macos">PKG</a>
<a href="/downloads/kirin-hypha/windows">Download for Windows VST3 + AAX</a>
macOS 12+ (VST3 + Audio Unit + AAX) / Windows 10/11 (VST3 + AAX)
<a href="{{KIRIN_HYPHA_RELEASE_NOTES_URL}}">Read {{KIRIN_HYPHA_LATEST_VERSION}}</a></section></html>`;
}
function facts(version = '1.2.3') {
  return ['macos','windows'].map((target,i) => ({ path: installerFilename(version,target), bytes:10+i, sha256:String(i+1).repeat(64) }));
}
const otherNotice = { id: 'os-1-2-0-release-20261001', category: 'update', target: 'product', products: ['os'],
  date: '2026-08-01', status: 'confirmed', version: '1.2.0',
  ja: { title: 'Kirin OS 1.2.0を公開', titlePhrases: ['Kirin OS ', '1.2.0を', '公開'], summary: '別製品の告知。', url: '/ja/os' },
  en: { title: 'Kirin OS 1.2.0 released', summary: 'Another product notice.', url: '/os' },
  evidence: { source: 'reviewed', published_at: '2026-08-01', url: 'https://example.org/notes' } };
function news(...items) { return `${JSON.stringify({ schema: 'kirin-news-v1', items: [...items, otherNotice] }, null, 2)}\n`; }
function documents(version = '1.1.49') {
  return [page('en'), page('ja'), JSON.stringify(makeHpRecord(version,'2026-09-02',facts(version))),
    news(hpReleaseNotice(makeHpRecord(version,'2026-09-02',facts(version))))];
}
function hpFixture(f) {
  const installerFacts = ['macos','windows'].map((target,i) => {
    const file = path.join(f.root,installerFilename(f.state.candidate.version,target));
    fs.writeFileSync(file,`signed ${target} fixture ${i}`); return fileFact(file);
  });
  f.state.stages.packages.facts=installerFacts;
  return installerFacts;
}
function writeHpFixture(root) {
  for (const [i,file] of HP_FILES.entries()) {
    fs.mkdirSync(path.dirname(path.join(root,file)),{recursive:true});
    fs.writeFileSync(path.join(root,file),documents()[i]);
  }
}
function publicHpFetcher(state,root,{deployed=()=>true,head=()=>state.hpCommit}={}) {
  return async (url,options={}) => {
    if (!deployed()) return new Response('not deployed',{status:503});
    if (url.includes('/downloads/kirin-hypha/')) {
      const target=url.endsWith('/macos')?'macos':'windows';
      const record=makeHpRecord(state.candidate.version,state.inputs.date,state.stages.packages.facts);
      if(options.redirect==='manual')return new Response(null,{status:302,headers:{Location:record.downloads[target].url,'Cache-Control':'no-store'}});
      return new Response(fs.readFileSync(state.stages.packages.facts.find(f=>path.basename(f.path)===installerFilename(state.candidate.version,target)).path));
    }
    if (url.includes('deployment-source')) return new Response(head());
    if (url.includes('/updates/hypha.json')) return new Response(fs.readFileSync(path.join(root, 'updates/hypha.json'), 'utf8'));
    if (url.includes('/data/news.json')) return new Response('not published', { status: 404 });
    if (new URL(url).pathname === '/' || new URL(url).pathname === '/ja/') {
      const items = JSON.parse(fs.readFileSync(path.join(root, 'data/news.json'), 'utf8')).items;
      return new Response(renderedNews(items, new URL(url).pathname === '/' ? 'en' : 'ja'));
    }
    const record=JSON.parse(fs.readFileSync(path.join(root,'updates/hypha.json'),'utf8'));
    return new Response(renderHpPage(fs.readFileSync(path.join(root,url.includes('/ja/')?'ja/hypha.html':'hypha.html'),'utf8'),record));
  };
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-hp-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.writeFileSync(path.join(root, 'AGENTS.md'), '# GitHub budget policy\n');
  const payload = path.join(root, 'payload.pkg'); fs.writeFileSync(payload, 'signed fixture bytes');
  const source = path.join(root, 'source.zip'); fs.writeFileSync(source, 'source delivery fixture bytes');
  return { root, payload, state: { root, candidate: { id: 'candidate', version: '1.2.3', commit: 'a'.repeat(40) },
    inputs: { notes: 'reviewed-notes.md', date: '2026-10-01' }, stages: { packages: { facts: [fileFact(payload)] },
      provenance: { status: 'PASS', facts: [fileFact(source)], publicFacts: [fileFact(source)] } } } };
}

test('HP release update changes only the central record and adds one standard release notice', () => {
  assert.deepEqual(HP_FILES, ['hypha.html', 'ja/hypha.html', 'updates/hypha.json', 'data/news.json']);
  const docs=documents(); const result=updateHpDocuments(docs,'1.2.3','2026-10-01',facts());
  assert.deepEqual(result.slice(0,2),docs.slice(0,2),'token templates are verified, never rewritten');
  const record=validateHpRecord(JSON.parse(result[2]));
  assert.equal(record.version,'1.2.3'); assert.equal(result[2],`${JSON.stringify(record,null,2)}\n`);
  for(const html of result.slice(0,2)) assert.match(renderHpPage(html,record),/Hypha 1\.2\.3/);
  const notices=JSON.parse(result[3]).items;
  assert.deepEqual(notices[0],{ id:'hypha-1-2-3-release-20261001', category:'update', target:'product', products:['hypha'],
    date:'2026-10-01', status:'confirmed', version:'1.2.3',
    ja:{ title:'Kirin Hypha 1.2.3を公開', titlePhrases:['Kirin Hypha ','1.2.3を','公開'],
      summary:'配布ファイルと更新情報を製品ページで確認できます。', url:'/ja/hypha' },
    en:{ title:'Kirin Hypha 1.2.3 released', summary:'Find the download files and release information on the product page.', url:'/hypha' },
    evidence:{ source:'updates/hypha.json', published_at:'2026-10-01', url:`${base}/tag/v1.2.3` } });
  assert.deepEqual(notices.slice(1),JSON.parse(docs[3]).items,'earlier and other-product notices stay byte-for-byte');
  assert.equal(result[3],`${JSON.stringify(JSON.parse(result[3]),null,2)}\n`);
  assert.deepEqual(docs,documents());
});

test('changed templates, literal versions, broken notices, missing integrity and wrong targets stop the HP update', () => {
  for(const change of [d=>d[0]=d[0].replace('{{KIRIN_HYPHA_LATEST_VERSION}}','1.1.49'),
      d=>d[1]=d[1].replace('/downloads/kirin-hypha/macos','/downloads/kirin-hypha/zip'),
      d=>d[0]=d[0].replace('</section>','Fixed in 1.1.49</section>'),
      d=>d[1]=d[1].replace('Windows 10/11 (VST3 + AAX)','Windows 10/11 (VST3)'),
      d=>d[3]=JSON.stringify({ schema:'kirin-news-v0', items:[] }),
      d=>d[3]=news(otherNotice),
      d=>d[3]='{broken']){
    const d=documents();change(d);assert.throws(()=>updateHpDocuments(d,'1.2.3','2026-10-01',facts()));
  }
  assert.throws(()=>updateHpDocuments(documents(),'latest','2026-10-01',facts()),/Invalid/);
  assert.throws(()=>updateHpDocuments(documents(),'1.2.3','2026-02-31',facts()),/Invalid/);
  for(const invalid of [[],facts().slice(0,1),[...facts(),facts()[0]],facts().map(f=>({...f,sha256:'bad'}))]){
    assert.throws(()=>updateHpDocuments(documents(),'1.2.3','2026-10-01',invalid));
  }
});

test('central release records reject unknown keys, foreign or mismatched URLs and malformed integrity', () => {
  const valid=makeHpRecord('1.1.49','2026-09-02',facts('1.1.49'));
  for(const change of [r=>r.downloads.arm64=r.downloads.macos,r=>delete r.downloads.windows,
      r=>r.downloads.macos.url+='?redirect=evil',r=>r.downloads.macos.url=r.downloads.macos.url.replace('v1.1.49','v1.1.48'),
      r=>r.downloads.windows.bytes=0,r=>r.downloads.windows.sha256='A'.repeat(64),
      r=>r.published_at='2026-02-31',r=>r.unreviewed=true]){
    const record=structuredClone(valid);change(record);assert.throws(()=>validateHpRecord(record));
  }
});

test('a release dated on or before the current notice, or after today in Tokyo, stops before publication', () => {
  // HP lists confirmed notices newest date first (ties by id ascending) and rejects future-dated ones.
  assert.throws(()=>updateHpDocuments(documents(),'1.2.3','2026-09-02',facts()),/later than/);
  assert.throws(()=>updateHpDocuments(documents(),'1.2.3','2026-09-01',facts()),/later than/);
  assert.doesNotThrow(()=>updateHpDocuments(documents(),'1.2.3','2026-09-03',facts()));
  assert.equal(hpPublicationDay(new Date('2026-10-17T15:00:00Z')),'2026-10-18');
  assert.equal(hpPublicationDay(new Date('2026-10-17T14:59:59Z')),'2026-10-17');
  assert.doesNotThrow(()=>assertHpPublicationDate('2026-10-18',new Date('2026-10-17T15:00:00Z')));
  assert.throws(()=>assertHpPublicationDate('2026-10-18',new Date('2026-10-17T14:59:59Z')),Checkpoint);
});

test('own planned update is idempotent while downgrade or changed same-version integrity stops', () => {
  const first=updateHpDocuments(documents(),'1.2.3','2026-10-01',facts());
  assert.deepEqual(updateHpDocuments(first,'1.2.3','2026-10-01',facts()),first);
  assert.throws(()=>updateHpDocuments(first,'1.2.2','2026-10-01',facts('1.2.2')),/downgrade/);
  assert.throws(()=>updateHpDocuments(first,'1.2.3','2026-10-01',facts().map(f=>({...f,bytes:f.bytes+1}))),/same-version/);
  // A second notice for the same release, or an edited one, needs review rather than an overwrite.
  const edited=[...first]; const items=JSON.parse(first[3]).items; items[0].en.summary='edited';
  edited[3]=`${JSON.stringify({ schema:'kirin-news-v1', items },null,2)}\n`;
  assert.throws(()=>updateHpDocuments(edited,'1.2.3','2026-10-01',facts()),/notice/);
  const duplicate=[...first]; const twice=JSON.parse(first[3]).items; twice.unshift({ ...twice[0], id:'hypha-1-2-3-release-20260930' });
  duplicate[3]=`${JSON.stringify({ schema:'kirin-news-v1', items:twice },null,2)}\n`;
  assert.throws(()=>updateHpDocuments(duplicate,'1.2.3','2026-10-01',facts()),/notice/);
});

// Drafts are invisible to the tag endpoint, so the mock answers the authenticated list and ID endpoints.
const isCreate = a => a[0] === 'api' && a[1].endsWith('/releases') && a.includes('POST');
const isUpload = a => a[0] === 'api' && a[1].startsWith('https://uploads.github.com/');
const isPublish = a => a[0] === 'api' && a.includes('PATCH');
const isReadback = ([tool, a, options]) => tool === 'gh' && a.includes('Accept: application/octet-stream') && !!options?.stdoutFile;
const field = (a, key) => a.find(v => v.startsWith(`${key}=`))?.slice(key.length + 1);
function githubMock(f, { published = false, existingAsset = false, corrupt = false } = {}) {
  let release = published ? { id: 1, tag_name: 'v1.2.3', name: 'Kirin Hypha 1.2.3', draft: false, target_commitish: f.state.candidate.commit, assets: [], html_url: 'release' } : null;
  const facts = [...f.state.stages.packages.facts, ...f.state.stages.provenance.publicFacts]; const calls = [];
  const asset = fact => ({ id: 10 + facts.indexOf(fact), name: path.basename(fact.path), size: fact.bytes, digest: `sha256:${fact.sha256}` });
  if (release || existingAsset) {
    release ||= { id: 1, tag_name: 'v1.2.3', name: 'Kirin Hypha 1.2.3', draft: true, target_commitish: f.state.candidate.commit, assets: [], html_url: 'release' };
    release.assets.push(...facts.map(asset));
  }
  const run = async (tool, args, options = {}) => {
    calls.push([tool, args, options]);
    assert.equal(tool, 'gh');
    if (options.stdoutFile) { // ID-addressed read-back: raw asset bytes into a new owned file
      assert.deepEqual(args.slice(2), ['-H', 'Accept: application/octet-stream']);
      const name = release.assets.find(a => `repos/heyalohaloha/kirin_hypha/releases/assets/${a.id}` === args[1]).name;
      const fact = facts.find(item => path.basename(item.path) === name);
      fs.writeFileSync(options.stdoutFile, corrupt ? 'corrupt' : fs.readFileSync(fact.path), { flag: 'wx' });
      return '';
    }
    if (args[0] === 'api') {
      if (args[1].includes('/git/ref/')) return release?.draft === false
        ? JSON.stringify({ object: { type: 'commit', sha: f.state.candidate.commit } }) : '';
      if (args[1].includes('/commits/')) return JSON.stringify({ sha: f.state.candidate.commit });
      if (args[1].includes('/releases?')) return JSON.stringify(release ? [release] : []);
      if (args[1].endsWith('/releases/1') && isPublish(args)) release.draft = false;
      if (args[1].endsWith('/releases/1')) return release ? JSON.stringify(release) : '';
    }
    if (isCreate(args)) {
      release = { id: 1, tag_name: field(args, 'tag_name'), draft: true, assets: [], name: field(args, 'name'),
        target_commitish: field(args, 'target_commitish'), html_url: 'release' };
      return JSON.stringify(release);
    }
    if (isUpload(args)) {
      const added = asset(facts.find(fact => fact.path === args[args.indexOf('--input') + 1]));
      release.assets.push(added); return JSON.stringify(added);
    }
    return '';
  };
  return { run, calls };
}

test('GitHub stages an immutable draft, hashes read-back before publish, pins exact commit', async t => {
  const f = fixture(t); const m = githubMock(f);
  await publishGithub(f.state, m.run, () => {});
  const create = m.calls.find(([, a]) => isCreate(a))[1];
  assert.equal(field(create, 'target_commitish'), f.state.candidate.commit);
  assert.equal(create[create.indexOf('draft=true') - 1], '-F');
  assert.ok(m.calls.findIndex(isReadback) >= 0);
  assert.ok(m.calls.findLastIndex(isReadback) < m.calls.findIndex(([, a]) => isPublish(a)));
  assert.ok(!m.calls.some(([, a]) => a.includes('--clobber') || a[1] === 'delete' || a.includes('DELETE')));
});

test('missing source gate or changed source bytes stops before GitHub mutation', async t => {
  const f = fixture(t); const m = githubMock(f);
  f.state.stages.provenance.status = 'PENDING';
  await assert.rejects(publishGithub(f.state, m.run, () => {}), Checkpoint);
  assert.deepEqual(m.calls, []);
  f.state.stages.provenance.status = 'PASS';
  fs.writeFileSync(f.state.stages.provenance.publicFacts[0].path, 'changed source');
  await assert.rejects(publishGithub(f.state, m.run, () => {}), /artifact changed/);
  assert.deepEqual(m.calls, []);
});

test('interrupted draft upload and published same-byte release are reusable without duplicate upload', async t => {
  const f = fixture(t);
  for (const scenario of [{ existingAsset: true }, { published: true }]) {
    const m = githubMock(f, scenario); await publishGithub(f.state, m.run, () => {});
    assert.ok(!m.calls.some(([, a]) => ['create', 'upload'].includes(a[1]) || isCreate(a) || isUpload(a)));
  }
});

test('wrong published bytes, foreign tags and authentication failure never become new release', async t => {
  const f = fixture(t); const m = githubMock(f, { existingAsset: true, corrupt: true });
  await assert.rejects(publishGithub(f.state, m.run, () => {}), /bytes/);
  assert.ok(!m.calls.some(([, a]) => a[1] === 'edit' || isPublish(a)));
  await assert.rejects(checkTag(f.state, async () => JSON.stringify({ object: { type: 'commit', sha: 'f'.repeat(40) } })), /frozen/);
  await assert.rejects(publishGithub(f.state, async () => { throw new Error('HTTP 403'); }, () => {}), /403/);
});

test('public-user-path download validates actual streamed bytes, not HEAD 200 or asset names', async t => {
  const f = fixture(t); const data = fs.readFileSync(f.payload);
  let requests = 0;
  const fetcher = async url => { requests++; assert.match(url, /download\/v1\.2\.3\/payload.pkg/); return new Response(data); };
  assert.equal((await publicDownloadFacts(f.state, f.state.stages.packages.facts, fetcher))[0].sha256, hash(data));
  assert.equal(requests, 1);
  await assert.rejects(publicDownloadFacts(f.state, f.state.stages.packages.facts,
    async () => new Response('wrong')), /hash mismatch/);
  await assert.rejects(publicDownloadFacts(f.state, f.state.stages.packages.facts,
    async () => new Response('no', { status: 404 })), /download failed/);
});

test('Corresponding Source public readback detects missing or altered source independently of the package', async t => {
  const f = fixture(t); const facts = [...f.state.stages.packages.facts, ...f.state.stages.provenance.publicFacts];
  const visited = [];
  await assert.rejects(publicDownloadFacts(f.state, facts, async url => {
    visited.push(url);
    return new Response(url.endsWith('/source.zip') ? 'wrong source' : fs.readFileSync(f.payload));
  }), /hash|bytes|size/i);
  assert.equal(visited.length, 2);
  assert.ok(visited[1].endsWith('/source.zip'));
});

test('HP public check validates both rendered languages/date/notes, redirects, bytes and deployed source', async t => {
  const f=fixture(t);hpFixture(f);f.state.hpCommit='c'.repeat(40);
  const root=path.join(f.root,'public-hp');fs.mkdirSync(root);writeHpFixture(root);
  const docs=updateHpDocuments(documents(),'1.2.3','2026-10-01',f.state.stages.packages.facts);
  for(const [i,file]of HP_FILES.entries())fs.writeFileSync(path.join(root,file),docs[i]);
  const fetcher=publicHpFetcher(f.state,root);await verifyPublicHp(f.state,fetcher);
  for(const [label,mutate]of [
    ['language',r=>new Response('wrong language')],
    ['date',r=>new Response(renderHpPage(docs[1],makeHpRecord('1.2.3','2026-10-01',f.state.stages.packages.facts)).replaceAll('2026-10-01','2026-10-02'))],
    ['notes',r=>new Response(renderHpPage(docs[1],makeHpRecord('1.2.3','2026-10-01',f.state.stages.packages.facts)).replace('/tag/v1.2.3','/tag/v1.2.2'))],
    ['redirect',r=>new Response(null,{status:302,headers:{Location:'https://evil.example','Cache-Control':'no-store'}})],
    ['cache',r=>new Response(null,{status:302,headers:{Location:makeHpRecord('1.2.3','2026-10-01',f.state.stages.packages.facts).downloads.macos.url,'Cache-Control':'public'}})],
    ['bytes',r=>new Response('wrong bytes')],
    ['source',r=>new Response('old source')],
  ]){
    const bad=async(url,options={})=>{
      const match=['language','date','notes'].includes(label)?url.includes('/ja/hypha'):label==='source'?url.includes('deployment-source'):
        url.endsWith('/macos')&&(label==='bytes'?options.redirect==='follow':options.redirect==='manual');
      return match?mutate():fetcher(url,options);
    };
    await assert.rejects(verifyPublicHp(f.state,bad),undefined,label);
  }
});

test('HP dirtiness, target drift and wrong baseline stop before any production command', async t => {
  const f = fixture(t); const root = path.join(f.root, 'hp'); fs.mkdirSync(path.join(root, '.vercel'), { recursive: true });
  fs.writeFileSync(path.join(root, 'README_DEPLOY.md'), 'standard clean deploy');
  fs.writeFileSync(path.join(root, '.vercel/project.json'), JSON.stringify({ projectName: 'expected', projectId: 'expected-id' }));
  hpFixture(f); writeHpFixture(root);
  f.state.inputs.hpRoot = root; f.state.inputs.hpBaseCommit = 'c'.repeat(40);
  f.state.inputs.hpProjectSha256 = fileFact(path.join(root, '.vercel/project.json')).sha256;
  let dirty = ''; let target = 'expected expected-id';
  const run = async (tool, args) => {
    if (tool === 'vercel') return target;
    if (args[0] === 'branch') return 'main';
    if (args[0] === 'status') return dirty;
    return f.state.inputs.hpBaseCommit;
  };
  await hpPreflight(f.state, run);
  dirty = ' M other.html'; await assert.rejects(hpPreflight(f.state, run), Checkpoint);
  dirty = ''; target = 'different'; await assert.rejects(hpPreflight(f.state, run), /project name\/ID/);
  target = 'expected expected-id'; f.state.inputs.date = '2999-01-01';
  await assert.rejects(hpPreflight(f.state, run), /after today in Tokyo/);
});

test('HP workflow resumes only its own planned changes and uses the standard verified deployment once', async t => {
  const f = fixture(t); const root = path.join(f.root, 'hp'); fs.mkdirSync(path.join(root, '.vercel'), { recursive: true });
  fs.writeFileSync(path.join(root, 'README_DEPLOY.md'), 'standard clean deployment');
  fs.writeFileSync(path.join(root, '.vercel/project.json'), JSON.stringify({ projectName: 'expected', projectId: 'expected-id' }));
  hpFixture(f); writeHpFixture(root);
  f.state.inputs.hpRoot = root; f.state.inputs.hpBaseCommit = 'c'.repeat(40);
  f.state.inputs.hpProjectSha256 = fileFact(path.join(root, '.vercel/project.json')).sha256;
  let head = f.state.inputs.hpBaseCommit; let remote = head; let failTests = true; let auditBlocked = true; let deployed = false;
  const calls = [];
  const run = async (tool, args) => {
    calls.push([tool, args]);
    if (tool === 'vercel') { assert.deepEqual(args, ['project', 'inspect', '--non-interactive']); return 'expected expected-id'; }
    if (tool === 'npm') { if (failTests) throw new Error('HP tests failed'); if (args[0] === 'run' && auditBlocked) throw new Error('HP audit failed'); return ''; }
    if (tool === 'bash') { assert.deepEqual(args, ['scripts/deploy-production-clean.sh']); deployed = true; return ''; }
    assert.equal(tool, 'git');
    if (args[0] === 'branch') return 'main';
    if (args[0] === 'rev-parse') return head;
    if (args[0] === 'ls-remote') return remote;
    if (args[0] === 'status') return f.state.hpPlanned && head === f.state.inputs.hpBaseCommit ? ' M hypha.html' : '';
    if (args[0] === 'diff') return args.includes('--cached') ? '' : HP_FILES.join('\n');
    if (args[0] === 'ls-files') return '';
    if (args[0] === 'commit') head = 'd'.repeat(40);
    if (args[0] === 'push') { assert.ok(!args.includes('--force')); remote = head; }
    return '';
  };
  const fetcher=publicHpFetcher(f.state,root,{deployed:()=>deployed,head:()=>head});
  await assert.rejects(publishHp(f.state, run, () => {}, fetcher), /tests failed/);
  assert.ok(f.state.hpPlanned); assert.equal(f.state.hpCommit, undefined);
  failTests = false;
  await assert.rejects(publishHp(f.state, run, () => {}, fetcher), /audit failed/);
  assert.equal(calls.filter(([tool]) => tool === 'bash').length, 0);
  auditBlocked = false;
  await publishHp(f.state, run, () => {}, fetcher);
  assert.equal(f.state.hpCommit, head);
  assert.equal((await publishHp(f.state, run, () => {}, fetcher)).reused, true);
  assert.equal(calls.filter(([tool, args]) => tool === 'git' && args[0] === 'commit').length, 1);
  assert.equal(calls.filter(([tool]) => tool === 'bash').length, 1);
  assert.equal(calls.filter(([tool, args]) => tool === 'git' && args[0] === 'push').length, 1);
});

test('reviewed bilingual publication copy is idempotent and rejects stale or duplicated notices', () => {
  const record = makeHpRecord('1.2.3', '2026-10-01', facts());
  const notice = hpReleaseNotice(record);
  notice.en.summary += ' Known issue: temporary limitation.';
  notice.ja.summary += ' 既知の点：一時的な制限があります。';
  const first = updateHpDocuments(documents(), '1.2.3', '2026-10-01', facts(), notice);
  assert.deepEqual(JSON.parse(first[3]).items[0], notice);
  assert.deepEqual(updateHpDocuments(first, '1.2.3', '2026-10-01', facts(), notice), first);
  const stale = [...first]; const staleNews = JSON.parse(stale[3]); staleNews.items[0].ja.summary = '古い内容'; stale[3] = JSON.stringify(staleNews);
  assert.throws(() => updateHpDocuments(stale, '1.2.3', '2026-10-01', facts(), notice), /notice/);
  const duplicate = [...first]; const duplicateNews = JSON.parse(duplicate[3]); duplicateNews.items.push(duplicateNews.items[0]); duplicate[3] = JSON.stringify(duplicateNews);
  assert.throws(() => updateHpDocuments(duplicate, '1.2.3', '2026-10-01', facts(), notice), /notice/);
});


test('public HP accepts later deployment only when the entire Hypha record and bilingual notice agree', async t => {
  const f = fixture(t); hpFixture(f); f.state.hpCommit = 'c'.repeat(40);
  const root = path.join(f.root, 'public-hp'); fs.mkdirSync(root); writeHpFixture(root);
  const docs = updateHpDocuments(documents(), '1.2.3', '2026-10-01', f.state.stages.packages.facts);
  for (const [i, file] of HP_FILES.entries()) fs.writeFileSync(path.join(root, file), docs[i]);
  const otherProductUpdate = JSON.parse(docs[3]); otherProductUpdate.items.find(item => item.products.includes('os')).en.summary = 'Updated other product notice.';
  fs.writeFileSync(path.join(root, 'data/news.json'), JSON.stringify(otherProductUpdate));
  const fetcher = publicHpFetcher(f.state, root, { head: () => 'd'.repeat(40) });
  const result = await verifyPublicHp(f.state, fetcher);
  assert.equal(result.deploymentCommit, 'd'.repeat(40));
  for (const mutation of ['version', 'hash', 'notice', 'duplicate', 'duplicate-version']) {
    const broken = async (url, options) => {
      if (url.includes('/updates/hypha.json') && ['version', 'hash'].includes(mutation)) {
        const record = JSON.parse(docs[2]);
        if (mutation === 'version') record.version = '1.2.2'; else record.downloads.macos.sha256 = 'e'.repeat(64);
        return new Response(JSON.stringify(record));
      }
      if (new URL(url).pathname === '/ja/' && ['notice', 'duplicate', 'duplicate-version'].includes(mutation)) {
        const news = JSON.parse(docs[3]);
        if (mutation === 'notice') news.items[0].ja.summary = 'old';
        else news.items.push({ ...news.items[0], ...(mutation === 'duplicate-version' ? { id: `hypha-1-2-3-release-20260930` } : {}) });
        return new Response(renderedNews(news.items, 'ja'));
      }
      return fetcher(url, options);
    };
    await assert.rejects(verifyPublicHp(f.state, broken), undefined, mutation);
  }
});


test('published notice uses the same approved bilingual input and rejects later changes', async t => {
  const f = fixture(t); hpFixture(f); f.state.hpCommit = 'c'.repeat(40);
  const root = path.join(f.root, 'public-hp'); fs.mkdirSync(root); writeHpFixture(root);
  const record = makeHpRecord(f.state.candidate.version, f.state.inputs.date, f.state.stages.packages.facts);
  const notice = hpReleaseNotice(record); notice.en.summary += ' Known issue.'; notice.ja.summary += ' 既知の点。';
  const notes = path.join(f.root, f.state.inputs.notes); fs.writeFileSync(notes, 'reviewed notes');
  const input = path.join(f.root, 'public.json');
  fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version: record.version, date: record.published_at, notice }));
  f.state.publication = publicationBinding(f.state, input);
  const docs = updateHpDocuments(documents(), record.version, record.published_at, f.state.stages.packages.facts, notice);
  for (const [i, file] of HP_FILES.entries()) fs.writeFileSync(path.join(root, file), docs[i]);
  await verifyPublicHp(f.state, publicHpFetcher(f.state, root));
  fs.writeFileSync(notes, 'changed after approval');
  await assert.rejects(verifyPublicHp(f.state, publicHpFetcher(f.state, root)), /changed/);
});

test('HP remote advancing during update stops before commit or push and requires a new plan', async t => {
  const f = fixture(t); const root = path.join(f.root, 'hp'); fs.mkdirSync(path.join(root, '.vercel'), { recursive: true });
  fs.writeFileSync(path.join(root, 'README_DEPLOY.md'), 'standard deploy');
  fs.writeFileSync(path.join(root, '.vercel/project.json'), JSON.stringify({ projectName: 'expected', projectId: 'expected-id' }));
  hpFixture(f); writeHpFixture(root); f.state.inputs.hpRoot = root; f.state.inputs.hpBaseCommit = 'c'.repeat(40);
  f.state.inputs.hpProjectSha256 = fileFact(path.join(root, '.vercel/project.json')).sha256;
  let remote = f.state.inputs.hpBaseCommit; const calls = [];
  const run = async (tool, args) => {
    calls.push([tool, args]);
    if (tool === 'vercel') return 'expected expected-id';
    if (tool === 'npm') { remote = 'd'.repeat(40); return ''; }
    if (args[0] === 'branch') return 'main';
    if (args[0] === 'rev-parse') return f.state.inputs.hpBaseCommit;
    if (args[0] === 'ls-remote') return remote;
    if (args[0] === 'status' || args[0] === 'ls-files') return '';
    if (args[0] === 'diff') return args.includes('--cached') ? '' : HP_FILES.join('\n');
    return '';
  };
  await assert.rejects(publishHp(f.state, run, () => {}, async () => new Response('', { status: 503 })), /remote advanced/);
  assert.equal(calls.some(([tool, args]) => tool === 'git' && ['add', 'commit', 'push'].includes(args[0])), false);
  assert.equal(calls.some(([tool]) => tool === 'bash'), false);
  assert.ok(f.state.hpPlanned); assert.equal(f.state.hpCommit, undefined);
});

test('a deployment changing during public readback is rejected rather than mixed', async t => {
  const f = fixture(t); hpFixture(f); f.state.hpCommit = 'c'.repeat(40);
  const root = path.join(f.root, 'public-hp'); fs.mkdirSync(root); writeHpFixture(root);
  const docs = updateHpDocuments(documents(), '1.2.3', '2026-10-01', f.state.stages.packages.facts);
  for (const [i, file] of HP_FILES.entries()) fs.writeFileSync(path.join(root, file), docs[i]);
  let reads = 0;
  const fetcher = publicHpFetcher(f.state, root, { head: () => ++reads === 1 ? 'c'.repeat(40) : 'd'.repeat(40) });
  await assert.rejects(verifyPublicHp(f.state, fetcher), /deployment.*changed/i);
});

test('A7 and LS reach their own checks without a local HP checkout', async t => {
  const { realActions } = await import('../release_hypha.mjs');
  const { digest, reportTemplate, POST_TESTS } = await import('./hypha_release_contract.mjs');
  const f = fixture(t); hpFixture(f); f.state.hpCommit = 'c'.repeat(40);
  f.state.inputs.hpRoot = path.join(f.root, 'missing-hp-checkout');
  f.state.inputs.postreleaseReport = 'release_state/a7.json'; f.state.inputs.lsReport = 'release_state/ls.json';
  f.state.inputs.lsState = 'release_state/targets.json'; fs.mkdirSync(path.join(f.root, 'release_state'));
  fs.writeFileSync(path.join(f.root, f.state.inputs.lsState), JSON.stringify({ lemonSqueezy: { products: [{ productId: 1 }] } }));
  const report = reportTemplate(f.state, 'A7', digest(f.state.stages.packages.facts), POST_TESTS);
  report.tests = report.tests.map(row => ({ ...row, result: 'PASS', measured: row.expected,
    oracle: 'retained fixture evidence', evidence: [fileFact(f.payload)] }));
  fs.writeFileSync(path.join(f.root, f.state.inputs.postreleaseReport), JSON.stringify(report));
  const root = path.join(f.root, 'public-hp'); fs.mkdirSync(root); writeHpFixture(root);
  const docs = updateHpDocuments(documents(), '1.2.3', f.state.inputs.date, f.state.stages.packages.facts);
  for (const [i, file] of HP_FILES.entries()) fs.writeFileSync(path.join(root, file), docs[i]);
  const hpFetcher = publicHpFetcher(f.state, root);
  const fetcher = async (url, options) => {
    if (url.startsWith('https://github.com/')) {
      const name = decodeURIComponent(url.split('/').at(-1));
      return new Response(fs.readFileSync([...f.state.stages.packages.facts, ...f.state.stages.provenance.publicFacts].find(fact => path.basename(fact.path) === name).path));
    }
    return hpFetcher(url, options);
  };
  const actions = realActions(f.state, { publishApproved: f.state.candidate.id }, async () => { throw new Error('unexpected command'); }, () => {}, fetcher);
  const result = await actions.postrelease(); assert.equal(result.evidence.publicEvidence.deploymentCommit, f.state.hpCommit);
  await assert.rejects(actions.ls(), /A6-LS.*not PASS/);
});

test('reviewed notice rejects text that the public news renderer cannot build', () => {
  const record = makeHpRecord('1.2.3', '2026-10-01', facts());
  for (const alter of [
    notice => { notice.en.summary += '\nline'; },
    notice => { notice.ja.titlePhrases = ['incorrect']; },
    notice => { notice.en.summary = 'x'.repeat(1001); },
    notice => { notice.en.title = ' leading'; },
  ]) {
    const notice = hpReleaseNotice(record); alter(notice);
    assert.throws(() => updateHpDocuments(documents(), record.version, record.published_at, facts(), notice), /notice/i);
  }
});
