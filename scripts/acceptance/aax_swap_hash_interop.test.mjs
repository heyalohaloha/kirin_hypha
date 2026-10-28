import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { shellTreeHash, validateTreeName } from './aax_swap_shell_plan.mjs';

test('shell tree format binds names modes bytes and attribute bytes', () => {
  const digest = value => createHash('sha256').update(value).digest('hex');
  const data = { entries: [{ path: '', kind: 'directory', mode: 0o700 }, { path: 'file', kind: 'file', mode: 0o600, sha256: digest('bytes') }],
    attributes: [{ path: '', values: [] }, { path: 'file', values: [{ name: 'com.example.fixture', base64: Buffer.from('value').toString('base64') }] }] };
  const expected = `D\t700\t.\t-\nF\t600\tfile\t${digest('bytes')}\nX\tfile\tcom.example.fixture\t${digest(Buffer.from('value').toString('hex').toUpperCase())}\n`;
  assert.equal(shellTreeHash(data), digest(expected));
});

test('protocol names reject traversal and line delimiters before auth', () => {
  for (const name of ['../outside', '/absolute', 'a//b', 'a/./b', 'line\nname', 'tab\tname']) assert.throws(() => validateTreeName(name), /path/);
  assert.doesNotThrow(() => validateTreeName("quote'$()-日本語"));
});
