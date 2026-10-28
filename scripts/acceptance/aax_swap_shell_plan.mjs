import crypto from 'node:crypto';
import { verifySnapshot, SUPPORTED_ATTRIBUTES } from './aax_swap_preflight.mjs';

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const order = (a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b));
export function validateTreeName(name) {
  if (typeof name !== 'string' || /[\0\r\n\t]/.test(name) || name.startsWith('/')
      || (name && name.split('/').some(part => !part || part === '.' || part === '..'))) throw new Error('unsupported tree path');
}
// Format shared with the fixed shell, including all names, modes, bytes and xattrs.
export function shellTreeHash(snapshot) {
  const attributes = new Map(snapshot.attributes.map(item => [item.path, item.values]));
  const lines = [];
  for (const entry of [...snapshot.entries].sort((a, b) => order(a.path, b.path))) {
    validateTreeName(entry.path);
    if (!Number.isInteger(entry.mode) || entry.mode < 0 || entry.mode > 0o777 || (entry.mode & 0o022)) throw new Error('unsafe tree mode');
    if (!['directory', 'file'].includes(entry.kind)) throw new Error('symlinks are unsupported');
    const rel = entry.path || '.';
    lines.push(`${entry.kind === 'directory' ? 'D' : 'F'}\t${entry.mode.toString(8)}\t${rel}\t${entry.sha256 || '-'}\n`);
    for (const attribute of [...(attributes.get(entry.path) || [])].sort((a, b) => order(a.name, b.name))) {
      if (!SUPPORTED_ATTRIBUTES.includes(attribute.name)) throw new Error('unsupported tree attribute');
      lines.push(`X\t${rel}\t${attribute.name}\t${hash(Buffer.from(attribute.base64, 'base64').toString('hex').toUpperCase())}\n`);
    }
  }
  return hash(lines.join(''));
}

export function shellPayload(request) {
  const candidates = ['PRE', 'POST'].map(role => request.candidates.find(item => item.role === role));
  const lines = ['HYPHA-AAX-SWAP-2', ...candidates.map(item => item.originalTreeSha256)];
  for (const candidate of candidates) {
    const snapshot = candidate.snapshot;
    verifySnapshot(snapshot, candidate.sha256);
    const treeHash = shellTreeHash(snapshot);
    if (treeHash !== candidate.treeSha256) throw new Error('candidate tree hash mismatch');
    lines.push(candidate.role, treeHash);
    const files = new Map(snapshot.files.map(item => [item.path, item.base64]));
    const attributes = new Map(snapshot.attributes.map(item => [item.path, item.values]));
    for (const entry of snapshot.entries) {
      validateTreeName(entry.path);
      lines.push(entry.kind === 'directory' ? 'D' : 'F', entry.path || '.', entry.mode.toString(8));
      if (entry.kind === 'file') lines.push(files.get(entry.path));
      for (const attribute of attributes.get(entry.path) || []) {
        const bytes = Buffer.from(attribute.base64, 'base64');
        if (bytes.length > 16384) throw new Error('attribute exceeds shell transport limit');
        lines.push('X', entry.path || '.', attribute.name, bytes.toString('hex').toUpperCase());
      }
    }
    lines.push('END');
  }
  return lines.join('\n') + '\n';
}
