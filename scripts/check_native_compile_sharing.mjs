import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

// Inspect the compiler's real translation units, not just CMake target declarations.
// Keep shipping wrappers and the deliberately instrumented processor in their own profiles.
export function checkCompileSharing(commands) {
  if (!Array.isArray(commands) || !commands.length) throw new Error('empty compile inventory');
  const profiles = new Map();
  let sharedModules = 0, sharedSources = 0;
  for (const entry of commands) {
    const command = entry.command ?? entry.arguments?.join(' ');
    const target = command?.match(/CMakeFiles\/([^/]+)\.dir\//)?.[1];
    if (!target) continue;
    const file = entry.file.replaceAll('\\', '/');
    const isModule = file.includes('/JUCE/modules/');
    const isProduct = file.includes('/juce_shell/src/');
    const pooled = /^KirinNative(?:Juce|Sources)_/.test(target);
    const standalone = /Tests$/.test(target) || target === 'KirinHyphaReferencePreviewRenderer';
    if (standalone && target !== 'KirinAudioTransparencyContractTests' && (isModule || isProduct)) {
      throw new Error(`unshared translation unit: ${target}: ${file.split('/').at(-1)}`);
    }
    if (pooled) {
      const key = `${target}:${file}`;
      if (profiles.has(key)) throw new Error(`duplicate shared translation unit: ${key}`);
      profiles.set(key, true);
      if (isModule) sharedModules++;
      if (isProduct) sharedSources++;
    }
    if (/^KirinHypha(?:PRE|POST)(?:_|$)/.test(target)
        && /(?:KIRIN_HYPHA_TEST_WALL_CLOCK|KIRIN_REFERENCE_AUDIO_PAGES_TEST_HOOK|HYPHA_UPDATE_TRANSPORT_TESTING)=1/.test(command)) {
      throw new Error(`test definition reached shipping target: ${target}`);
    }
    if (target === 'KirinNativeTestProcessor' && isProduct
        && !command.includes('KIRIN_HYPHA_TEST_WALL_CLOCK=1')) {
      throw new Error('instrumented processor lost its fixture clock');
    }
  }
  if (!sharedModules || !sharedSources) throw new Error('missing shared native libraries');
  return { sharedModules, sharedSources };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    if (process.argv.length !== 3) throw new Error('expected compile_commands.json');
    console.log(checkCompileSharing(JSON.parse(readFileSync(process.argv[2], 'utf8'))));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
