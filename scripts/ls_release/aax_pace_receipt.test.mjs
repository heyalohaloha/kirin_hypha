import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { AAX_PACE_RECEIPT_ENV, AAX_PACE_RECEIPT_SHA256_ENV, paceFromSigningReceipt } from './aax_bundle_verify.mjs';
import { signedAaxReceiptEnv } from './hypha_release_local.mjs';
import { Checkpoint } from './hypha_release_contract.mjs';

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const tree = 'a'.repeat(64);
const entry = { role: 'PRE', bundle: 'Kirin Hypha PRE.aaxplugin', bundle_manifest_sha256: tree,
  pace_signer: 'Signer', pace_signer_guid: 'GUID', pace_publisher_id: 'PUBLISHER' };

function receipt(t, bundles = [entry]) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-pace-receipt-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const file = path.join(dir, 'receipt.json'), bytes = Buffer.from(JSON.stringify({ bundles }));
  fs.writeFileSync(file, bytes);
  return { file, env: { [AAX_PACE_RECEIPT_ENV]: file, [AAX_PACE_RECEIPT_SHA256_ENV]: hash(bytes) } };
}

test('without a selected receipt, PACE itself verifies the bundle', () => {
  assert.equal(paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', tree, {}), null);
});

test('an identical tree carries the PACE signer the signing stage verified', t => {
  const { env } = receipt(t);
  assert.deepEqual(paceFromSigningReceipt('/staged/Kirin Hypha PRE.aaxplugin', tree, env),
    { signerName: 'Signer', signerGuid: 'GUID', publisherId: 'PUBLISHER' });
});

test('a changed tree, receipt, name or missing signer fails instead of skipping PACE', t => {
  const { file, env } = receipt(t);
  assert.throws(() => paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', 'b'.repeat(64), env), /differs/);
  assert.throws(() => paceFromSigningReceipt('/x/Kirin Hypha POST.aaxplugin', tree, env), /differs/);
  assert.throws(() => paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', tree, { ...env, [AAX_PACE_RECEIPT_SHA256_ENV]: '' }), /sha256/);
  fs.appendFileSync(file, ' ');
  assert.throws(() => paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', tree, env), /changed/);
  const unsigned = receipt(t, [{ ...entry, pace_signer: '' }]);
  assert.throws(() => paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', tree, unsigned.env), /signer/);
});

test('the release passes only the receipt the macOS AAX stage recorded', () => {
  const root = '/release';
  const receiptPath = path.resolve(root, 'build-aax-universal/kirin-hypha-macos-aax-notarization.json');
  const state = { root, stages: { 'macos-aax': { status: 'PASS', facts: [{ path: receiptPath, sha256: 'c'.repeat(64) }] } } };
  assert.deepEqual(signedAaxReceiptEnv(state), { [AAX_PACE_RECEIPT_ENV]: receiptPath, [AAX_PACE_RECEIPT_SHA256_ENV]: 'c'.repeat(64) });
  for (const broken of [{ ...state, stages: {} }, { ...state, stages: { 'macos-aax': { status: 'FAIL', facts: state.stages['macos-aax'].facts } } },
    { ...state, stages: { 'macos-aax': { status: 'PASS', facts: [] } } }]) {
    assert.throws(() => signedAaxReceiptEnv(broken), Checkpoint);
  }
});

test('the signing stage clears any receipt in the environment before building and verifying', () => {
  const source = fs.readFileSync(new URL('./hypha_release_local.mjs', import.meta.url), 'utf8');
  assert.match(source, /build_aax_universal\.sh'[^\n]*\n[^\n]*\.\.\.NO_RECEIPT_ENV/);
  assert.match(source, /env: signedAaxReceipt \? signedAaxReceiptEnv\(state\) : NO_RECEIPT_ENV/);
  assert.equal(paceFromSigningReceipt('/x/Kirin Hypha PRE.aaxplugin', tree, { [AAX_PACE_RECEIPT_ENV]: '', [AAX_PACE_RECEIPT_SHA256_ENV]: '' }), null);
});
