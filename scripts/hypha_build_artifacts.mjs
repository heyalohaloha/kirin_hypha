import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export function expectedArtifacts(platform, buildDir) {
  const formats = platform === 'macos' ? ['AAX', 'AU', 'VST3'] : ['AAX', 'VST3'];
  const extensions = { AAX: 'aaxplugin', AU: 'component', VST3: 'vst3' };
  return ['PRE', 'POST'].flatMap((role) => formats.map((format) => {
    const name = `Kirin Hypha ${role}`;
    const bundle = path.join(buildDir, `KirinHypha${role}_artefacts`, 'Release', format,
      `${name}.${extensions[format]}`);
    const executable = platform === 'macos'
      ? path.join(bundle, 'Contents', 'MacOS', name)
      : path.join(bundle, 'Contents', format === 'AAX' ? 'x64' : 'x86_64-win',
        `${name}.${extensions[format]}`);
    return { role, format, bundle, executable };
  }));
}

export function inspectPe(bytes) {
  if (bytes.length < 64 || bytes.toString('ascii', 0, 2) !== 'MZ') {
    throw new Error('Executable is not a PE binary');
  }
  const offset = bytes.readUInt32LE(60);
  if (offset < 64 || offset + 176 > bytes.length
      || bytes.toString('ascii', offset, offset + 4) !== 'PE\0\0') {
    throw new Error('Invalid or truncated PE header');
  }
  const magic = bytes.readUInt16LE(offset + 24);
  if (magic !== 0x20b) throw new Error('Expected a 64-bit PE executable');
  return {
    machine: bytes.readUInt16LE(offset + 4),
    certificateBytes: bytes.readUInt32LE(offset + 24 + 112 + 4 * 8 + 4),
  };
}

export function rejectSignedArtifacts(platform, buildDir, run) {
  for (const receipt of ['kirin-hypha-macos-aax-notarization.json',
    'kirin-hypha-windows-aax-signed.json']) {
    if (fs.existsSync(path.join(buildDir, receipt))) {
      throw new Error('Refusing to overwrite release output; select a new --build-id');
    }
  }
  for (const artifact of expectedArtifacts(platform, buildDir)) {
    if (!fs.existsSync(artifact.executable)) continue;
    if (platform === 'windows') {
      if (inspectPe(fs.readFileSync(artifact.executable)).certificateBytes > 0) {
        throw new Error('Refusing to overwrite signed Windows output; use a new --build-id');
      }
    } else if (fs.existsSync(path.join(artifact.bundle, 'Contents', '_CodeSignature'))) {
      const signature = run('codesign', ['-d', '-v', artifact.bundle], { capture: true,
        allowFailure: true, includeStderr: true });
      // JUCE's local ad-hoc signature is safe to rebuild; Developer ID output is not.
      if (!signature.includes('Signature=adhoc')) {
        throw new Error('Refusing to overwrite signed Mac output; use a new --build-id');
      }
    }
  }
}

export function verifyArtifacts({ platform, buildDir, version }, run) {
  return expectedArtifacts(platform, buildDir).map((artifact) => {
    const stat = fs.statSync(artifact.executable, { throwIfNoEntry: false });
    if (!stat?.isFile() || stat.size === 0) {
      throw new Error(`Missing or empty ${artifact.role} ${artifact.format} executable`);
    }
    let architectures;
    if (platform === 'macos') {
      architectures = run('lipo', ['-archs', artifact.executable], { capture: true })
        .trim().split(/\s+/).sort();
      if (architectures.join(' ') !== 'arm64 x86_64') {
        throw new Error(`${artifact.role} ${artifact.format} is not Universal`);
      }
      const plist = path.join(artifact.bundle, 'Contents', 'Info.plist');
      const value = (key) => run('/usr/libexec/PlistBuddy',
        ['-c', `Print :${key}`, plist], { capture: true }).trim();
      if (value('CFBundleShortVersionString') !== version
          || value('CFBundleExecutable') !== path.basename(artifact.executable)) {
        throw new Error(`${artifact.role} ${artifact.format} version/executable mismatch`);
      }
      if (artifact.format === 'AAX' && (value('KirinHyphaAudioSuiteEnabled') !== 'false'
          || value('KirinHyphaAaxBuildMode') !== 'diagnostic')) {
        throw new Error(`${artifact.role} AAX must remain diagnostic and Native-only`);
      }
    } else {
      if (inspectPe(fs.readFileSync(artifact.executable)).machine !== 0x8664) {
        throw new Error(`${artifact.role} ${artifact.format} is not Windows x64`);
      }
      const escaped = artifact.executable.replaceAll("'", "''");
      const command = `$v=(Get-Item -LiteralPath '${escaped}').VersionInfo; `
        + '[pscustomobject]@{File=$v.FileVersion;Product=$v.ProductVersion}|ConvertTo-Json -Compress';
      const versions = JSON.parse(run('powershell.exe', ['-NoProfile', '-EncodedCommand',
        Buffer.from(command, 'utf16le').toString('base64')], { capture: true }));
      if (versions.File !== version || versions.Product !== version) {
        throw new Error(`${artifact.role} ${artifact.format} Windows version mismatch`);
      }
      architectures = ['x64'];
    }
    return { ...artifact, architectures, bytes: stat.size,
      sha256: crypto.createHash('sha256').update(fs.readFileSync(artifact.executable)).digest('hex') };
  });
}
