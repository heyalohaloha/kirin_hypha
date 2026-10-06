import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const withoutComments = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '');
const updateFolder = 'juce_shell/src/update';
const updateSources = fs.readdirSync(path.join(root, updateFolder))
  .filter(name => /\.(?:h|cpp|mm)$/.test(name))
  .map(name => [name, withoutComments(read(`${updateFolder}/${name}`))]);

test('update storage and HTTP never use Kirin OS entitlement, Reference or Record domains', () => {
  assert.ok(updateSources.length >= 2, 'owned update sources must exist');
  for (const [name, source] of updateSources) {
    assert.doesNotMatch(source,
      /identity\.json|plugin_data|reference\/v2|ReferenceAuditionLease|ReferenceRuntime|StoragePaths|work\.json|kirin_hypha_(?:load_license|set_license|keep|set_identity)/,
      `update domain imports or accesses a protected domain in ${name}`);
  }
  const checker = read(`${updateFolder}/UpdateChecker.cpp`);
  assert.match(checker, /userApplicationDataDirectory/, 'use the host user update-storage base');
  assert.match(checker, /Kirin Hypha/, 'own product root, not the OS root');
  assert.match(checker, /UpdateCheck/, 'dedicated update-check directory');
});

test('only processor non-RT ownership may reference updater; audio, license, Record and Reference stay isolated', () => {
  const folders = ['juce_shell/src', 'juce_shell/src/reference_audition',
    'crates/kirin_measure/src', 'crates/kirin_hypha_ffi/src'];
  for (const folder of folders) {
    for (const name of fs.readdirSync(path.join(root, folder))) {
      if (! /\.(?:h|cpp|rs)$/.test(name)) continue;
      if (folder === 'juce_shell/src' && ! name.startsWith('PluginProcessor')) continue;
      let source = withoutComments(read(`${folder}/${name}`));
      if (folder === 'juce_shell/src' && name === 'PluginProcessor.h') {
        // Explicit ownership is required to drain BEFORE DLL detach. These exact
        // three declarations grant no HTTP/audio/runtime integration permission.
        for (const declaration of [
          '#include "update/UpdateServiceOwner.h"',
          'hypha::update::ServiceOwner& updateServicesForEditor() noexcept { return updateServiceOwner; }',
          'hypha::update::ServiceOwner updateServiceOwner;',
        ]) {
          assert.equal(source.split(declaration).length, 2, `unique non-RT ownership declaration: ${declaration}`);
          source = source.replace(declaration, '');
        }
      }
      assert.doesNotMatch(source, /hypha::update|update\/(?:UpdateStore|UpdateChecker|UpdateTransport)|fetchOfficialManifest/,
        `protected runtime depends on update services: ${folder}/${name}`);
    }
  }
});

test('module statics cannot retain workers and editor closes cannot release the processor owner', () => {
  const checker = read(`${updateFolder}/UpdateChecker.cpp`);
  assert.match(checker, /static std::map<juce::String, std::weak_ptr<Checker>> instances/);
  assert.doesNotMatch(checker, /static\s+[^;]*shared_ptr<Checker>/);
  const editor = read('juce_shell/src/PluginEditorUpdate.cpp');
  assert.match(editor, /processorRef\.updateServicesForEditor\(\)\.get/);
  const processor = withoutComments(read('juce_shell/src/PluginProcessor.cpp'));
  assert.doesNotMatch(processor, /updateService|updateChecker|fetchOfficialManifest|hypha::update/);
});

test('store uses bounded dedicated files, nonblocking OS exclusion and durable atomic reservation', () => {
  const source = read(`${updateFolder}/UpdateStore.cpp`);
  assert.match(source, /maximumBytes\s*=\s*32\s*\*\s*1024/);
  assert.match(source, /stateName\s*=\s*"state\.json"/);
  assert.match(source, /lockName\s*=\s*"owner\.lock"/);
  assert.match(source, /LOCK_EX\s*\|\s*LOCK_NB/);
  assert.match(source, /LOCKFILE_EXCLUSIVE_LOCK\s*\|\s*LOCKFILE_FAIL_IMMEDIATELY/);
  assert.match(source, /O_NOFOLLOW/);
  assert.match(source, /FILE_FLAG_OPEN_REPARSE_POINT/);
  assert.match(source, /fsync\s*\(file\)/);
  assert.match(source, /fsync\s*\(heldLock->directory\)/);
  assert.match(source, /FlushFileBuffers/);
  assert.match(source, /MOVEFILE_WRITE_THROUGH/);
  assert.match(source, /lastSuccess\s*>=\s*0\s*&&\s*value\.lastSuccess\s*<=\s*value\.lastAttempt/);
});
