// Derive the private PKG receipt from frozen inputs; never include it in distribution facts.
import path from 'node:path';
import fs from 'node:fs';
import { fileFact, safeStatePath, resolveInput, digest } from './hypha_release_contract.mjs';
import { verifyPkgResume } from './pkg_staple.mjs';

function canonicalPath(file) {
  let ancestor = path.resolve(file), tail = [];
  while (!fs.existsSync(ancestor)) {
    const parent = path.dirname(ancestor);
    if (parent === ancestor) throw new Error('PKG path has no existing ancestor');
    tail.unshift(path.basename(ancestor)); ancestor = parent;
  }
  return path.join(fs.realpathSync(ancestor), ...tail);
}
export function createPkgStapleContext({ root, packagePath, statePath, source, version, aaxIncluded, updatePublicKey = '', binding, keychainProfile }) {
  if (!/^\d+\.\d+\.\d+$/.test(version) || !/^[a-f0-9]{40}$/.test(source?.commit)
      || typeof source.bNumber !== 'string' || typeof aaxIncluded !== 'boolean' || typeof updatePublicKey !== 'string') {
    throw new Error('Invalid PKG source/product context');
  }
  const identity = { source: { commit: source.commit, bNumber: source.bNumber }, version, aaxIncluded, updatePublicKey };
  return { packagePath: canonicalPath(packagePath), statePath: safeStatePath(root, statePath),
    binding: binding || digest(identity), identity, ...(keychainProfile ? { keychainProfile } : {}) };
}
export function requireFreshPackage(context, { unsignedSmoke = false } = {}) {
  if (unsignedSmoke) {
    if (!path.basename(context.packagePath).endsWith('-UNSIGNED-DO-NOT-UPLOAD.pkg')) throw new Error('Smoke output requires the unsigned package name');
    return;
  }
  if (fs.existsSync(context.packagePath) || fs.existsSync(context.statePath)) {
    throw new Error('Existing PKG/state must be reconciled or resumed; never rebuild or resubmit');
  }
}
export function pkgRecoveryContext(state) {
  if (!state.freeze?.sha256) throw new Error('PKG resume requires the frozen candidate');
  return createPkgStapleContext({ root: state.root,
    packagePath: path.join(state.root, 'dist/LS_UPLOAD', `Kirin-Hypha-${state.candidate.version}-macOS-Universal.pkg`),
    statePath: path.join(path.dirname(resolveInput(state, state.inputs.hostsReport)), 'pkg-staple/receipt.json'),
    binding: state.freeze.sha256, source: state.candidate, version: state.candidate.version,
    aaxIncluded: true, updatePublicKey: state.inputs.updatePublicKey || '' });
}
export function pkgRecoveryEvidence(state) {
  if (!state.freeze?.sha256) return undefined;
  const context = pkgRecoveryContext(state);
  return fs.existsSync(context.statePath) ? { pkgStaple: fileFact(context.statePath) } : undefined;
}
export function requirePkgRecovery(state) {
  const evidence = state.stages.packages?.evidence?.pkgStaple;
  if (!evidence) throw new Error('Retained private PKG checkpoint evidence is required');
  const context = pkgRecoveryContext(state);
  if (path.resolve(evidence.path) !== context.statePath) throw new Error('PKG receipt path differs from the candidate');
  verifyPkgResume(context, evidence);
  return context;
}
