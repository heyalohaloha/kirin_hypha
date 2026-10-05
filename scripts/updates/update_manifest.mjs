#!/usr/bin/env node
// Offline producer only: no fetch, upload, deploy, CI or production-key generation.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { STAGES } from '../release_hypha.mjs';
import { assertState, checkFacts, digest, hash, readJson, resolveInput, safeStatePath,
  validateReport, DISTRIBUTION_TESTS, POST_TESTS } from '../ls_release/hypha_release_contract.mjs';
import { verifyPackages } from '../ls_release/hypha_release_local.mjs';
import { assertUpdateBinding, updateBinding, validatePublicKey } from './update_key_binding.mjs';

export const MAX_BYTES = 16 * 1024;
export const MAX_LIFETIME = 30 * 24 * 60 * 60;
const FIELDS = ['schema', 'product', 'channel', 'version', 'source_commit', 'published_at',
  'expires_at', 'publication_sequence', 'withdrawn', 'platforms', 'formats'];
const MODULE = fileURLToPath(import.meta.url);
const ROOT = path.resolve(path.dirname(MODULE), '../..');
const GITHUB = 'https://github.com/heyalohaloha/kirin_hypha/releases';

export function validSemVer(value) {
  return typeof value === 'string' && value.length <= 32
    && /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(value)
    && value.split('.').every(p => p.length <= 10 && Number(p) <= 2147483647);
}

function exactFields(value, fields) {
  return value && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).length === fields.length && fields.every(k => Object.hasOwn(value, k));
}

export function validatePayload(value, now) {
  const integer = n => Number.isSafeInteger(n) && n >= 0;
  const set = (a, allowed) => Array.isArray(a) && a.length > 0 && a.length <= allowed.length
    && new Set(a).size === a.length && a.every(x => allowed.includes(x));
  if (!exactFields(value, FIELDS) || value.schema !== 1 || value.product !== 'kirin-hypha'
      || value.channel !== 'stable' || !validSemVer(value.version)
      || typeof value.source_commit !== 'string' || !/^[0-9a-f]{40}$/.test(value.source_commit)
      || !integer(now) || !integer(value.published_at) || !integer(value.expires_at)
      || !integer(value.publication_sequence) || value.publication_sequence === 0
      || typeof value.withdrawn !== 'boolean' || value.published_at > now || value.expires_at <= now
      || value.expires_at <= value.published_at || value.expires_at - value.published_at > MAX_LIFETIME
      || !set(value.platforms, ['macos', 'windows']) || !set(value.formats, ['AU', 'VST3', 'AAX'])
      || (value.platforms.length === 1 && value.platforms[0] === 'windows' && value.formats.includes('AU'))) {
    throw new Error('Invalid stable update payload/schema/time/platform');
  }
  return value;
}

export function jucePublicKey(key) {
  const publicKey = key.type === 'public' ? key : crypto.createPublicKey(key);
  const details = publicKey.asymmetricKeyDetails;
  if (publicKey.asymmetricKeyType !== 'rsa' || details?.modulusLength !== 2048
      || details?.publicExponent !== 65537n) throw new Error('Requires RSA-2048 exponent 65537');
  const jwk = publicKey.export({ format: 'jwk' });
  const modulus = Buffer.from(jwk.n, 'base64url');
  if (modulus.length !== 256 || modulus[0] < 128 || !(modulus[255] & 1)) throw new Error('Invalid RSA modulus');
  return validatePublicKey(`10001,${modulus.toString('hex')}`);
}

// Shared by fixture tests; the publishing CLI additionally requires completed release evidence.
export function signManifest(payload, privateKey, now) {
  validatePayload(payload, now);
  if (privateKey.type !== 'private') throw new Error('External private signing key required');
  jucePublicKey(privateKey);
  const bytes = Buffer.from(JSON.stringify(payload), 'utf8');
  const signature = crypto.sign('sha256', bytes, { key: privateKey, padding: crypto.constants.RSA_PKCS1_PADDING });
  const wire = JSON.stringify({ payload: bytes.toString('base64'), signature: signature.toString('base64') });
  if (signature.length !== 256 || Buffer.byteLength(wire) > MAX_BYTES) throw new Error('Manifest exceeds wire bounds');
  return wire;
}

