#!/usr/bin/env node
// Read-only preparation checks. No application launch, login, chmod, dispatch or budget change.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { loadApiKey, probeLsProduct } from './ls_api_readback.mjs';
import { pathToFileURL } from 'node:url';

function writableTree(directory) {
  const stat = fs.lstatSync(directory);
  if (stat.isSymbolicLink() || !stat.isDirectory()) return false;
  fs.accessSync(directory, fs.constants.W_OK);
  fs.accessSync(path.dirname(directory), fs.constants.W_OK);
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isSymbolicLink()) continue; // PACE compatibility links are not permission targets.
    if (entry.isDirectory()) { if (!writableTree(file)) return false; }
    else fs.accessSync(file, fs.constants.W_OK);
  }
  return true;
}
export async function inspectPreflight(config, { lsKey = loadApiKey, fetcher = fetch, wait, github = endpoint => {
  const r = spawnSync('gh', ['api', '--method', 'GET', endpoint], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  if (r.status !== 0) throw new Error('GitHub read unavailable');
  return JSON.parse(r.stdout);
} } = {}) {
  const checks = [];
  const check = (id, operation) => {
    try { checks.push({ id, result: operation() ? 'PASS' : 'CHECKPOINT' }); }
    catch { checks.push({ id, result: 'CHECKPOINT' }); }
  };
  check('chrome-saved-profile', () => {
    const local = JSON.parse(fs.readFileSync(path.join(path.dirname(config.chromeProfile), 'Local State'), 'utf8'));
    return local.profile?.last_used === path.basename(config.chromeProfile);
  });
  check('chrome-saved-translation-off', () => {
    const prefs = JSON.parse(fs.readFileSync(path.join(config.chromeProfile, 'Preferences'), 'utf8'));
    return prefs.translate?.enabled === false;
  });
  check('mac-aax-writable', () => Array.isArray(config.macAaxBundles) && config.macAaxBundles.length > 0
    && config.macAaxBundles.every(writableTree));
  if (config.githubUser) {
    check('actions-budget-readable', () => {
      if (!/^[A-Za-z0-9][A-Za-z0-9-]{0,38}$/.test(config.githubUser)) return false;
      const report = github(`users/${config.githubUser}/settings/billing/usage`);
      // A readable usage report is not proof of remaining budget; reconcile the limit manually.
      return Array.isArray(report.usageItems) && report.usageItems.every(row => typeof row.product === 'string'
        && Number.isFinite(row.quantity) && Number.isFinite(row.netAmount));
    });
  } else if (config.githubOrganization) {
    check('actions-budget-readable', () => {
      if (!/^[A-Za-z0-9][A-Za-z0-9-]{0,38}$/.test(config.githubOrganization)) return false;
      const report = github(`organizations/${config.githubOrganization}/settings/billing/budgets`);
      // Access does not prove remaining allowance or authorize spending. Full reconciliation is manual.
      return Array.isArray(report.budgets) && report.has_next_page === false
        && report.budgets.some(row => row.budget_product_skus?.some(sku => sku.startsWith('actions')));
    });
  } else checks.push({ id: 'actions-budget-readable', result: 'CHECKPOINT' });
  let credential;
  check('ls-key-readable', () => { credential = lsKey(); return typeof credential === 'string' && credential.trim().length > 0; });
  let productReadable = false;
  if (credential) {
    try { productReadable = await probeLsProduct(config.lsProductId, { key: () => credential, fetcher, wait }); }
    catch { /* Only the outcome is retained, never the key or response. */ }
  }
  check('ls-api-product-readable', () => productReadable);
  for (const id of ['chrome-current-profile-and-settings', 'ls-upload-login', 'ilok-active-side',
    'windows-login', 'actions-budget-and-signing-allowance']) checks.push({ id, result: 'MANUAL' });
  return { schema: 'hypha-release-preflight-v1', checks };
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    if (process.argv.length !== 3) throw new Error('Usage: node scripts/ls_release/release_preflight.mjs PRIVATE_CONFIG_JSON');
    const result = await inspectPreflight(JSON.parse(fs.readFileSync(process.argv[2], 'utf8')));
    console.log(JSON.stringify(result, null, 2));
    if (result.checks.some(c => c.result !== 'PASS')) process.exitCode = 2;
  } catch { console.error('Preflight configuration unreadable'); process.exitCode = 2; }
}
