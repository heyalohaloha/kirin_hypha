// Read-only Lemon Squeezy JSON:API verification. Never return credentials or response bodies.
import { spawnSync } from 'node:child_process';
import { setTimeout as delay } from 'node:timers/promises';

const API = 'https://api.lemonsqueezy.com/v1/';
export const LS_RETRY_LIMIT = 3;
const id = value => /^[1-9]\d*$/.test(String(value ?? ''));
const safeId = value => id(value) ? String(value) : 'invalid';
const safeStatus = value => ['pending', 'draft', 'published'].includes(value) ? value : 'other';
class LsApiError extends Error {
  constructor(checks, exitCode) {
    super(`LS API ${exitCode === 2 ? 'unconfirmed' : 'mismatch'}: ${checks.at(-1).id}`);
    this.exitCode = exitCode; this.checks = [...checks];
  }
}
export function loadApiKey(env = process.env, invoke = spawnSync) {
  if (env.LEMONSQUEEZY_API_KEY?.trim()) return env.LEMONSQUEEZY_API_KEY.trim();
  if (env.LEMONSQUEEZY_API_KEYCHAIN_SERVICE) {
    const result = invoke('security', ['find-generic-password', '-s', env.LEMONSQUEEZY_API_KEYCHAIN_SERVICE, '-w'],
      { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
    if (result.status === 0 && result.stdout?.trim()) return result.stdout.trim();
  }
  throw new Error('LS API credential unavailable; configure the environment or Keychain service at runtime');
}
function lsReader(checks, check, { fetcher = fetch, key = loadApiKey, wait = delay } = {}) {
  let credential;
  try { credential = key(); if (!credential?.trim()) throw new Error(); }
  catch {
    checks.push({ id: 'LS/credential-readable', ok: false, status: 'UNCONFIRMED', expected: true, actual: false });
    throw new LsApiError(checks, 2);
  }
  async function get(url, name) {
    const u = new URL(url);
    check(`${name}/endpoint`, u.origin === new URL(API).origin && u.pathname.startsWith('/v1/')
      && !u.username && !u.password, 'configured API origin and path', 'endpoint validation');
    for (let attempt = 1; attempt <= LS_RETRY_LIMIT; attempt++) {
      let response;
      try {
        response = await fetcher(u.href, { method: 'GET', redirect: 'error', signal: AbortSignal.timeout(15000),
          headers: { Accept: 'application/vnd.api+json', Authorization: `Bearer ${credential}` } });
      } catch { /* Transport error details may contain credentials or signed URLs. */ }
      let status = response?.status ?? (response?.ok ? 200 : 'unavailable');
      if (response?.ok) {
        let data;
        try { data = await response.json(); }
        catch (error) {
          if (error instanceof SyntaxError) check(`${name}/json`, false, 'JSON object', 'invalid');
          // The response can lose its connection while reading the body after HTTP headers.
          response = undefined; status = 'body unavailable';
        }
        if (response) {
          check(`${name}/json`, !!data && typeof data === 'object', 'JSON object', data ? 'object' : 'invalid');
          return data;
        }
      }
      const unavailable = !response || status === 'unavailable' || [401, 403, 429].includes(status)
        || (Number.isInteger(status) && status >= 500 && status <= 599);
      if (!unavailable) check(`${name}/http`, false, 200, Number.isInteger(status) ? status : 'invalid');
      if (attempt < LS_RETRY_LIMIT) await wait(500 * attempt);
      else {
        checks.push({ id: `${name}/http`, ok: false, status: 'UNCONFIRMED', expected: 200, actual: status, attempts: attempt });
        throw new LsApiError(checks, 2);
      }
    }
  }
  return get;
}
export async function verifyLsApi(products, artifacts, options = {}) {
  const checks = [];
  const check = (name, ok, expected, actual) => {
    checks.push({ id: name, ok, status: ok ? 'PASS' : 'FAIL', expected, actual });
    if (!ok) throw new LsApiError(checks, 1);
  };
  check('LS/targets', products.length > 0 && artifacts.length > 0
    && products.every(p => id(p.productId) && id(p.variantId)), 'configured numeric targets and artifacts', 'configuration');
  const get = lsReader(checks, check, options);
  for (const product of products) {
    const prefix = `LS/${product.productId}`;
    const record = (await get(`${API}products/${product.productId}`, `${prefix}/product`)).data;
    const variant = (await get(`${API}variants/${product.variantId}`, `${prefix}/variant`)).data;
    check(`${prefix}/product/id`, record?.type === 'products' && String(record.id) === String(product.productId), String(product.productId), safeId(record?.id));
    check(`${prefix}/product/name`, record.attributes?.name === product.productName, 'configured name', record.attributes?.name === product.productName ? 'matches' : 'differs');
    check(`${prefix}/product/status`, record.attributes?.status === 'published', 'published', safeStatus(record.attributes?.status));
    check(`${prefix}/product/live`, record.attributes?.test_mode === false, false, typeof record.attributes?.test_mode === 'boolean' ? record.attributes.test_mode : 'invalid');
    check(`${prefix}/variant/id`, variant?.type === 'variants' && String(variant.id) === String(product.variantId), String(product.variantId), safeId(variant?.id));
    check(`${prefix}/variant/product_id`, String(variant.attributes?.product_id) === String(product.productId), String(product.productId), safeId(variant.attributes?.product_id));
    // A sole default variant is pending even though its product is published.
    check(`${prefix}/variant/status`, ['published', 'pending'].includes(variant.attributes?.status), ['published', 'pending'], safeStatus(variant.attributes?.status));
    check(`${prefix}/variant/live`, variant.attributes?.test_mode === false, false, typeof variant.attributes?.test_mode === 'boolean' ? variant.attributes.test_mode : 'invalid');
    const files = [], seen = new Set();
    let next = `${API}files?filter[variant_id]=${product.variantId}&page[size]=100`, pageNumber = 0;
    while (next) {
      let u;
      try { u = new URL(next, API); } catch { check(`${prefix}/files/pagination`, false, 'valid page', 'invalid'); }
      const number = Number(u.searchParams.get('page[number]') || 1);
      const filter = u.searchParams.get('filter[variant_id]');
      check(`${prefix}/files/pagination`, u.origin === new URL(API).origin && u.pathname === '/v1/files'
        && !u.username && !u.password && (filter == null || filter === String(product.variantId))
        && Number.isSafeInteger(number) && number > pageNumber && !seen.has(u.href), 'same endpoint and advancing page', 'pagination validation');
      // Keep the target filter even when the API omits it from the next-page link.
      u.searchParams.set('filter[variant_id]', String(product.variantId));
      seen.add(u.href); pageNumber = number;
      const page = await get(u.href, `${prefix}/files/page-${number}`);
      check(`${prefix}/files/data`, Array.isArray(page.data), 'array', Array.isArray(page.data) ? 'array' : 'invalid');
      files.push(...page.data); next = page.links?.next || null;
    }
    for (const [index, file] of files.entries()) {
      const name = `${prefix}/files/${id(file?.id) ? file.id : `index-${index}`}`;
      check(`${name}/id`, file?.type === 'files' && id(file.id), 'numeric file identity', safeId(file?.id));
      check(`${name}/variant_id`, String(file.attributes?.variant_id) === String(product.variantId), String(product.variantId), safeId(file.attributes?.variant_id));
    }
    check(`${prefix}/files/unique`, new Set(files.map(file => file.id)).size === files.length, true, new Set(files.map(file => file.id)).size === files.length);
    if (product.expectedFilesCount != null) check(`${prefix}/files/count`, files.length === product.expectedFilesCount, product.expectedFilesCount, files.length);
    for (const [index, artifact] of artifacts.entries()) {
      const matches = files.filter(file => file.attributes?.name === artifact.fileName);
      const matchName = `${prefix}/artifact-${index}`;
      check(`${matchName}/matches`, matches.length === 1, 1, matches.length);
      const file = matches[0], attrs = file.attributes, name = `${prefix}/files/${file.id}`;
      check(`${name}/status`, attrs.status === 'published', 'published', safeStatus(attrs.status));
      check(`${name}/live`, attrs.test_mode === false, false, typeof attrs.test_mode === 'boolean' ? attrs.test_mode : 'invalid');
      check(`${name}/size`, Number.isSafeInteger(attrs.size) && attrs.size > 0 && attrs.size === artifact.size, artifact.size,
        Number.isSafeInteger(attrs.size) ? attrs.size : 'invalid');
      // API metadata does not replace retained download/hash evidence.
      checks.push({ id: `${prefix}/${file.id}`, ok: true, fileName: artifact.fileName, bytes: attrs.size });
    }
  }
  return checks;
}
export async function lsApiChecks(products, artifacts, options) {
  try { return await verifyLsApi(products, artifacts, options); }
  catch (error) { if (error instanceof LsApiError) return error.checks; throw error; }
}

export async function probeLsProduct(productId, options) {
  if (!id(productId)) throw new Error('Configure a numeric LS product for the read-only probe');
  const checks = [];
  const check = (name, ok, expected, actual) => {
    checks.push({ id: name, ok, status: ok ? 'PASS' : 'FAIL', expected, actual });
    if (!ok) throw new LsApiError(checks, 1);
  };
  const get = lsReader(checks, check, options);
  const record = (await get(`${API}products/${productId}`, 'LS/product-readable')).data;
  check('LS/product-readable/id', record?.type === 'products' && String(record.id) === String(productId), String(productId), safeId(record?.id));
  return true;
}
