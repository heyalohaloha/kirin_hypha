// Required release stages and their direct prerequisites, in the default execution order.
export const STAGE_DEPENDENCIES = Object.freeze({
  'provenance-inputs': [], ci: ['provenance-inputs'], 'source-delivery': ['ci'],
  'macos-au-vst3': ['source-delivery'], 'macos-aax': ['macos-au-vst3'], windows: ['macos-aax'],
  freeze: ['windows'], hosts: ['freeze'], packages: ['hosts'], provenance: ['packages'],
  distribution: ['provenance'], github: ['distribution'], hp: ['github'],
  postrelease: ['hp'], ls: ['distribution'],
});
export const STAGES = Object.keys(STAGE_DEPENDENCIES);
export function releaseComplete(state) {
  return STAGES.every(stage => state.stages[stage]?.status === 'PASS');
}
export function prerequisites(stage) {
  if (!Object.hasOwn(STAGE_DEPENDENCIES, stage)) throw new Error('Unknown release stage');
  const result = new Set();
  const add = name => { for (const dep of STAGE_DEPENDENCIES[name]) { add(dep); result.add(dep); } };
  add(stage); return STAGES.filter(name => result.has(name));
}
export function stagePlan(until) {
  const targets = { packages: ['packages'], hp: ['hp'], postrelease: ['postrelease'], ls: ['ls'], complete: ['postrelease', 'ls'] }[until];
  if (!targets) throw new Error('Invalid --until');
  const required = new Set();
  const add = stage => { for (const dep of STAGE_DEPENDENCIES[stage]) add(dep); required.add(stage); };
  targets.forEach(add);
  return STAGES.filter(stage => required.has(stage));
}
