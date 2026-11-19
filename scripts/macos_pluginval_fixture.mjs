// Exercise the unmodified macOS gate in an owned disposable checkout. No vendor
// tool, network request, quarantine operation or real plugin is used here.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const sourceURL = new URL('./validate_macos_pluginval.sh', import.meta.url);
const officialURL = 'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip';
const stub = String.raw`
const fs = require('node:fs'), path = require('node:path');
const { spawnSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const args = process.argv.slice(2), tool = path.basename(process.argv[1]);
const root = process.env.FIXTURE_ROOT;
const hash = p => createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const owned = p => {
  const full = path.resolve(p);
  if (!full.startsWith(root + path.sep)) throw Error('fixture path escaped: ' + p);
  return full;
};
const event = data => fs.appendFileSync(path.join(root, 'events.jsonl'), JSON.stringify({tool,...data})+'\n');
if (tool === 'uname') { console.log('Darwin'); }
else if (tool === 'node') {
  const verifying = args[0].endsWith('pluginval_archive.mjs') && args[1] === 'verify';
  if (verifying) event({operation:'verify', archive:owned(args[4]), sha256:hash(args[4])});
  const result = spawnSync(process.execPath, args, {stdio:'inherit', env:process.env, timeout:10000});
  if (verifying) event({operation:'verified', status:result.status});
  process.exit(result.status ?? 99);
} else if (tool === 'curl') {
  const url=args[args.indexOf('-o')-1];
  event({operation:'download',url});
  if(url!==process.env.FIXTURE_OFFICIAL_URL) { console.error('unexpected download URL'); process.exit(1); }
  fs.copyFileSync(path.join(root, 'good.zip'), owned(args[args.indexOf('-o')+1]));
} else if (tool === 'unzip') {
  if (process.env.FIXTURE_SWAP === '1') {
    // Deterministic interleaving: a second writer atomically replaces only the
    // shared cache after the verifier returned and before the extractor opens.
    const cached = path.join(root,'cache','pluginval_macOS.zip');
    fs.copyFileSync(path.join(root,'bad.zip'), cached+'.next');
    fs.renameSync(cached+'.next',cached);
    event({operation:'shared-cache-replaced',sha256:hash(cached)});
  }
  const archive=owned(args[1]), stage=owned(args[args.indexOf('-d')+1]);
  event({operation:'extract',archive,stage,sha256:hash(archive)});
  const executable=path.join(stage,'pluginval.app','Contents','MacOS','pluginval');
  fs.mkdirSync(path.dirname(executable),{recursive:true});
  fs.copyFileSync(path.join(root,'bin','pluginval'),executable);fs.chmodSync(executable,0o755);
} else if (tool === 'ditto') {
  event({operation:'stage'});fs.cpSync(owned(args[0]),owned(args[1]),{recursive:true});
} else if (tool === 'xattr') { event({operation:'quarantine-stub'}); }
else if (tool === 'sleep') { event({operation:'retry-wait'}); }
else if (tool === 'cargo') { event({operation:'bundle-check'}); }
else if (tool === 'pluginval') {
  owned(process.env.HOME);owned(process.env.TMPDIR);owned(process.env.KIRIN_HYPHA_TEST_STORAGE_ROOT);
  event({operation:'execute',executable:owned(process.argv[1]),bundle:owned(args.at(-1)),home:process.env.HOME});
} else throw Error('unregistered fixture command: '+tool);
`;

