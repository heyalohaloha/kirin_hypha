import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { digest, hash, treeFact } from '../ls_release/hypha_release_contract.mjs';
import { walkFiles } from './distribution_gate.mjs';
import { releaseSealedAliases, sealedAliasRules, sealedPayloadInventory } from './sealed_pace_alias.mjs';

// Disposable bundles shaped like a PACE-sealed AAX alias; fake bytes, no SDK, PACE tool or signing.
const LINK = 'Contents/Resources/__Pace_Eden/Signatures/codesign.dsig';
const TEXT = '../../../__Pace_Eden.bundle/Contents/Resources/Signatures/codesign.dsig';
const TARGET = 'Contents/__Pace_Eden.bundle/Contents/Resources/Signatures/codesign.dsig';
const SEAL = 'Contents/_CodeSignature/CodeResources';
const PLUGINS = 'Library/Application Support/Avid/Audio/Plug-Ins';
const LAYOUTS = [['macos-pkg', 'install_relative'], ['macos-zip', 'archive_relative']];

function put(root, name, bytes) {
  fs.mkdirSync(path.dirname(path.join(root, name)), { recursive: true });
  fs.writeFileSync(path.join(root, name), bytes);
}
function link(root, name, text) {
  fs.mkdirSync(path.dirname(path.join(root, name)), { recursive: true });
  fs.symlinkSync(text, path.join(root, name));
}
function relink(root, name, text) {
  fs.unlinkSync(path.join(root, name)); fs.symlinkSync(text, path.join(root, name));
}
function sealXml(links) {
  const records = links.map(([file, text]) => `\t\t<key>${file.replace(/^Contents\//, '')}</key>\n`
    + `\t\t<dict>\n\t\t\t<key>symlink</key>\n\t\t\t<string>${text}</string>\n\t\t</dict>\n`).join('');
  return '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0">\n<dict>\n\t<key>files</key>\n\t<dict/>\n'
    + `\t<key>files2</key>\n\t<dict>\n${records}\t\t<key>__Pace_Eden.bundle</key>\n\t\t<dict>\n`
    + '\t\t\t<key>cdhash</key>\n\t\t\t<data>ZmFrZQ==</data>\n\t\t</dict>\n\t</dict>\n</dict>\n</plist>\n';
}
function makeBundle(dir, role, { links = [[LINK, TEXT]], sealed = links } = {}) {
  put(dir, 'Contents/Info.plist', `fake ${role} plist`);
  put(dir, `Contents/MacOS/Kirin Hypha ${role}`, `fake ${role} binary`);
  put(dir, 'Contents/__Pace_Eden.bundle/Contents/Info.plist', 'fake nested plist');
  put(dir, TARGET, `fake ${role} detached signature`);
  for (const [file, text] of links) link(dir, file, text);
  put(dir, SEAL, sealXml(sealed));
}
function fixture(t, shapes = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-sealed-alias-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bundles = ['PRE', 'POST'].map(role => {
    const name = `Kirin Hypha ${role}.aaxplugin`;
    const spec = { role, sourcePath: path.join(root, 'build', name),
      install_relative: `${PLUGINS}/${name}`, archive_relative: `AAX/${name}` };
    makeBundle(spec.sourcePath, role, shapes[role]);
    return spec;
  });
  const frozen = [{ path: path.join(root, 'unrelated'), treeSha256: '0'.repeat(64) },
    ...bundles.map(b => treeFact(b.sourcePath))];
  const rules = () => sealedAliasRules({ bundles, frozenPayloads: frozen });
  return { root, bundles, frozen, rules };
}
function payload(f, channel, field) {
  const dir = path.join(f.root, channel);
  put(dir, 'LICENSE', 'fixture license');
  if (field) {
    for (const b of f.bundles) {
      fs.cpSync(b.sourcePath, path.join(dir, b[field]), { recursive: true, verbatimSymlinks: true });
    }
  }
  return dir;
}

test('frozen PACE aliases pass in PKG and ZIP layouts and stay out of the regular-file inventory', t => {
  const f = fixture(t); const rules = f.rules();
  for (const [channel, field] of LAYOUTS) {
    const result = sealedPayloadInventory(payload(f, channel, field), channel, rules);
    assert.deepEqual(result.aliases.map(a => [a.channel, a.role, a.path, a.text, a.target]),
      f.bundles.map(b => [channel, b.role, `${b[field]}/${LINK}`, TEXT, `${b[field]}/${TARGET}`]));
    for (const [index, alias] of result.aliases.entries()) {
      const bytes = fs.readFileSync(path.join(f.bundles[index].sourcePath, TARGET));
      assert.equal(alias.targetSha256, hash(bytes)); assert.equal(alias.targetSize, bytes.length);
      assert.equal(alias.sealSha256, hash(fs.readFileSync(path.join(f.bundles[index].sourcePath, SEAL))));
    }
    assert.ok(result.files.includes(`${f.bundles[0][field]}/${TARGET}`));
    assert.ok(result.files.includes('LICENSE'));
    assert.equal(result.files.filter(file => file.endsWith(LINK)).length, 0);
  }
  assert.deepEqual(sealedPayloadInventory(payload(f, 'windows-exe'), 'windows-exe', rules).aliases, []);
});