export function verifyWire(wire, key, now, { allowExpired = false } = {}) {
  if (typeof wire !== 'string' || Buffer.byteLength(wire) > MAX_BYTES) throw new Error('Oversized manifest');
  const envelope = JSON.parse(wire);
  if (!exactFields(envelope, ['payload', 'signature']) || JSON.stringify(envelope) !== wire.trim())
    throw new Error('Invalid/noncanonical wire schema');
  const decode = text => {
    if (typeof text !== 'string' || !/^[A-Za-z0-9+/]+={0,2}$/.test(text) || text.length % 4 !== 0)
      throw new Error('Invalid base64');
    const bytes = Buffer.from(text, 'base64');
    if (bytes.toString('base64') !== text) throw new Error('Noncanonical base64');
    return bytes;
  };
  const bytes = decode(envelope.payload), signature = decode(envelope.signature);
  jucePublicKey(key);
  if (signature.length !== 256 || !crypto.verify('sha256', bytes,
    { key, padding: crypto.constants.RSA_PKCS1_PADDING }, signature)) throw new Error('Invalid signature');
  const text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  const value = JSON.parse(text);
  // Producer format is compact canonical JSON.stringify; duplicates/escaped aliases do not round-trip.
  if (JSON.stringify(value) !== text) throw new Error('Noncanonical payload');
  if (allowExpired && (!Number.isSafeInteger(now) || value.published_at > now))
    throw new Error('Previous manifest is future dated');
  return validatePayload(value, allowExpired ? value.published_at : now);
}

export function payloadFromCompletedRelease(state, { now, expiresAt, sequence, withdrawn = false,
  completedState, previousPayload } = {}) {
  assertState(state);
  if (state.releaseState === 'RELEASE_INCIDENT' && withdrawn) {
    if (!completedState || !previousPayload || !state.blocker
        || completedState.root !== state.root
        || digest(completedState.candidate) !== digest(state.candidate)
        || digest(completedState.source) !== digest(state.source)
        || digest(completedState.freeze) !== digest(state.freeze)
        || digest(completedState.stages.packages?.facts) !== digest(state.stages.packages?.facts)
        || previousPayload.version !== state.candidate.version
        || previousPayload.source_commit !== state.candidate.commit
        || digest(previousPayload.platforms) !== digest(['macos', 'windows'])
        || digest(previousPayload.formats) !== digest(['AU', 'VST3', 'AAX'])) {
      throw new Error('Withdrawal requires retained completed public receipt and the same previous signed release');
    }
    return payloadFromCompletedRelease(completedState, { now, expiresAt, sequence, withdrawn });
  }
  if (state.releaseState !== 'RELEASE_COMPLETE' || state.blocker
      || STAGES.some(stage => state.stages[stage]?.status !== 'PASS')) {
    throw new Error('All three distribution channels and A7 must be complete; no fabricated PASS');
  }
  const { sha256, ...frozen } = state.freeze || {};
  if (!sha256 || digest(frozen) !== sha256 || digest(state.freeze.source) !== digest(state.source)
      || state.freeze.inputs !== digest(state.inputs)
      || state.freeze.criteriaSha256 !== digest({ tests: state.requiredHostTests, expected: state.expected })) {
    throw new Error('Exact candidate freeze/source/criteria changed');
  }
  assertUpdateBinding(state.freeze.updateCheck, state.inputs.updatePublicKey);
  for (const stage of STAGES) if (state.stages[stage].facts) checkFacts(state.stages[stage].facts);
  checkFacts(state.freeze.payloads);
  const packages = verifyPackages(state);
  if (digest(packages) !== digest(state.stages.packages.facts)) throw new Error('Package set differs from verified release');
  validateReport(state, state.inputs.hostsReport, 'A4', state.freeze.sha256, state.requiredHostTests);
  validateReport(state, state.inputs.distributionReport, 'A5', digest(packages), DISTRIBUTION_TESTS);
  validateReport(state, state.inputs.postreleaseReport, 'A7', digest(packages), POST_TESTS);
  const products = readJson(resolveInput(state, state.inputs.lsState)).lemonSqueezy?.products;
  if (!Array.isArray(products) || products.length === 0 || new Set(products.map(p => p.productId)).size !== products.length)
    throw new Error('Exact LS product set missing or duplicate');
  validateReport(state, state.inputs.lsReport, 'A6-LS', digest(packages), products.map(p => `LS/${p.productId}`));
  const ls = readJson(resolveInput(state, state.inputs.lsReport));
  for (const row of ls.tests) if (row.measured?.sha256 !== packages[0].sha256
      || !row.evidence.some(e => e.sha256 === packages[0].sha256)) throw new Error('LS downloaded PKG mismatch');
  const github = state.stages.github.evidence, hp = state.stages.hp.evidence;
  if (github?.url !== `${GITHUB}/tag/v${state.candidate.version}` || !Number.isSafeInteger(github.releaseId)
      || github.releaseId <= 0 || !/^[0-9a-f]{40}$/.test(hp?.hpCommit || '') || hp.hpCommit !== state.hpCommit
      || !Array.isArray(hp.downloads)) throw new Error('Public stable Release / EN+JA HP readback evidence missing');
  for (const fact of [...packages, ...(state.stages.provenance.publicFacts || [])]) {
    const url = `${GITHUB}/download/v${state.candidate.version}/${encodeURIComponent(path.basename(fact.path))}`;
    const hits = hp.downloads.filter(d => d.url === url && d.bytes === fact.bytes && d.sha256 === fact.sha256);
    if (hits.length !== 1) throw new Error('Public HP download bytes are not bound to all release artifacts');
  }
  return validatePayload({ schema: 1, product: 'kirin-hypha', channel: 'stable', version: state.candidate.version,
    source_commit: state.candidate.commit, published_at: now, expires_at: expiresAt,
    publication_sequence: sequence, withdrawn, platforms: ['macos', 'windows'], formats: ['AU', 'VST3', 'AAX'] }, now);
}

