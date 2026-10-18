// PKG payload directories created by the builder follow the caller's umask; an owner-only
// umask would install root-only folders. Set the container modes while staging, before signing.
import fs from 'node:fs';
import path from 'node:path';

export const CONTAINER_DIRECTORY_MODE = 0o755;

// Directories between the payload root and each staged leaf; the leaf itself (a signed bundle
// or the Legal tree, which stageLegalDelivery owns) is never changed here.
export function containerDirectories(payloadRoot, leaves) {
  const root = path.resolve(payloadRoot);
  const result = new Set([root]);
  for (const leaf of leaves) {
    const relative = path.relative(root, path.resolve(root, leaf));
    if (!relative || relative.startsWith('..') || path.isAbsolute(relative)) {
      throw new Error(`PKG payload leaf is outside the payload root: ${leaf}`);
    }
    let dir = root;
    for (const part of relative.split(path.sep).slice(0, -1)) result.add(dir = path.join(dir, part));
  }
  return [...result].sort();
}

export function normalizeContainerDirectories(payloadRoot, leaves) {
  for (const dir of containerDirectories(payloadRoot, leaves)) {
    if (fs.lstatSync(dir).isSymbolicLink()) throw new Error(`PKG payload directory is a symlink: ${dir}`);
    fs.chmodSync(dir, CONTAINER_DIRECTORY_MODE);
  }
  verifyContainerDirectories(payloadRoot, leaves);
}

export function verifyContainerDirectories(payloadRoot, leaves) {
  for (const dir of containerDirectories(payloadRoot, leaves)) {
    const stat = fs.lstatSync(dir);
    if (!stat.isDirectory() || (stat.mode & 0o777) !== CONTAINER_DIRECTORY_MODE) {
      throw new Error(`PKG payload directory mode ${(stat.mode & 0o777).toString(8)} is not 755: ${path.relative(payloadRoot, dir) || '.'}`);
    }
  }
}
