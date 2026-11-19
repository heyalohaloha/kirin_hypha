import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { violations } from './check_platform_header_boundary.mjs';
test('SDK includes are forbidden in every public header, including nested and conditional headers', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-header-boundary-'));
  try {
    const dir = path.join(root, 'juce_shell/src/nested'); fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'bad.h'), '#if JUCE_WINDOWS\n # include <Windows.h>\n#endif\n');
    fs.writeFileSync(path.join(dir, 'shell.h'), '#include "shlobj.h"\n');
    fs.writeFileSync(path.join(dir, 'safe.h'), '// #include <windows.h>\n/* #include <shlobj.h> */\n#include <vector>\n');
    fs.writeFileSync(path.join(dir, 'private.cpp'), '#include <windows.h>\n');
    assert.deepEqual(violations(root), ['juce_shell/src/nested/bad.h', 'juce_shell/src/nested/shell.h']);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
});
test('private SDK translation units are linked for Windows shipping and native tests', () => {
  const root = new URL('../', import.meta.url);
  const cmake = fs.readFileSync(new URL('juce_shell/cmake/WindowsPlatform.cmake', root), 'utf8');
  const files = [...cmake.matchAll(/src\/[^\s)]+\.cpp/g)].map(m => m[0]);
  assert.equal(files.length, 6);
  for (const file of files) {
    const text = fs.readFileSync(new URL('juce_shell/' + file, root), 'utf8');
    const include = text.indexOf('#include <windows.h>');
    assert.ok(include > text.indexOf('#define NOMINMAX') && include > text.indexOf('#define WIN32_LEAN_AND_MEAN'), file);
    assert.ok(text.indexOf('#if JUCE_WINDOWS') < include, file);
  }
  assert.match(cmake, /if\(WIN32\)/);
  assert.match(cmake, /link_libraries\(KirinWindowsPlatform\)/);
});
