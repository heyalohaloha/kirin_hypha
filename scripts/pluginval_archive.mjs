#!/usr/bin/env node
// No environment variable can replace the reviewed archive pin.
import { readFileSync, statSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const pins = JSON.parse(readFileSync(new URL('./pluginval_archives.json', import.meta.url), 'utf8'));
export function resolvePin(version, platform) {
  const pin = pins.versions?.[version]?.[platform];
  if (!pin) throw new Error(`Unregistered pluginval archive: ${version}/${platform}`);
  if (!/^[a-f0-9]{64}$/.test(pin.sha256) || !Number.isSafeInteger(pin.bytes) || pin.bytes < 1)
    throw new Error('Invalid reviewed pluginval archive pin');
  return { version, platform, ...pin };
}
export function verifyArchive(pin, archive) {
  if (!/^[a-f0-9]{64}$/.test(pin.sha256 ?? '') || !Number.isSafeInteger(pin.bytes) || pin.bytes < 1)
    throw new Error('Missing or malformed pluginval archive pin');
  if (!statSync(archive).isFile()) throw new Error('Archive is not a regular file');
  const bytes = readFileSync(archive);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  if (bytes.length !== pin.bytes || sha256 !== pin.sha256)
    throw new Error('pluginval archive integrity mismatch; do not extract, retry, or execute');
  return { ...pin, verifiedBytes: bytes.length, verifiedSha256: sha256 };
}
if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  try {
    const [command, version, platform, archive] = process.argv.slice(2);
    if (!['describe', 'url', 'verify'].includes(command)) throw new Error('Use describe|url|verify VERSION PLATFORM [ARCHIVE]');
    const pin = resolvePin(version, platform);
    if (command === 'url') console.log(pin.url);
    else if (command === 'describe') console.log(JSON.stringify(pin));
    else {
      if (!archive) throw new Error('Archive path is required');
      console.log(JSON.stringify(verifyArchive(pin, fileURLToPath(pathToFileURL(archive)))));
    }
  } catch (error) { console.error(`ERROR: ${error.message}`); process.exitCode = 1; }
}