test('alias rules are derived from the frozen source tree, not from fixed names', t => {
  const shape = { links: [['Contents/Resources/seal/alias.bin', '../../__Pace_Eden.bundle/Contents/Info.plist']] };
  const f = fixture(t, { PRE: shape, POST: shape }); const rules = f.rules();
  assert.deepEqual(rules.channels['macos-zip'].map(r => [r.link, r.target]),
    f.bundles.map(() => ['Contents/Resources/seal/alias.bin', 'Contents/__Pace_Eden.bundle/Contents/Info.plist']));
  const dir = payload(f, 'macos-zip', 'archive_relative');
  assert.equal(sealedPayloadInventory(dir, 'macos-zip', rules).aliases.length, 2);
  link(dir, `AAX/Kirin Hypha PRE.aaxplugin/${LINK}`, TEXT);
  assert.throws(() => sealedPayloadInventory(dir, 'macos-zip', rules), /Unexpected symlink in macos-zip payload/);
});

const payloadCases = {
  'absolute link text': (dir, b) => relink(dir, `${b}/${LINK}`, path.join(dir, b, TARGET)),
  'escape beyond the bundle': (dir, b) => relink(dir, `${b}/${LINK}`,
    `../../../../../Kirin Hypha POST.aaxplugin/${TARGET}`),
  'dangling target': (dir, b) => fs.rmSync(path.join(dir, b, TARGET)),
  'chain through a target symlink': (dir, b) => {
    fs.renameSync(path.join(dir, b, TARGET), path.join(dir, b, `${TARGET}.real`));
    link(dir, `${b}/${TARGET}`, 'codesign.dsig.real');
  },
  'chain through an ancestor directory symlink': (dir, b) => {
    fs.renameSync(path.join(dir, b, 'Contents/__Pace_Eden.bundle'), path.join(dir, b, 'Contents/__Pace_Eden.real'));
    link(dir, `${b}/Contents/__Pace_Eden.bundle`, '__Pace_Eden.real');
  },
  'renamed link path': (dir, b) => fs.renameSync(path.join(dir, b, LINK), path.join(dir, b, `${LINK}2`)),
  'changed link text': (dir, b) => relink(dir, `${b}/${LINK}`, '../../../__Pace_Eden.bundle/Contents/Info.plist'),
  'changed target bytes': (dir, b) => {
    const bytes = fs.readFileSync(path.join(dir, b, TARGET)); bytes[0] ^= 1; put(dir, `${b}/${TARGET}`, bytes);
  },
  'changed target mode': (dir, b) => fs.chmodSync(path.join(dir, b, TARGET), 0o600),
  'added extra alias': (dir, b) => link(dir, `${b}/Contents/Resources/extra.dsig`,
    '../__Pace_Eden.bundle/Contents/Resources/Signatures/codesign.dsig'),
  'missing expected alias': (dir, b) => fs.unlinkSync(path.join(dir, b, LINK)),
  'changed signature seal': (dir, b) => put(dir, `${b}/${SEAL}`, sealXml([[LINK, TEXT]]).replace('ZmFrZQ', 'b3RoZXI')),
};
const payloadReasons = {
  'absolute link text': /must be relative/, 'escape beyond the bundle': /escapes its bundle/,
  'dangling target': /target is missing/, 'chain through a target symlink': /traverses a symlink/,
  'chain through an ancestor directory symlink': /traverses a symlink/,
  'renamed link path': /Missing sealed PACE alias/, 'changed link text': /text or target changed/,
  'changed target bytes': /target bytes changed/, 'changed target mode': /target mode changed/,
  'added extra alias': /Unexpected symlink/, 'missing expected alias': /Missing sealed PACE alias/,
  'changed signature seal': /code signature seal changed/,
};

test('payload aliases are rejected unless link, text, target, bytes, mode and seal match exactly', t => {
  for (const [channel, field] of LAYOUTS) {
    for (const [name, mutate] of Object.entries(payloadCases)) {
      const f = fixture(t); const rules = f.rules(); const dir = payload(f, channel, field);
      mutate(dir, f.bundles[0][field]);
      assert.throws(() => sealedPayloadInventory(dir, channel, rules), payloadReasons[name], `${channel}: ${name}`);
    }
  }
});

test('an alias in the Windows payload is rejected even when Mac aliases are frozen', t => {
  const f = fixture(t); const rules = f.rules();
  const dir = payload(f, 'windows-exe', 'archive_relative');
  assert.throws(() => sealedPayloadInventory(dir, 'windows-exe', rules), /Unexpected symlink in windows-exe payload/);
});

