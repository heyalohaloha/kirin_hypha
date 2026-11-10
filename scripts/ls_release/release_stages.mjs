// Required release stages and their direct prerequisites, in the default execution order.
export const STAGE_DEPENDENCIES = Object.freeze({
  'provenance-inputs': [], ci: ['provenance-inputs'], 'source-delivery': ['ci'],
  'macos-au-vst3': ['source-delivery'], 'macos-aax': ['macos-au-vst3'], windows: ['macos-aax'],
  // The PKG is built before host acceptance: one administrator install of that PKG serves the
  // host tests and the clean-install check, and every host test runs the bytes users receive.
  freeze: ['windows'], packages: ['freeze'], hosts: ['packages'], provenance: ['hosts'],
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
