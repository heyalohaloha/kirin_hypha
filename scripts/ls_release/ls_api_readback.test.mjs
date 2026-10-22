import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test from 'node:test';
import { verifyLsApi, loadApiKey } from './ls_api_readback.mjs';
import { parseArgs } from './kirin_hypha_ls_dry_run.mjs';

function fixture() {
  const secret = crypto.randomBytes(24).toString('hex'), calls = [];
  const product = { productId: 11, variantId: 22, productName: 'Fixture product' };
  const artifact = { fileName: 'fixture.pkg', size: 1234 };
  const file = { type: 'files', id: '33', attributes: { name: artifact.fileName, variant_id: 22,
    size: 1234, status: 'published', test_mode: false } };
  const records = {
    '/v1/products/11': { data: { type: 'products', id: '11', attributes: { name: product.productName, status: 'published', test_mode: false } } },
    '/v1/variants/22': { data: { type: 'variants', id: '22', attributes: { product_id: 11, status: 'published', test_mode: false } } },
    '/v1/files': { data: [file], links: {} },
  };
  const fetcher = async (url, options) => {
    const u = new URL(url); assert.equal(u.origin, 'https://api.lemonsqueezy.com');
    assert.equal(options.method, 'GET'); assert.equal(options.redirect, 'error');
    assert.equal(options.headers.Authorization, `Bearer ${secret}`); calls.push(u);
    return { ok: true, json: async () => structuredClone(records[u.pathname]) };
  };
  return { secret, calls, product, artifact, file, records, fetcher, key: () => secret, wait: async () => {} };
}
test('LS uses GET product/variant/files, traverses pagination and returns only safe metadata', async () => {
  const f = fixture(), page = f.records['/v1/files'];
  page.links.next = 'https://api.lemonsqueezy.com/v1/files?filter[variant_id]=22&page[number]=2';
  const fetcher = async (url, opts) => {
    const r = await f.fetcher(url, opts);
    if (new URL(url).searchParams.get('page[number]') === '2') return { ok: true, json: async () => ({ data: [], links: {} }) };
    return r;
  };
  const result = await verifyLsApi([f.product], [f.artifact], { fetcher, key: f.key });
  assert.equal(result.find(r => r.bytes).bytes, 1234); assert.ok(result.every(r => r.ok));
  assert.equal(f.calls.filter(u => u.pathname === '/v1/files').length, 2);
  assert.ok(!JSON.stringify(result).includes(f.secret));
  assert.ok(!JSON.stringify(result).includes('download_url'));
  assert.equal(parseArgs(['--state', 'fixture.json', '--with-ls-api']).withLsApi, true);
  assert.throws(() => parseArgs(['--state', 'fixture.json', '--with-ls-chrome']));
});
test('LS rejects wrong associations, mode, size, status, missing and duplicate files', async () => {
  for (const mutate of [
    f => { f.records['/v1/variants/22'].data.attributes.product_id = 99; },
    f => { f.file.attributes.variant_id = 99; }, f => { f.file.attributes.size = 1235; },
    f => { f.file.attributes.test_mode = true; }, f => { f.file.attributes.status = 'draft'; },
    f => { f.records['/v1/products/11'].data.attributes.status = 'draft'; },
    f => { f.records['/v1/files'].data = []; },
    f => { f.records['/v1/files'].data.push({ ...f.file, id: '34' }); },
    f => { f.records['/v1/files'].links.next = 'https://invalid.example/v1/files'; },
    f => { f.records['/v1/files'].links.next = 'https://api.lemonsqueezy.com/v1/files?filter[variant_id]=99'; },
  ]) {
    const f = fixture(); mutate(f);
    await assert.rejects(verifyLsApi([f.product], [f.artifact], f));
  }
});
test('runtime credentials and transport failures never enter error output', async () => {
  const f = fixture();
  assert.equal(loadApiKey({ LEMONSQUEEZY_API_KEY: f.secret }), f.secret);
  assert.equal(loadApiKey({ LEMONSQUEEZY_API_KEYCHAIN_SERVICE: 'fixture-service' }, (tool, args, options) => {
    assert.equal(tool, 'security'); assert.ok(args.includes('-w')); assert.equal(options.stdio[2], 'pipe');
    return { status: 0, stdout: f.secret };
  }), f.secret);
  assert.throws(() => loadApiKey({}));
  for (const fetcher of [async () => { throw new Error(f.secret); }, async () => ({ ok: false })]) {
    await assert.rejects(verifyLsApi([f.product], [f.artifact], { key: f.key, fetcher, wait: async () => {} }), error => !error.message.includes(f.secret));
  }
});

