#!/usr/bin/env node
// Compare retained unsigned products before and after a scheduling change, without rebuilding.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { buildTreeManifest } from './aax_submission_archive.mjs';
import { digest } from './hypha_release_contract.mjs';

export function verifyBuildBytes(serial, parallel) {
  if (fs.lstatSync(serial).isSymbolicLink() || fs.lstatSync(parallel).isSymbolicLink()
      || fs.realpathSync(serial) === fs.realpathSync(parallel)) throw new Error('Select two separate retained unsigned product trees');
  const before = buildTreeManifest(serial, 'payload'), after = buildTreeManifest(parallel, 'payload');
  if (digest(before) !== digest(after)) throw new Error('Unsigned product bytes, entry set, modes or symlink targets differ');
  return { schema: 'hypha-build-bytes-v1', passed: true, treeSha256: digest(before),
    files: before.entries.filter(entry => entry.type === 'file').length };
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    if (process.argv.length !== 4) throw new Error('Usage: node scripts/ls_release/verify_build_bytes.mjs SERIAL_PRODUCTS PARALLEL_PRODUCTS');
    console.log(JSON.stringify(verifyBuildBytes(process.argv[2], process.argv[3])));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