export function assertReleaseSigner(state, privateKey) {
  const key = jucePublicKey(privateKey);
  if (!state.inputs?.updatePublicKey) throw new Error('Disabled release has no approved update signer binding');
  assertUpdateBinding(state.freeze?.updateCheck, key);
  if (updateBinding(state.inputs.updatePublicKey).publicKeySha256 !== updateBinding(key).publicKeySha256) {
    throw new Error('Manifest signer differs from the exact distributed binary key');
  }
}

const HELP = `Offline only: node scripts/updates/update_manifest.mjs --state release_state/RELEASE.json
  --key-file /external/private/manifest-key.pem --sequence INTEGER
  --expires-at UNIX_SECONDS --output /staging/hypha.json [--previous FILE | --initial]
  [--withdrawn] [--help]
  --completed-state FILE  Required for incident withdrawal; immutable prior completed receipt
Requires real RELEASE_COMPLETE, all candidate-bound gates and retained bytes.
No network, key generation, upload or deploy. Output is never overwritten.
Production key must be provided outside the product repository; fixture keys are not production keys.
`;

export function runCli(argv, now = Math.floor(Date.now() / 1000)) {
  if (argv.includes('--help')) { console.log(HELP); return; }
  const options = {};
  const fields = { '--state': 'state', '--key-file': 'keyFile', '--sequence': 'sequence',
    '--expires-at': 'expiresAt', '--output': 'output', '--previous': 'previous', '--completed-state': 'completedState' };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (fields[arg]) {
      if (options[fields[arg]] !== undefined || !argv[i + 1] || argv[i + 1].startsWith('--'))
        throw new Error('Missing/duplicate producer option');
      options[fields[arg]] = argv[++i];
    } else if (arg === '--initial' || arg === '--withdrawn') {
      if (options[arg.slice(2)]) throw new Error('Duplicate producer option');
      options[arg.slice(2)] = true;
    } else throw new Error('Unknown producer option');
  }
  if (!options.state || !options.keyFile || !options.output || !/^\d+$/.test(options.sequence || '')
      || !/^\d+$/.test(options.expiresAt || '') || (!!options.initial === !!options.previous))
    throw new Error('Explicit state/key/sequence/expiry/output and previous or initial required');
  const stateFile = safeStatePath(ROOT, options.state);
  const keyFile = fs.realpathSync(options.keyFile);
  const state = readJson(stateFile);
  const productRoot = fs.realpathSync(state.root);
  if ([ROOT, productRoot].some(root => keyFile === root || keyFile.startsWith(`${root}${path.sep}`)))
    throw new Error('Production signing key must not be inside the product repository');
  const key = crypto.createPrivateKey(fs.readFileSync(keyFile));
  assertReleaseSigner(state, key);
  const previous = options.previous ? verifyWire(fs.readFileSync(options.previous, 'utf8'), crypto.createPublicKey(key), now,
    { allowExpired: true }) : undefined;
  const completedState = options.completedState ? readJson(safeStatePath(ROOT, options.completedState)) : undefined;
  if (options.completedState && (!options.withdrawn || !options.previous)) throw new Error('Completed receipt is only for same-release incident withdrawal');
  const payload = payloadFromCompletedRelease(state, { now, expiresAt: Number(options.expiresAt),
    sequence: Number(options.sequence), withdrawn: !!options.withdrawn, previousPayload: previous, completedState });
  if (options.previous) {
    if (payload.publication_sequence <= previous.publication_sequence) throw new Error('Sequence must advance');
    const oldVersion = previous.version.split('.').map(Number), newVersion = payload.version.split('.').map(Number);
    for (let i = 0; i < 3; ++i) {
      if (newVersion[i] < oldVersion[i]) throw new Error('Stable version must not regress');
      if (newVersion[i] > oldVersion[i]) break;
    }
  }
  const wire = signManifest(payload, key, now);
  const output = path.resolve(options.output);
  if (output === stateFile || output === keyFile) throw new Error('Output cannot replace an input');
  fs.writeFileSync(output, wire, { flag: 'wx', mode: 0o644 });
  console.log(JSON.stringify({ version: payload.version, sourceCommit: payload.source_commit,
    sequence: payload.publication_sequence, bytes: Buffer.byteLength(wire), sha256: hash(wire),
    publicKeySha256: hash(jucePublicKey(key)), publication: 'not performed' }));
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE) {
  try { runCli(process.argv.slice(2)); } catch {
    // Imported report errors may contain private LS target IDs or file paths.
    // Do not echo those, provider exceptions or key material into public logs.
    console.error('[hypha-update-manifest] Offline preparation rejected: check the completed release gates and private inputs locally. No publication performed.');
    process.exitCode = 1;
  }
}
