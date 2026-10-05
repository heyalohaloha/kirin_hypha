import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { parseArgs as parseReleaseSetArgs, requireWindowsInstaller } from './build_kirin_hypha_release_set.mjs';
const sha256File = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const binding = rows => ({ protocol: 1, publicKeySha256: '', binaries: rows.map(([format, role, binarySha256]) => ({ format, role, binarySha256, publicKeySha256: '' })) });

test('full release set accepts only a signed, verified, externally validated Windows installer', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-windows-primary-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const installer = path.join(root, 'Kirin-Hypha-1.2.3-Windows-x64-Setup.exe');
  fs.writeFileSync(installer, 'signed installer fixture');
  const digest = sha256File(installer);
  const preDigest = '1'.repeat(64);
  const postDigest = '2'.repeat(64);
  const uninstallerDigest = '3'.repeat(64);
  const identity = { version: '1.2.3', commit: '0'.repeat(40), bNumber: 'B-123' };
  fs.writeFileSync(`${installer}.sha256`, `${digest}  ${path.basename(installer)}\n`);
  const signatureTarget = (role, targetDigest) => ({
    role,
    file_name: `${role}.exe`,
    status: 'Valid',
    sha256: targetDigest,
    signer_subject: 'CN=Kirin fixture',
    signer_thumbprint: 'A'.repeat(40),
    timestamp_subject: 'CN=Timestamp fixture',
    timestamp_thumbprint: 'B'.repeat(40),
  });
  const manifest = {
    updateCheck: binding([['VST3', 'PRE', preDigest], ['VST3', 'POST', postDigest]]),
    schema: 'kirin-hypha-windows-installer-v1',
    product: {
      name: 'Kirin Hypha',
      version: identity.version,
      platform: 'windows-x64',
      format: 'VST3',
      formats: ['VST3'],
    },
    source: {
      commit: identity.commit,
      b_number: identity.bNumber,
      github_actions_run: 'https://github.com/heyalohaloha/kirin_hypha/actions/runs/123',
    },
    installer: {
      sha256: digest,
      payload: [
        { role: 'PRE', binary_sha256: preDigest },
        { role: 'POST', binary_sha256: postDigest },
      ],
    },
    signing: {
      status: 'valid',
      workflow_run: 'https://github.com/heyalohaloha/kirin_sense_lens/actions/runs/456',
      verification: {
        targets: [
          signatureTarget('installer', digest),
          signatureTarget('installed PRE VST3 binary', preDigest),
          signatureTarget('installed POST VST3 binary', postDigest),
          signatureTarget('installed uninstaller', uninstallerDigest),
        ],
      },
    },
    ci_validation: { status: 'passed' },
    external_validation: {
      status: 'complete',
      note: 'The exact signed installer passed the retained dedicated Windows DAW validation.',
      report_sha256: 'c'.repeat(64),
      installer_sha256: digest,
      candidate_workflow_run: 'https://github.com/heyalohaloha/kirin_sense_lens/actions/runs/456',
      completed_at: '2026-09-20T03:00:00.000Z',
    },
    distribution: { primary: true, public_ready: true, aax_included: false },
  };
  fs.writeFileSync(`${installer}.json`, JSON.stringify(manifest));

  assert.equal(requireWindowsInstaller(root, identity), installer);
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    external_validation: { ...manifest.external_validation, installer_sha256: 'd'.repeat(64) },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity),
    /exact signed-candidate validation provenance is incomplete/,
  );
  fs.writeFileSync(`${installer}.json`, JSON.stringify(manifest));
  assert.equal(
    parseReleaseSetArgs(['--windows-artifact-dir', root]).windowsInstallerDir,
    root,
  );
  assert.equal(parseReleaseSetArgs(['--with-aax']).withAax, true);
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /format VST3 does not match VST3\+AAX/,
  );

  const preAaxDigest = '4'.repeat(64);
  const postAaxDigest = '5'.repeat(64);
  const aaxProofName = `Kirin-Hypha-${identity.version}-Windows-x64-AAX.json`;
  const signedAaxProof = {
    updateCheck: binding([['AAX', 'PRE', preAaxDigest], ['AAX', 'POST', postAaxDigest]]),
    schema: 'kirin-hypha-windows-aax-signed-v1',
    source: { commit: identity.commit, b_number: identity.bNumber, state: 'clean source' },
    product: { name: 'Kirin Hypha', version: identity.version, platform: 'windows-x64', format: 'AAX' },
    release: {
      mode: 'release', kimera_embedded: false, native_only: true, audio_suite_enabled: false,
    },
    bundles: [
      { role: 'PRE', sha256: preAaxDigest, pace_verified: true, authenticode_verified: true },
      { role: 'POST', sha256: postAaxDigest, pace_verified: true, authenticode_verified: true },
    ],
    signing: { pace_verified: true, authenticode_verified: true },
  };
  const aaxProofPath = path.join(root, aaxProofName);
  fs.writeFileSync(aaxProofPath, JSON.stringify(signedAaxProof));
  const aaxProofDigest = sha256File(aaxProofPath);
  const aaxManifest = {
    ...manifest,
    updateCheck: binding([['VST3', 'PRE', preDigest], ['VST3', 'POST', postDigest], ['AAX', 'PRE', preAaxDigest], ['AAX', 'POST', postAaxDigest]]),
    product: { ...manifest.product, format: 'VST3+AAX', formats: ['VST3', 'AAX'] },
    installer: {
      ...manifest.installer,
      aax_payload: [
        {
          role: 'PRE', format: 'AAX', binary_sha256: preAaxDigest,
          pace_verified: true, authenticode_verified: true,
        },
        {
          role: 'POST', format: 'AAX', binary_sha256: postAaxDigest,
          pace_verified: true, authenticode_verified: true,
        },
      ],
    },
    signing: {
      ...manifest.signing,
      verification: {
        targets: [
          ...manifest.signing.verification.targets.slice(0, 3),
          signatureTarget('installed PRE AAX binary', preAaxDigest),
          signatureTarget('installed POST AAX binary', postAaxDigest),
          manifest.signing.verification.targets[3],
        ],
      },
    },
    distribution: {
      ...manifest.distribution,
      aax_included: true,
      aax_identity: {
        source_commit: identity.commit,
        b_number: identity.bNumber,
        source_state: 'clean source',
        build_mode: 'release',
        kimera_embedded: false,
        native_only: true,
        audio_suite_enabled: false,
        signed_manifest: aaxProofName,
        signed_manifest_sha256: aaxProofDigest,
      },
    },
  };
  fs.writeFileSync(`${installer}.json`, JSON.stringify(aaxManifest));
  assert.equal(requireWindowsInstaller(root, identity, { requireAax: true }), installer);
  assert.throws(
    () => requireWindowsInstaller(root, identity),
    /contains AAX.*did not select --with-aax/,
  );
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...aaxManifest,
    installer: {
      ...aaxManifest.installer,
      aax_payload: aaxManifest.installer.aax_payload.map((payload) => (
        payload.role === 'POST' ? { ...payload, pace_verified: false } : payload
      )),
    },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /POST AAX payload is not fully verified/,
  );
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...aaxManifest,
    signing: {
      ...aaxManifest.signing,
      verification: {
        targets: aaxManifest.signing.verification.targets.map((target) => (
          target.role === 'installed PRE AAX binary'
            ? { ...target, sha256: '6'.repeat(64) }
            : target
        )),
      },
    },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /PRE AAX payload is not fully verified/,
  );

  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...aaxManifest,
    distribution: {
      ...aaxManifest.distribution,
      aax_identity: { ...aaxManifest.distribution.aax_identity, build_mode: 'diagnostic' },
    },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /AAX release provenance is incomplete/,
  );

  const diagnosticAaxProof = {
    ...signedAaxProof,
    release: { ...signedAaxProof.release, mode: 'diagnostic' },
  };
  fs.writeFileSync(aaxProofPath, JSON.stringify(diagnosticAaxProof));
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...aaxManifest,
    distribution: {
      ...aaxManifest.distribution,
      aax_identity: {
        ...aaxManifest.distribution.aax_identity,
        signed_manifest_sha256: sha256File(aaxProofPath),
      },
    },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /signed provenance content is invalid/,
  );
  fs.writeFileSync(aaxProofPath, JSON.stringify(signedAaxProof));

  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...aaxManifest,
    distribution: {
      ...aaxManifest.distribution,
      aax_identity: { ...aaxManifest.distribution.aax_identity, kimera_embedded: true },
    },
  }));
  assert.throws(
    () => requireWindowsInstaller(root, identity, { requireAax: true }),
    /signed provenance content is invalid/,
  );

  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    signing: { status: 'verified_unsigned_ci_candidate' },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /not Authenticode-ready/);
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    source: { ...manifest.source, commit: 'f'.repeat(40) },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /source commit or B number/);
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    signing: {
      ...manifest.signing,
      verification: {
        targets: manifest.signing.verification.targets.map((target) => (
          target.role === 'installed uninstaller' ? { ...target, status: 'NotSigned' } : target
        )),
      },
    },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /installed uninstaller/);
});
