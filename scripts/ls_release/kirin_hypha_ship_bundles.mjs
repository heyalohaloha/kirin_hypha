#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MODULE_PATH = fileURLToPath(import.meta.url);
const SCRIPT_DIR = path.dirname(MODULE_PATH);
export const PROJECT_ROOT = path.resolve(SCRIPT_DIR, '..', '..');
export const SHIP_BUNDLE_MANIFEST_PATH = path.join(
  PROJECT_ROOT,
  'config/hypha_macos_ship_bundles.json',
);

const SCHEMA = 'kirin-hypha-macos-ship-bundles-v1';
const INSTALL_PARENTS = {
  au: 'Library/Audio/Plug-Ins/Components',
  vst3: 'Library/Audio/Plug-Ins/VST3',
};
const ARCHIVE_PARENTS = {
  au: 'Audio Unit',
  vst3: 'VST3',
};
const EXTENSIONS = {
  au: '.component',
  vst3: '.vst3',
};

function fail(message) {
  throw new Error(`invalid macOS ship bundle manifest: ${message}`);
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
  if (!Array.isArray(manifest.bundles) || manifest.bundles.length !== 4) {
    fail('bundles must contain exactly PRE/POST × AU/VST3');
  }

  const identities = new Set();
  for (const bundle of manifest.bundles) {
    if (!['PRE', 'POST'].includes(bundle.role)) fail(`unsupported role: ${bundle.role}`);
    if (!['au', 'vst3'].includes(bundle.kind)) fail(`unsupported kind: ${bundle.kind}`);
    const identity = `${bundle.role}:${bundle.kind}`;
    if (identities.has(identity)) fail(`duplicate role/format: ${identity}`);
    identities.add(identity);

    for (const field of ['source_relative', 'install_relative', 'archive_relative']) {
      validateRelative(bundle[field], `${identity}.${field}`);
    }
    if (!Array.isArray(bundle.legacy_install_relative)) {
      fail(`${identity}.legacy_install_relative must be an array`);
    }
    for (const legacy of bundle.legacy_install_relative) {
      validateRelative(legacy, `${identity}.legacy_install_relative`);
    }
    if (
      typeof bundle.executable_name !== 'string'
      || bundle.executable_name.length === 0
      || /[\/\0\r\n\t]/.test(bundle.executable_name)
    ) {
      fail(`${identity}.executable_name must be one safe file name`);
    }
    if (typeof bundle.display_name !== 'string' || !bundle.display_name.startsWith(bundle.role)) {
      fail(`${identity}.display_name must be role-first`);
    }

    const extension = EXTENSIONS[bundle.kind];
    if (
      path.posix.extname(bundle.source_relative) !== extension
      || path.posix.extname(bundle.install_relative) !== extension
      || path.posix.extname(bundle.archive_relative) !== extension
    ) {
      fail(`${identity} bundle extensions do not match kind`);
    }
    if (
      path.posix.basename(bundle.source_relative, extension) !== bundle.executable_name
    ) {
      fail(`${identity} source bundle name must match executable_name`);
    }
    if (path.posix.dirname(bundle.install_relative) !== INSTALL_PARENTS[bundle.kind]) {
      fail(`${identity} install path is outside the approved plug-in directory`);
    }
    if (
      path.posix.dirname(bundle.archive_relative) !== ARCHIVE_PARENTS[bundle.kind]
      || path.posix.basename(bundle.archive_relative) !== path.posix.basename(bundle.install_relative)
    ) {
      fail(`${identity} archive path must preserve the installed file name`);
    }
    for (const legacy of bundle.legacy_install_relative) {
      if (
        path.posix.dirname(legacy) !== INSTALL_PARENTS[bundle.kind]
        || path.posix.extname(legacy) !== extension
        || legacy === bundle.install_relative
      ) {
        fail(`${identity} legacy path is outside the approved plug-in directory`);
      }
    }
    if (bundle.kind === 'au' && bundle.component_cid !== null) {
      fail(`${identity} AU must not declare a VST3 CID`);
    }
    if (
      bundle.kind === 'vst3'
      && (typeof bundle.component_cid !== 'string'
        || !/^[0-9A-Fa-f]{32}$/.test(bundle.component_cid))
    ) {
      fail(`${identity} VST3 CID must contain 32 hexadecimal characters`);
    }
  }
}

export function loadMacShipBundleManifest(options = {}) {
  const root = path.resolve(options.root || PROJECT_ROOT);
  const manifestPath = path.join(root, 'config/hypha_macos_ship_bundles.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  validateManifest(manifest);
  const buildRoot = path.resolve(root, options.buildRoot || manifest.default_build_root);
  return {
    schema: manifest.schema,
    defaultBuildRoot: buildRoot,
    bundles: manifest.bundles.map((bundle) => ({
      ...bundle,
      label: `${bundle.role} ${bundle.kind === 'au' ? 'AU' : 'VST3'}`,
      sourcePath: path.join(buildRoot, bundle.source_relative),
    })),
  };
}

function runCli(argv) {
  let buildRoot;
  let kind;
  for (let index = 0; index < argv.length; index += 1) {
    switch (argv[index]) {
      case '--build-root':
        buildRoot = argv[++index];
        if (!buildRoot) throw new Error('--build-root requires a value');
        break;
      case '--kind':
        kind = argv[++index];
        if (!['au', 'vst3'].includes(kind)) throw new Error('--kind requires au or vst3');
        break;
      case '--help':
      case '-h':
        console.log('Usage: node kirin_hypha_ship_bundles.mjs [--build-root DIR] [--kind au|vst3]');
        return;
      default:
        throw new Error(`unknown argument: ${argv[index]}`);
    }
  }

  const manifest = loadMacShipBundleManifest({ buildRoot });
  for (const bundle of manifest.bundles.filter((item) => !kind || item.kind === kind)) {
    process.stdout.write(
      `${bundle.sourcePath}\t${bundle.install_relative}\t${bundle.label}\n`,
    );
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    runCli(process.argv.slice(2));
  } catch (error) {
    console.error(`[kirin-hypha-ship-bundles] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
