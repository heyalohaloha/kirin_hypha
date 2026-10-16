// Diagnostic CI has an exact commit even when a shallow merge has no build number.
// Formal producers and all signed/distribution paths still require a real number.
import { readReleaseSourceIdentity } from '../ls_release/release_source_identity.mjs';

export function bindWindowsSourceIdentity(opts, { root, diagnostic = false } = {}) {
  const identity = readReleaseSourceIdentity({ root, requireBNumber: !diagnostic });
  if ((opts.commit && opts.commit !== identity.commit)
      || (opts.bNumber && opts.bNumber !== identity.bNumber)) {
    throw new Error('Windows source identity differs from this checkout');
  }
  return { ...opts, commit: identity.commit, bNumber: identity.bNumber };
}
