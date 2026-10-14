#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createSigningAttempt, finishSigningAttempt } from './signing_recovery.mjs';
import { batchSign } from './sign-codesigntool.mjs';

const THIS_FILE = fileURLToPath(import.meta.url);

export function parseInnoArgs(argv) {
  if (argv.length !== 2 || argv[0] !== '--input-file' || !argv[1]
      || argv[1].startsWith('--')) {
    throw new Error('Inno signer requires exactly --input-file <path>');
  }
  return argv[1];
}

function readPe(file, label) {
  if (!fs.lstatSync(file, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`${label} is missing or not a regular file`);
  }
  const bytes = fs.readFileSync(file);
  const offset = bytes.length >= 64 ? bytes.readUInt32LE(0x3c) : 0;
  if (bytes.length < 64 || bytes.toString('ascii', 0, 2) !== 'MZ'
      || offset < 64 || offset > bytes.length - 6
      || bytes.toString('ascii', offset, offset + 4) !== 'PE\0\0') {
    throw new Error(`${label} is not a PE image`);
  }
  return bytes;
}

// Inno's generated uninstaller has a .tmp extension; CodeSignTool's batch
// selection needs an executable name. Keep the input intact until output is
// validated. The installer lifecycle gate separately verifies Authenticode.
export async function signInnoFile(inputFile, options = {}) {
  const original = path.resolve(inputFile);
  const unsigned = readPe(original, 'Inno signing input');
  const mode = fs.statSync(original).mode;
  const recoveryRoot = options.recoveryRoot || process.env.KIRIN_ESIGNER_RECOVERY_DIR || path.join(path.dirname(original), 'signing-recovery');
  const attempt = createSigningAttempt(recoveryRoot, path.extname(original).toLowerCase() === '.tmp' ? 'inno-uninstaller' : 'inno-installer');
  let completed = false;
  const staging = fs.mkdtempSync(path.join(path.dirname(original), 'kirin-inno-sign-'));
  try {
    const inputDir = path.join(staging, 'input');
    const outputDir = path.join(staging, 'output');
    fs.mkdirSync(inputDir);
    fs.mkdirSync(outputDir);
    const name = path.extname(original).toLowerCase() === '.tmp'
      ? 'uninstaller.exe' : path.basename(original);
    fs.writeFileSync(path.join(inputDir, name), unsigned);
    const { signer = batchSign, recoveryRoot: _recoveryRoot, ...signingOptions } = options;
    await signer(inputDir, outputDir, signingOptions);
    const output = path.join(outputDir, name);
    const signed = readPe(output, 'Signed Inno output');
    if (signed.equals(unsigned)) {
      throw new Error('CodeSignTool did not modify the staged Inno signing input');
    }
    if (!readPe(original, 'Inno signing input').equals(unsigned)) {
      throw new Error('Inno signing input changed while signing');
    }
    fs.chmodSync(output, mode);
    // Same-filesystem rename avoids truncating the original on a failed copy.
    fs.copyFileSync(output, path.join(attempt.directory, name));
    fs.renameSync(output, original);
    completed = true;
    return original;
  } finally {
    let preserved = false;
    try {
      if (!completed) fs.cpSync(staging, path.join(attempt.directory, 'staging'), { recursive: true, errorOnExist: true, force: false });
      finishSigningAttempt(attempt, completed ? 'completed' : 'failed-or-uncertain');
      preserved = true;
    } finally {
      if (preserved) fs.rmSync(staging, { recursive: true, force: true });
    }
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  Promise.resolve().then(() => signInnoFile(parseInnoArgs(process.argv.slice(2))))
    .catch((error) => {
      console.error(`[hypha-inno-sign] ERROR: ${error.message}`);
      process.exitCode = 1;
    });
}
