#!/usr/bin/env node
// Copy a closed, explicitly selected record directory. Never discover a live Library.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

function fileHash(file) {
  const hash = crypto.createHash('sha256'), fd = fs.openSync(file, 'r');
  try {
    const buffer = Buffer.alloc(1024 * 1024);
    let bytes;
    while ((bytes = fs.readSync(fd, buffer)) > 0) hash.update(buffer.subarray(0, bytes));
    return hash.digest('hex');
  } finally { fs.closeSync(fd); }
}
function inventory(root) {
  const rows = [];
  function visit(relative) {
    const file = path.join(root, relative), stat = fs.lstatSync(file);
    if (stat.isSymbolicLink()) throw new Error('Record sources must not contain symlinks');
    if (stat.isDirectory()) {
      for (const entry of fs.readdirSync(file).sort()) visit(path.join(relative, entry));
    } else if (stat.isFile()) rows.push({ path: relative, bytes: stat.size, sha256: fileHash(file) });
    else throw new Error('Record sources must contain only regular files and directories');
  }
  visit(''); return rows;
}
const inside = (parent, child) => child === parent || child.startsWith(`${parent}${path.sep}`);
export function collectRecords(source, destination) {
  source = path.resolve(source); destination = path.resolve(destination);
  if (fs.realpathSync(source) !== source) throw new Error('Record source path must not traverse symlinks');
  const parent = fs.realpathSync(path.dirname(destination));
  if (parent !== path.dirname(destination) || inside(source, destination) || inside(destination, source)) {
    throw new Error('Evidence must be separate from the source and must not traverse symlinks');
  }
  if (!fs.statSync(source).isDirectory()) throw new Error('Select a closed record directory');
  const rows = inventory(source);
  if (!rows.length) throw new Error('No record files selected');
  fs.mkdirSync(destination, { mode: 0o700 }); // Exclusive; never merge with another run.
  try {
    const files = path.join(destination, 'files'); fs.mkdirSync(files);
    for (const row of rows) {
      const output = path.join(files, row.path);
      fs.mkdirSync(path.dirname(output), { recursive: true });
      fs.copyFileSync(path.join(source, row.path), output, fs.constants.COPYFILE_EXCL);
      if (fileHash(output) !== row.sha256) throw new Error('Record changed while copying');
    }
    if (JSON.stringify(inventory(source)) !== JSON.stringify(rows)) throw new Error('Record source changed while collecting');
    const manifest = { schema: 'hypha-record-collection-v1', files: rows };
    fs.writeFileSync(path.join(destination, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
    return manifest;
  } catch (error) { fs.rmSync(destination, { recursive: true, force: true }); throw error; }
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    if (process.argv.length !== 4) throw new Error('Usage: node scripts/acceptance/collect_records.mjs CLOSED_RECORD_DIR NEW_EVIDENCE_DIR');
    console.log(JSON.stringify(collectRecords(process.argv[2], process.argv[3])));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