test('a pending default variant is accepted only with the configured live published product', async () => {
  const f = fixture(); f.records['/v1/variants/22'].data.attributes.status = 'pending';
  assert.ok((await verifyLsApi([f.product], [f.artifact], f)).every(check => check.ok));
  for (const mutate of [
    f => { f.records['/v1/variants/22'].data.attributes.product_id = 99; },
    f => { f.records['/v1/variants/22'].data.attributes.test_mode = true; },
    f => { f.records['/v1/products/11'].data.attributes.status = 'draft'; },
  ]) {
    const bad = fixture(); bad.records['/v1/variants/22'].data.attributes.status = 'pending'; mutate(bad);
    await assert.rejects(verifyLsApi([bad.product], [bad.artifact], bad));
  }
});
test('unavailable access is bounded and unconfirmed; a readable mismatch fails immediately', async () => {
  for (const status of [401, 403, 429, 500, 503, null]) {
    const f = fixture(); let calls = 0;
    await assert.rejects(verifyLsApi([f.product], [f.artifact], { key: f.key, wait: async () => {},
      fetcher: async () => { calls++; if (status === null) throw new Error(f.secret);
        return { ok: false, status }; } }), error => error.exitCode === 2 && !JSON.stringify(error).includes(f.secret));
    assert.equal(calls, 3);
  }
  const f = fixture(); let calls = 0;
  await assert.rejects(verifyLsApi([f.product], [f.artifact], { key: () => { throw new Error(f.secret); },
    fetcher: async () => { calls++; } }), error => error.exitCode === 2 && !error.message.includes(f.secret));
  assert.equal(calls, 0);
  f.file.attributes.size++;
  await assert.rejects(verifyLsApi([f.product], [f.artifact], f), error => error.exitCode === 1
    && error.checks.some(check => !check.ok && check.id.endsWith('/size') && check.expected === 1234 && check.actual === 1235));
});

test('pagination may omit the filter but every requested page and returned file stay bound to the variant', async () => {
  const f = fixture(); f.records['/v1/files'].links.next = 'https://api.lemonsqueezy.com/v1/files?page[number]=2&page[size]=100';
  const fetcher = async (url, options) => {
    const u = new URL(url); assert.equal(u.searchParams.get('filter[variant_id]'), u.pathname === '/v1/files' ? '22' : null);
    if (u.searchParams.get('page[number]') === '2') return { ok: true, json: async () => ({ data: [], links: {} }) };
    return f.fetcher(url, options);
  };
  assert.ok((await verifyLsApi([f.product], [f.artifact], { ...f, fetcher })).every(check => check.ok));
  for (const next of ['https://api.lemonsqueezy.com/v1/files?page[number]=1', 'https://api.lemonsqueezy.com/v1/products?page[number]=2']) {
    const bad = fixture(); bad.records['/v1/files'].links.next = next;
    await assert.rejects(verifyLsApi([bad.product], [bad.artifact], bad), error => error.exitCode === 1);
  }
});
test('reported failure checks contain only safe attribute diagnostics, not response text or signed URLs', async () => {
  for (const mutate of [
    f => { f.records['/v1/products/11'].data.attributes.name = f.secret; },
    f => { f.records['/v1/variants/22'].data.attributes.status = f.secret; },
    f => { f.file.attributes.size = f.secret; },
  ]) {
    const f = fixture(); mutate(f);
    await assert.rejects(verifyLsApi([f.product], [f.artifact], f), error => error.exitCode === 1
      && error.checks.some(check => !check.ok && 'expected' in check && 'actual' in check)
      && !JSON.stringify(error.checks).includes(f.secret));
  }
});

test('a retry that recovers leaves a passing report; unavailable and mismatched checks map to distinct exits', async () => {
  const { lsApiChecks } = await import('./ls_api_readback.mjs');
  const { verificationExitCode } = await import('./kirin_hypha_ls_dry_run.mjs');
  const f = fixture(); let attempts = 0;
  const recovered = await verifyLsApi([f.product], [f.artifact], { ...f, fetcher: async (...args) => {
    if (++attempts === 1) return { ok: false, status: 429 };
    return f.fetcher(...args);
  } });
  assert.ok(recovered.every(check => check.ok));
  const unavailable = await lsApiChecks([f.product], [f.artifact], { ...f, fetcher: async () => ({ ok: false, status: 503 }) });
  assert.equal(verificationExitCode(unavailable), 2);
  f.file.attributes.status = 'draft';
  const mismatch = await lsApiChecks([f.product], [f.artifact], f);
  assert.equal(verificationExitCode(mismatch), 1);
  assert.ok(mismatch.some(check => !check.ok && check.id.endsWith('/status')));
});

test('a wrong file association identifies the file and expected/actual variant without its body', async () => {
  const f = fixture(); f.file.attributes.variant_id = 99;
  await assert.rejects(verifyLsApi([f.product], [f.artifact], f), error => error.checks.some(check =>
    check.id === 'LS/11/files/33/variant_id' && check.ok === false && check.expected === '22' && check.actual === '99'));
});

test('body transport loss is unconfirmed while malformed readable JSON fails without retry', async () => {
  const f = fixture(); let calls = 0;
  await assert.rejects(verifyLsApi([f.product], [f.artifact], { ...f, fetcher: async () => {
    calls++; return { ok: true, status: 200, json: async () => { throw new TypeError(f.secret); } };
  } }), error => error.exitCode === 2 && !JSON.stringify(error).includes(f.secret));
  assert.equal(calls, 3); calls = 0;
  await assert.rejects(verifyLsApi([f.product], [f.artifact], { ...f, fetcher: async () => {
    calls++; return { ok: true, status: 200, json: async () => { throw new SyntaxError(f.secret); } };
  } }), error => error.exitCode === 1 && !JSON.stringify(error).includes(f.secret));
  assert.equal(calls, 1);
});