test('source aliases that are absolute, escaping, dangling, chained or unsealed yield no rules', t => {
  const hop = 'Contents/__Pace_Eden.bundle/Contents/Resources/Signatures/hop.dsig';
  const cases = [
    [{ links: [[LINK, '/Library/fixture/codesign.dsig']] }, /must be relative/],
    [{ links: [[LINK, '../../../../../outside/codesign.dsig']] }, /escapes its bundle/],
    [{ links: [[LINK, '../../../__Pace_Eden.bundle/Contents/Resources/Signatures/absent.dsig']] }, /not a regular file/],
    [{ links: [[LINK, '../../../__Pace_Eden.bundle/Contents/Resources/Signatures/hop.dsig'], [hop, 'codesign.dsig']] },
      /not a regular file/],
    [{ links: [[LINK, '../../../hop/Contents/Resources/Signatures/codesign.dsig'], ['Contents/hop', '__Pace_Eden.bundle']] },
      /traverses a symlink|not a regular file/],
    [{ links: [[LINK, '../../../__Pace_Eden.bundle/./Contents/Resources/Signatures/codesign.dsig']] }, /not canonical/],
    [{ links: [[LINK, TEXT]], sealed: [] }, /not sealed by the code signature/],
    [{ links: [[LINK, TEXT]], sealed: [[LINK, '../../../__Pace_Eden.bundle/Contents/Info.plist']] },
      /not sealed by the code signature/],
  ];
  for (const [shape, reason] of cases) {
    const f = fixture(t, { PRE: shape });
    assert.throws(f.rules, reason, JSON.stringify(shape));
  }
});

test('a source bundle whose tree fact is not frozen yields no alias rules', t => {
  const f = fixture(t);
  put(f.bundles[0].sourcePath, 'Contents/Info.plist', 'changed after freeze');
  assert.throws(f.rules, /not a frozen signed payload/);
  const g = fixture(t);
  relink(g.bundles[1].sourcePath, LINK, '../../../__Pace_Eden.bundle/Contents/Info.plist');
  assert.throws(g.rules, /not a frozen signed payload/);
  const h = fixture(t);
  assert.throws(() => sealedAliasRules({ bundles: h.bundles, frozenPayloads: h.frozen.slice(0, 2) }),
    /not a frozen signed payload/);
  assert.throws(() => sealedAliasRules({ bundles: h.bundles, frozenPayloads: [] }), /Frozen payload facts/);
});

test('an empty, partial, Windows or reshaped expected alias set cannot stand in for frozen source aliases', t => {
  const f = fixture(t); const dir = payload(f, 'macos-pkg', 'install_relative');
  const mutations = [r => { r.channels['macos-pkg'] = []; }, r => { r.channels['macos-zip'].pop(); },
    r => { r.channels['windows-exe'] = []; }, r => { r.bundles[0].aliasCount = 0; },
    r => { const rule = r.channels['macos-pkg'][0]; rule.bundle = '..'; rule.path = `../${rule.link}`; },
    r => { r.channels['macos-pkg'][0].target = 'Contents/Info.plist'; }];
  for (const mutate of mutations) {
    const rules = f.rules(); mutate(rules);
    assert.throws(() => sealedPayloadInventory(dir, 'macos-pkg', rules), /alias rule set/);
  }
});

test('source-dependency walks stay strict even for a frozen alias shape', t => {
  const f = fixture(t); assert.equal(f.rules().channels['macos-pkg'].length, 2);
  const dependency = path.join(f.root, 'dependency');
  fs.cpSync(f.bundles[0].sourcePath, path.join(dependency, 'vendored.bundle'), { recursive: true, verbatimSymlinks: true });
  assert.throws(() => walkFiles(dependency), /Source dependency contains symlink/);
});

test('release rules come from the integrity-checked freeze and the configured AAX bundles', t => {
  const f = fixture(t);
  put(f.root, 'config/hypha_macos_aax_bundles.json', JSON.stringify({ schema: 'kirin-hypha-macos-aax-bundles-v1',
    default_build_root: 'build', bundles: f.bundles.map(b => ({ role: b.role, source_relative: path.basename(b.sourcePath),
      install_relative: b.install_relative, archive_relative: b.archive_relative,
      executable_name: `Kirin Hypha ${b.role}`, bundle_identifier: `fixture.${b.role.toLowerCase()}` })) }));
  const freeze = { payloads: f.frozen }; freeze.sha256 = digest(freeze);
  const rules = releaseSealedAliases({ root: f.root, freeze });
  assert.deepEqual(rules.channels['macos-pkg'].map(r => r.path), f.bundles.map(b => `${b.install_relative}/${LINK}`));
  assert.throws(() => releaseSealedAliases({ root: f.root, freeze: { ...freeze, payloads: f.frozen.slice(0, 2) } }),
    /Frozen candidate receipt/);
  assert.throws(() => releaseSealedAliases({ root: f.root }), /Frozen candidate receipt/);
});
