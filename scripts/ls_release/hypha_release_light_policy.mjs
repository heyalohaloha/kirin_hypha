// Explicit new-candidate acceptance policy; v2 and frozen decisions never migrate.
export const LIGHT_FIX_ROWS_SCHEMA = 'hypha-release-fix-rows-light-v1';
export const BASELINE_SCOPE = 'ordinary-pass';
export const BASELINE_EXPECTED = {
  installed: true, launched: true, operated: true, saved: true, reopened: true, retained: true, newIssues: 0,
};
const text = value => typeof value === 'string' && value.trim().length > 0;
const unique = values => new Set(values).size === values.length;
const keys = (value, wanted) => value && typeof value === 'object' && !Array.isArray(value)
  && Object.keys(value).sort().join() === [...wanted].sort().join();
const formats = ['AU', 'VST3', 'AAX'];
export function lightPolicy(spec, candidate, promises, daws, canonicalDaw, sensitive) {
  if (!/^\d+\.\d+\.\d+$/.test(candidate.version)
      || candidate.version.split('.').map(Number).some((n, i, a) =>
        i === 0 && n < 1 || i === 1 && a[0] === 1 && n < 1
        || i === 2 && a[0] === 1 && a[1] === 1 && n < 58)) {
    throw new Error('Light acceptance is available only for new candidates from 1.1.58');
  }
  const plan = spec.impact;
  if (!Array.isArray(plan) || plan.length !== promises.length || !unique(plan.map(item => item?.promise))) {
    throw new Error('Light impact plan must cover each release-note promise exactly once');
  }
  for (const item of plan) {
    if (!keys(item, ['promise', 'reason', 'scope', 'targets', 'transitions'])
        || !promises.includes(item.promise) || !text(item.reason)
        || !['common', 'host', 'platform', 'format'].includes(item.scope)
        || !Array.isArray(item.targets) || !item.targets.length
        || !Array.isArray(item.transitions) || item.transitions.some(t => !text(t)) || !unique(item.transitions)) {
      throw new Error('Light impact needs a known scope, reason, explicit targets and transition list');
    }
    const targetKeys = [];
    for (const target of item.targets) {
      if (!keys(target, ['daw', 'os', 'formats']) || !canonicalDaw(target.daw)
          || !['macos', 'windows'].includes(target.os) || !Array.isArray(target.formats)
          || !target.formats.length || !unique(target.formats) || target.formats.some(f => !formats.includes(f))
          || (target.os === 'windows' && target.formats.includes('AU'))
          || (canonicalDaw(target.daw) === 'Logic Pro' && target.os !== 'macos')
          || (canonicalDaw(target.daw) === 'Cakewalk' && target.os !== 'windows')) {
        throw new Error('Light impact target needs a known DAW, OS and applicable formats');
      }
      targetKeys.push(`${canonicalDaw(target.daw)}/${target.os}`);
      for (const format of target.formats) {
        const rows = spec.rows.filter(row => row.kind === 'host' && row.depth === 'deep' && !row.scope
          && (row.promise === item.promise || row.promises?.includes(item.promise))
          && canonicalDaw(row.daw) === canonicalDaw(target.daw) && row.os === target.os
          && row.hosts?.some(host => host.split('/')[2] === format));
        if (!rows.length || !rows.some(row => item.transitions.every(t => row.expected?.transitions?.[t] === true))) {
          throw new Error(`Light impact lacks deep host evidence for ${item.promise}: ${target.daw}/${target.os}/${format}`);
        }
      }
    }
    if (!unique(targetKeys)) throw new Error('Light impact targets must be distinct');
    if ((item.scope === 'platform' || sensitive.includes(item.promise))
        && !['macos', 'windows'].every(os => item.targets.some(t => t.os === os))) {
      throw new Error('Platform-sensitive light impact needs both operating systems');
    }
  }
  const baselines = spec.rows.filter(row => row.scope === BASELINE_SCOPE);
  for (const daw of daws) {
    if (baselines.filter(row => canonicalDaw(row.daw) === daw).length !== 1) {
      throw new Error(`Light acceptance needs exactly one ordinary pass for ${daw}`);
    }
  }
}

export function checkBaseline(row) {
  if (row.kind !== 'host' || row.depth !== 'baseline' || Object.hasOwn(row, 'promise') || Object.hasOwn(row, 'promises')
      || Object.entries(BASELINE_EXPECTED).some(([key, value]) => row.expected?.[key] !== value)) {
    throw new Error('Ordinary pass needs the installation, operation, save and reopen expectations and no promise');
  }
}
