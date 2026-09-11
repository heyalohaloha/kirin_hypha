#!/usr/bin/env node
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  inspectWindowsAaxRecord,
  loadWindowsAaxBundleManifest,
  verifyWindowsAaxRecord,
} from './windows-aax-bundles.mjs';

const THIS_FILE = fileURLToPath(import.meta.url);
export const BUILD_MANIFEST_NAME = 'kirin-hypha-windows-aax-build.json';
export const SIGNED_MANIFEST_NAME = 'kirin-hypha-windows-aax-signed.json';
const BUILD_SCHEMA = 'kirin-hypha-windows-aax-build-v1';
const SIGNED_SCHEMA = 'kirin-hypha-windows-aax-signed-v1';

function sha256File(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function readJson(filePath, label) {
  if (!fs.statSync(filePath, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`${label} is missing: ${filePath}`);
  }
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (error) {
    throw new Error(`${label} is invalid JSON: ${error.message}`);
  }
}

function validateSource(source) {
  if (!/^[0-9a-f]{40}$/.test(source?.commit || '')) {
    throw new Error('Windows AAX source commit must be a full lowercase Git commit');
  }
  if (!/^B-\d+$/.test(source?.b_number || '')) {
    throw new Error('Windows AAX source must have a B number');
  }
  if (!['clean source', 'modified source'].includes(source?.state)) {
    throw new Error('Windows AAX source state is invalid');
  }
}

function inspectRecords(artifactRoot) {
  return loadWindowsAaxBundleManifest({ artifactRoot }).bundles.map((record) => {
    const inspected = inspectWindowsAaxRecord(record);
    return {
      role: inspected.role,
      bundle: inspected.name,
      binary_relative: inspected.binaryRelative,
      size_bytes: fs.statSync(inspected.binary).size,
      sha256: inspected.binarySha256,
    };
  });
}

function cmakeCacheValue(cache, name) {
  const match = cache.match(new RegExp(`^${name}:[^=]+=(.*)$`, 'm'));
  if (!match) throw new Error(`Windows AAX CMake cache is missing ${name}`);
  return match[1].trim();
}

function inspectBuildConfiguration(artifactRoot) {
  const cachePath = path.join(artifactRoot, 'CMakeCache.txt');
  if (!fs.statSync(cachePath, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`Windows AAX CMake cache is missing: ${cachePath}`);
  }
  const cache = fs.readFileSync(cachePath, 'utf8');
  const font = cmakeCacheValue(cache, 'KIRIN_HYPHA_KIMERA_FONT_FILE');
  const licenseConfirmed = cmakeCacheValue(
    cache,
    'KIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED',
  ) === 'ON';
  const requireKimera = cmakeCacheValue(cache, 'KIRIN_HYPHA_REQUIRE_KIMERA_FONT') === 'ON';
  const nativeOnly = ['PRE', 'POST'].every((role) => {
    const project = path.join(artifactRoot, `KirinHypha${role}_AAX.vcxproj`);
    return fs.statSync(project, { throwIfNoEntry: false })?.isFile()
      && fs.readFileSync(project, 'utf8').includes('JucePlugin_AAXDisableAudioSuite=1');
  });
  return {
    kimera_embedded: font.length > 0 && licenseConfirmed && requireKimera,
    native_only: nativeOnly,
    audio_suite_enabled: !nativeOnly,
  };
}

function validateProduct(product, version) {
  if (product?.name !== 'Kirin Hypha'
      || product?.version !== version
      || product?.platform !== 'windows-x64'
      || product?.format !== 'AAX') {
    throw new Error(`Windows AAX product identity does not match ${version}`);
  }
}

function validateRelease(release, requireReleaseReady) {
  if (release?.native_only !== true || release?.audio_suite_enabled !== false) {
    throw new Error('Windows AAX must expose the Native-only surface');
  }
  if (typeof release?.kimera_embedded !== 'boolean') {
    throw new Error('Windows AAX Kimera state is missing');
  }
  if (requireReleaseReady && release.kimera_embedded !== true) {
    throw new Error('Windows AAX release signing requires the licensed Kimera App font');
  }
}

function validateBundles(expected, actual) {
  if (!Array.isArray(expected) || expected.length !== 2) {
    throw new Error('Windows AAX provenance must contain exactly PRE and POST');
  }
  for (const role of ['PRE', 'POST']) {
    const recorded = expected.find((item) => item?.role === role);
    const inspected = actual.find((item) => item.role === role);
    if (!recorded || !inspected
        || recorded.bundle !== inspected.bundle
        || recorded.binary_relative !== inspected.binary_relative
        || recorded.size_bytes !== inspected.size_bytes
        || recorded.sha256 !== inspected.sha256) {
      throw new Error(`${role} Windows AAX no longer matches its provenance manifest`);
    }
  }
}

export function writeWindowsAaxBuildProvenance({
  artifactRoot,
  version,
  source,
}) {
  validateSource(source);
  const resolvedRoot = path.resolve(artifactRoot);
  const manifest = {
    schema: BUILD_SCHEMA,
    generated_at: new Date().toISOString(),
    source,
    product: {
      name: 'Kirin Hypha',
      version,
      platform: 'windows-x64',
      format: 'AAX',
    },
    release: inspectBuildConfiguration(resolvedRoot),
    bundles: inspectRecords(resolvedRoot),
  };
  const manifestPath = path.join(resolvedRoot, BUILD_MANIFEST_NAME);
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  return { manifest, manifestPath };
}

export function loadWindowsAaxBuildProvenance({
  artifactRoot,
  version,
  requireReleaseReady = false,
}) {
  const resolvedRoot = path.resolve(artifactRoot);
  const manifestPath = path.join(resolvedRoot, BUILD_MANIFEST_NAME);
  const manifest = readJson(manifestPath, 'Windows AAX build provenance');
  if (manifest.schema !== BUILD_SCHEMA) throw new Error(`unsupported Windows AAX build provenance: ${manifest.schema}`);
  validateSource(manifest.source);
  validateProduct(manifest.product, version);
  validateRelease(manifest.release, requireReleaseReady);
  if (requireReleaseReady && manifest.source.state !== 'clean source') {
    throw new Error('Windows AAX release signing requires clean source');
  }
  validateBundles(manifest.bundles, inspectRecords(resolvedRoot));
  return { manifest, manifestPath, manifestSha256: sha256File(manifestPath) };
}

export function writeWindowsAaxSignedProvenance({
  sourceArtifactRoot,
  signedArtifactRoot,
  version,
  wraptool,
}) {
  const build = loadWindowsAaxBuildProvenance({
    artifactRoot: sourceArtifactRoot,
    version,
    requireReleaseReady: true,
  });
  const signedRoot = path.resolve(signedArtifactRoot);
  const signedRecords = loadWindowsAaxBundleManifest({ artifactRoot: signedRoot }).bundles.map((record) => {
    const verified = verifyWindowsAaxRecord(record, version, { wraptool });
    return {
      role: verified.role,
      bundle: verified.name,
      binary_relative: verified.binaryRelative,
      size_bytes: fs.statSync(verified.binary).size,
      sha256: verified.binarySha256,
      pace_verified: true,
      authenticode_verified: true,
    };
  });
  const manifest = {
    schema: SIGNED_SCHEMA,
    generated_at: new Date().toISOString(),
    source: build.manifest.source,
    product: build.manifest.product,
    release: build.manifest.release,
    unsigned_build: {
      manifest_sha256: build.manifestSha256,
      bundles: build.manifest.bundles,
    },
    bundles: signedRecords,
    signing: {
      operation: 'combined PACE and Authenticode',
      pace_verified: true,
      authenticode_verified: true,
    },
  };
  const manifestPath = path.join(signedRoot, SIGNED_MANIFEST_NAME);
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  return { manifest, manifestPath, manifestSha256: sha256File(manifestPath) };
}

export function loadWindowsAaxSignedProvenance({ artifactRoot, version }) {
  const resolvedRoot = path.resolve(artifactRoot);
  const manifestPath = path.join(resolvedRoot, SIGNED_MANIFEST_NAME);
  const manifest = readJson(manifestPath, 'signed Windows AAX provenance');
  if (manifest.schema !== SIGNED_SCHEMA) throw new Error(`unsupported signed Windows AAX provenance: ${manifest.schema}`);
  validateSource(manifest.source);
  validateProduct(manifest.product, version);
  validateRelease(manifest.release, true);
  if (manifest.source.state !== 'clean source') throw new Error('signed Windows AAX provenance is not clean-source');
  if (manifest.signing?.pace_verified !== true || manifest.signing?.authenticode_verified !== true) {
    throw new Error('signed Windows AAX provenance does not record both signature systems');
  }
  if (!/^[0-9a-f]{64}$/.test(manifest.unsigned_build?.manifest_sha256 || '')
      || !Array.isArray(manifest.unsigned_build?.bundles)
      || manifest.unsigned_build.bundles.length !== 2) {
    throw new Error('signed Windows AAX provenance has no valid unsigned-build receipt');
  }
  for (const role of ['PRE', 'POST']) {
    const unsignedRecord = manifest.unsigned_build.bundles.find((item) => item?.role === role);
    const signedRecord = manifest.bundles?.find((item) => item?.role === role);
    if (!/^[0-9a-f]{64}$/.test(unsignedRecord?.sha256 || '')
        || signedRecord?.pace_verified !== true
        || signedRecord?.authenticode_verified !== true) {
      throw new Error(`${role} Windows AAX signed provenance is incomplete`);
    }
  }
  validateBundles(manifest.bundles, inspectRecords(resolvedRoot));
  return { manifest, manifestPath, manifestSha256: sha256File(manifestPath) };
}

function valueAfter(argv, name) {
  const index = argv.indexOf(name);
  if (index < 0 || !argv[index + 1]) throw new Error(`${name} is required`);
  return argv[index + 1];
}

function main(argv) {
  const mode = argv[0];
  const artifactRoot = valueAfter(argv, '--artifact-dir');
  const version = valueAfter(argv, '--version');
  if (mode === 'write-build') {
    const result = writeWindowsAaxBuildProvenance({
      artifactRoot,
      version,
      source: {
        commit: valueAfter(argv, '--source-commit'),
        b_number: valueAfter(argv, '--b-number'),
        state: valueAfter(argv, '--source-state'),
      },
    });
    console.log(`[windows-aax-provenance] wrote ${result.manifestPath}`);
  } else if (mode === 'verify-build') {
    loadWindowsAaxBuildProvenance({
      artifactRoot,
      version,
      requireReleaseReady: argv.includes('--require-release-ready'),
    });
    console.log('[windows-aax-provenance] build provenance passed');
  } else if (mode === 'write-signed') {
    const result = writeWindowsAaxSignedProvenance({
      sourceArtifactRoot: valueAfter(argv, '--source-artifact-dir'),
      signedArtifactRoot: artifactRoot,
      version,
      wraptool: process.env.KIRIN_AAX_WRAPTOOL,
    });
    console.log(`[windows-aax-provenance] wrote ${result.manifestPath}`);
  } else if (mode === 'verify-signed') {
    loadWindowsAaxSignedProvenance({ artifactRoot, version });
    console.log('[windows-aax-provenance] signed provenance passed');
  } else {
    throw new Error('mode must be write-build, verify-build, write-signed, or verify-signed');
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`[windows-aax-provenance] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
