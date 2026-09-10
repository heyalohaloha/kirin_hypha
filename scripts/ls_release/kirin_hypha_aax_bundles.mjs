#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MODULE_PATH = fileURLToPath(import.meta.url);
const SCRIPT_DIR = path.dirname(MODULE_PATH);
export const PROJECT_ROOT = path.resolve(SCRIPT_DIR, '..', '..');
export const AAX_BUNDLE_MANIFEST_PATH = path.join(
  PROJECT_ROOT,
  'config/hypha_macos_aax_bundles.json',
);

const SCHEMA = 'kirin-hypha-macos-aax-bundles-v1';
const INSTALL_PARENT = 'Library/Application Support/Avid/Audio/Plug-Ins';
const ARCHIVE_PARENT = 'AAX';

function fail(message) {
  throw new Error(`invalid macOS AAX bundle manifest: ${message}`);
}

function validateRelative(value, field) {
  if (typeof value !== 'string' || value.length === 0) fail(`${field} must be non-empty`);
  if (
    path.posix.isAbsolute(value)
    || value.includes('\\')
    || value.split('/').some((part) => part === '' || part === '.' || part === '..')
    || /[\0\r\n\t]/.test(value)
  ) {
    fail(`${field} must be a safe POSIX relative path: ${value}`);
  }
}

function validateManifest(manifest) {
  if (manifest?.schema !== SCHEMA) fail(`schema must be ${SCHEMA}`);
  validateRelative(manifest.default_build_root, 'default_build_root');
  if (!Array.isArray(manifest.bundles) || manifest.bundles.length !== 2) {
    fail('bundles must contain exactly PRE and POST AAX');
  }

  const roles = new Set();
  for (const bundle of manifest.bundles) {
    if (!['PRE', 'POST'].includes(bundle.role)) fail(`unsupported role: ${bundle.role}`);
    if (roles.has(bundle.role)) fail(`duplicate role: ${bundle.role}`);
    roles.add(bundle.role);
    for (const field of ['source_relative', 'install_relative', 'archive_relative']) {
      validateRelative(bundle[field], `${bundle.role}.${field}`);
    }
    for (const field of ['executable_name', 'bundle_identifier']) {
      if (
        typeof bundle[field] !== 'string'
        || bundle[field].length === 0
        || /[\/\0\r\n\t]/.test(bundle[field])
      ) {
        fail(`${bundle.role}.${field} must be one safe value`);
      }
    }
    for (const field of ['source_relative', 'install_relative', 'archive_relative']) {
      if (path.posix.extname(bundle[field]) !== '.aaxplugin') {
        fail(`${bundle.role}.${field} must identify an .aaxplugin bundle`);
      }
    }
    if (path.posix.dirname(bundle.install_relative) !== INSTALL_PARENT) {
      fail(`${bundle.role}.install_relative is outside the Avid plug-in directory`);
    }
    if (
      path.posix.dirname(bundle.archive_relative) !== ARCHIVE_PARENT
      || path.posix.basename(bundle.archive_relative) !== path.posix.basename(bundle.install_relative)
    ) {
      fail(`${bundle.role}.archive_relative must preserve the installed name under AAX`);
    }
    if (
      path.posix.basename(bundle.source_relative, '.aaxplugin') !== bundle.executable_name
      || path.posix.basename(bundle.install_relative, '.aaxplugin') !== bundle.executable_name
    ) {
      fail(`${bundle.role} bundle names must match executable_name`);
    }
  }
}

export function loadMacAaxBundleManifest(options = {}) {
  const root = path.resolve(options.root || PROJECT_ROOT);
  const manifestPath = path.join(root, 'config/hypha_macos_aax_bundles.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  validateManifest(manifest);
  const buildRoot = path.resolve(root, options.buildRoot || manifest.default_build_root);
  return {
    schema: manifest.schema,
    defaultBuildRoot: buildRoot,
    bundles: manifest.bundles.map((bundle) => ({
      ...bundle,
      kind: 'aax',
      label: `${bundle.role} AAX`,
      sourcePath: path.join(buildRoot, bundle.source_relative),
      legacy_install_relative: [],
    })),
  };
}

function runCli(argv) {
  let buildRoot;
  for (let index = 0; index < argv.length; index += 1) {
    switch (argv[index]) {
      case '--build-root':
        buildRoot = argv[++index];
        if (!buildRoot) throw new Error('--build-root requires a value');
        break;
      case '--help':
      case '-h':
        console.log('Usage: node kirin_hypha_aax_bundles.mjs [--build-root DIR]');
        return;
      default:
        throw new Error(`unknown argument: ${argv[index]}`);
    }
  }
  const manifest = loadMacAaxBundleManifest({ buildRoot });
  for (const bundle of manifest.bundles) {
    process.stdout.write(`${bundle.sourcePath}\t${bundle.install_relative}\t${bundle.label}\n`);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    runCli(process.argv.slice(2));
  } catch (error) {
    console.error(`[kirin-hypha-aax-bundles] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
