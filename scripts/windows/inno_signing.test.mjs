import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { parseInnoArgs, signInnoFile } from './inno-sign-codesigntool.mjs';

function pe() {
  const bytes = Buffer.alloc(256);
  bytes.write('MZ');
  bytes.writeUInt32LE(128, 0x3c);
  bytes.write('PE\0\0', 128);
  bytes.writeUInt16LE(0x8664, 132);
  return bytes;
}

function fixture(t, name = 'Kirin Hypha Setup.exe') {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha inno fixture '));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const input = path.join(root, name);
  const unsigned = pe();
  fs.writeFileSync(input, unsigned);
  return { root, input, unsigned, name };
}

test('Inno signing CLI accepts exactly one file and no extra options', () => {
  assert.equal(parseInnoArgs(['--input-file', 'path with spaces.tmp']), 'path with spaces.tmp');
  for (const args of [[], ['--input-file'], ['--input-file', ''],
    ['--input-file', '--help'], ['--input-file', 'a', 'extra'], ['--batch-input-dir', 'a']]) {
    assert.throws(() => parseInnoArgs(args), /requires exactly/);
  }
});

for (const name of ['Kirin Hypha Setup.exe', 'temporary uninstaller.TMP']) {
  test(`Inno signer stages ${name} as an executable and replaces only verified output`, async (t) => {
    const f = fixture(t, name);
    const signed = Buffer.concat([f.unsigned, Buffer.from('fixture signature')]);
    let called = 0;
    const result = await signInnoFile(f.input, { logger: false, signer: async (input, output, options) => {
      called++;
      assert.notEqual(input, output);
      assert.equal(options.logger, false);
      const expectedName = name.endsWith('.TMP') ? 'uninstaller.exe' : name;
      assert.deepEqual(fs.readdirSync(input), [expectedName]);
      assert.deepEqual(fs.readFileSync(path.join(input, expectedName)), f.unsigned);
      assert.deepEqual(fs.readFileSync(f.input), f.unsigned);
      fs.writeFileSync(path.join(output, expectedName), signed);
    } });
    assert.equal(called, 1);
    assert.equal(result, f.input);
    assert.deepEqual(fs.readFileSync(f.input), signed);
    assert.deepEqual(fs.readdirSync(f.root), [name]);
  });
}

for (const mode of ['throw', 'missing', 'empty', 'bad header', 'unchanged', 'directory']) {
  test(`Inno signer preserves original and cleans staging on ${mode}`, async (t) => {
    const f = fixture(t);
    await assert.rejects(signInnoFile(f.input, { signer: async (input, output) => {
      assert.deepEqual(fs.readFileSync(f.input), f.unsigned);
      const target = path.join(output, f.name);
      if (mode === 'throw') throw new Error('fixture service failure');
      if (mode === 'empty') fs.writeFileSync(target, '');
      if (mode === 'bad header') fs.writeFileSync(target, Buffer.from('MZinvalid'));
      if (mode === 'unchanged') fs.copyFileSync(path.join(input, f.name), target);
      if (mode === 'directory') fs.mkdirSync(target);
    } }));
    assert.deepEqual(fs.readFileSync(f.input), f.unsigned);
    assert.deepEqual(fs.readdirSync(f.root), [f.name]);
  });
}

test('invalid PE and missing input never call the signing service', async (t) => {
  const f = fixture(t);
  const options = { signer: async () => assert.fail('must not call signer') };
  await assert.rejects(signInnoFile(path.join(f.root, 'missing.exe'), options), /missing/);
  for (const bytes of [Buffer.from(''), Buffer.from('MZ'), Buffer.alloc(256)]) {
    fs.writeFileSync(f.input, bytes);
    await assert.rejects(signInnoFile(f.input, options), /not a PE image/);
  }
  const corrupt = pe();
  corrupt.writeUInt32LE(0xffffffff, 0x3c);
  fs.writeFileSync(f.input, corrupt);
  await assert.rejects(signInnoFile(f.input, options), /not a PE image/);
  assert.deepEqual(fs.readdirSync(f.root), [f.name]);
});

test('concurrent input changes are not overwritten by the signing response', async (t) => {
  const f = fixture(t);
  const concurrent = Buffer.concat([f.unsigned, Buffer.from('concurrent change')]);
  await assert.rejects(signInnoFile(f.input, { signer: async (input, output) => {
    fs.writeFileSync(path.join(output, f.name), Buffer.concat([f.unsigned, Buffer.from('signed')]));
    fs.writeFileSync(f.input, concurrent);
  } }), /input changed/);
  assert.deepEqual(fs.readFileSync(f.input), concurrent);
  assert.deepEqual(fs.readdirSync(f.root), [f.name]);
});
