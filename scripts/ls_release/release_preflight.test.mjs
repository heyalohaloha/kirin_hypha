import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { inspectPreflight } from './release_preflight.mjs';

test('preflight reads saved preferences and permissions without changing them; manual gates remain explicit', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-preflight-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const profile = path.join(root, 'Profile'), bundle = path.join(root, 'bundle');
  fs.mkdirSync(profile); fs.mkdirSync(bundle);
  const prefs = path.join(profile, 'Preferences'), local = path.join(root, 'Local State');
  fs.writeFileSync(prefs, JSON.stringify({ translate: { enabled: false } }));
  fs.writeFileSync(local, JSON.stringify({ profile: { last_used: 'Profile' } }));
  const before = fs.readFileSync(prefs), mode = fs.statSync(bundle).mode;
  const config = { chromeProfile: profile, macAaxBundles: [bundle], githubOrganization: 'fixture-org' };
  const github = endpoint => {
    assert.equal(endpoint, 'organizations/fixture-org/settings/billing/budgets');
    return { budgets: [{ budget_product_skus: ['actions'], budget_amount: 10 }], has_next_page: false };
  };
  const credential = crypto.randomBytes(24).toString('hex');
  const ls = { lsKey: () => credential, fetcher: async () => { throw new Error('no real API'); }, wait: async () => {} };
  const result = await inspectPreflight(config, { github, ...ls });
  for (const id of ['chrome-saved-profile', 'chrome-saved-translation-off', 'mac-aax-writable', 'actions-budget-readable'])
    assert.equal(result.checks.find(c => c.id === id).result, 'PASS');
  assert.ok(result.checks.some(c => c.id === 'ls-upload-login' && c.result === 'MANUAL'));
  assert.ok(result.checks.some(c => c.id === 'actions-budget-and-signing-allowance' && c.result === 'MANUAL'));
  assert.deepEqual(fs.readFileSync(prefs), before); assert.equal(fs.statSync(bundle).mode, mode);
  fs.writeFileSync(prefs, '{}'); fs.writeFileSync(local, '{}');
  const bad = await inspectPreflight(config, { ...ls, github: () => { throw new Error('unavailable'); } });
  assert.ok(bad.checks.filter(c => c.result === 'CHECKPOINT').length >= 3);
  assert.equal((await inspectPreflight({}, ls)).checks.find(c => c.id === 'mac-aax-writable').result, 'CHECKPOINT');
});

test('personal usage and LS credential/product access are read-only; unavailable access stays unconfirmed', async () => {
  const credential = crypto.randomBytes(24).toString('hex');
  const calls = [], options = { github: endpoint => {
    calls.push(endpoint); return { usageItems: [{ product: 'Actions', quantity: 100, netAmount: 0.8 }] };
  }, lsKey: () => credential, fetcher: async (url, request) => {
    assert.equal(url, 'https://api.lemonsqueezy.com/v1/products/11'); assert.equal(request.method, 'GET');
    assert.equal(request.headers.Authorization, `Bearer ${credential}`);
    return { ok: true, json: async () => ({ data: { type: 'products', id: '11' } }) };
  }, wait: async () => {} };
  const config = { githubUser: 'fixture-user', lsProductId: 11 };
  const result = await inspectPreflight(config, options);
  for (const id of ['actions-budget-readable', 'ls-key-readable', 'ls-api-product-readable'])
    assert.equal(result.checks.find(check => check.id === id).result, 'PASS');
  assert.deepEqual(calls, ['users/fixture-user/settings/billing/usage']);
  assert.ok(!JSON.stringify(result).includes(credential));
  const missing = await inspectPreflight(config, { ...options, github: () => { throw new Error(); }, lsKey: () => { throw new Error(); },
    fetcher: () => assert.fail('missing credentials must not send a request') });
  for (const id of ['actions-budget-readable', 'ls-key-readable', 'ls-api-product-readable'])
    assert.equal(missing.checks.find(check => check.id === id).result, 'CHECKPOINT');
  const denied = await inspectPreflight(config, { ...options, fetcher: async () => ({ ok: false, status: 401 }) });
  assert.equal(denied.checks.find(check => check.id === 'ls-api-product-readable').result, 'CHECKPOINT');
});