export function runGateFixture(kind, { script = fs.readFileSync(sourceURL, 'utf8') } = {}) {
  assert.ok(['ci-override','local-override','bad-cache','bad-download','cached','download','cache-swap','cache-directory','cache-symlink'].includes(kind));
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'pluginval-gate-')));
  const write = (name, bytes, executable = false) => {
    const file = path.join(root, name); fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, bytes, executable ? { mode: 0o755 } : undefined); return file;
  };
  try {
    const good = Buffer.from('504b0506000000000000000000000000000000000000', 'hex');
    const bad = Buffer.from(good); bad[5] ^= 1;
    write('good.zip', kind === 'bad-download' ? bad : good); write('bad.zip', bad);
    write('scripts/validate_macos_pluginval.sh', script);
    write('scripts/pluginval_archive.mjs', fs.readFileSync(new URL('./pluginval_archive.mjs', import.meta.url)));
    write('scripts/pluginval_archives.json', JSON.stringify({ versions: { 'v1.0.4': { macOS: {
      url:officialURL, bytes:good.length, sha256:sha(good), executable:'pluginval.app/Contents/MacOS/pluginval',
    } } } }));
    write('scripts/ls_release/kirin_hypha_ship_bundles.mjs', `
import path from 'node:path';
for (const role of ['PRE','POST']) console.log([path.join(process.env.FIXTURE_ROOT,'build',role+'.vst3'),role+'.vst3',role].join('\t'));
`);
    for (const tool of ['uname','node','curl','unzip','ditto','xattr','cargo','pluginval','sleep'])
      write('bin/'+tool, '#!'+process.execPath+'\n'+stub, true);
    for (const name of ['build/PRE.vst3','build/POST.vst3','cache','output','home','tmp'])
      fs.mkdirSync(path.join(root,name),{recursive:true});
    if (!['download','bad-download','cache-directory','cache-symlink'].includes(kind))
      write('cache/pluginval_macOS.zip', kind === 'bad-cache' ? bad : good);
    if(kind==='cache-directory') fs.mkdirSync(path.join(root,'cache','pluginval_macOS.zip'));
    if(kind==='cache-symlink') fs.symlinkSync(path.join(root,'absent.zip'),path.join(root,'cache','pluginval_macOS.zip'));
    const env = {
      PATH:path.join(root,'bin')+path.delimiter+'/usr/bin:/bin',
      HOME:path.join(root,'home'), TMPDIR:path.join(root,'tmp'),
      FIXTURE_ROOT:root, FIXTURE_OFFICIAL_URL:officialURL, FIXTURE_SWAP:kind==='cache-swap'?'1':'0',
      CI:kind==='ci-override'?'true':'',
      PLUGINVAL_CACHE_DIR:path.join(root,'cache'), PLUGINVAL_OUTPUT_DIR:path.join(root,'output'),
      PLUGINVAL_RUNTIME_DIR:path.join(root,'runtime'),
    };
    if (kind.endsWith('override')) env.PLUGINVAL_BIN=path.join(root,'bin','pluginval');
    const result=spawnSync('/bin/bash',['scripts/validate_macos_pluginval.sh',path.join(root,'build')],
      {cwd:root, env, encoding:'utf8', timeout:20000, maxBuffer:1024*1024});
    assert.ifError(result.error); assert.equal(result.signal,null,'fixture must exit normally');
    const eventsFile=path.join(root,'events.jsonl');
    const events=fs.existsSync(eventsFile)?fs.readFileSync(eventsFile,'utf8').trim().split('\n').filter(Boolean).map(JSON.parse):[];
    const cache=path.join(root,'cache','pluginval_macOS.zip');
    return { kind,status:result.status,stdout:result.stdout,stderr:result.stderr,events,
      goodSha256:sha(good),badSha256:sha(bad),cacheSha256:fs.existsSync(cache)&&fs.statSync(cache).isFile()?sha(fs.readFileSync(cache)):null,
      cacheIsSymlink:fs.existsSync(cache)||kind==='cache-symlink'?fs.lstatSync(cache).isSymbolicLink():false,
      cacheIsDirectory:fs.existsSync(cache)&&fs.statSync(cache).isDirectory(),tempRoot:path.join(root,'tmp'),
      cachePath:cache,cacheEntries:fs.readdirSync(path.dirname(cache)),scriptSha256:sha(script) };
  } finally { fs.rmSync(root,{recursive:true,force:true}); }
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href===import.meta.url)
  console.log(JSON.stringify(runGateFixture(process.argv[2])));
