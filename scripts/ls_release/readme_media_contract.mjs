import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

function sha256File(filePath) {
  return createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

export function verifyReadmeMedia(repoRoot) {
  const readme = fs.readFileSync(path.join(repoRoot, 'README.md'), 'utf8');
  // Captures of the released plug-in playing music, scaled to 1800 x 1200; the Reference views are
  // rendered with invented data. The DRUM BAND example below stays a labelled synthetic figure.
  const media = [
    ['vu.jpg', 'aa9dbdb668850b5866addf47dc1445cdb251470ac489d5fe569f7dae681f339d'],
    ['tour.gif', '61e9422a128eafd1596feba2a07c85af3ecc4655bf6b33b91ff1989d31092c4a'],
    ['level.jpg', 'a59a108541c5dc2ecf44388e1d29c9a171fef0ceb3cd08ec8141d206f29891f3'],
    ['blind.jpg', '9f3e93faa159329cc7b2dd827a4e2f251268ecb9c449b400713cb90f7e8054c7'],
    ['ref_c.jpg', '72fe1a4afd7b471d99cdb9872f10137ed1714e53d77f0650487b9c50e5c9f9a8'],
    ['listen.jpg', '48b51c7703dfb0d178c6aa4af27dc1a20a6249e8462aa6673104048d2fc491e0'],
    ['drum.jpg', '058e19e8a3d4ec63a3e43e00fa8eea0bf04b12df9fc18c0e793881fb8390eb17'],
    ['freq.jpg', '7048cb0f7f47a45a3da1e757a59cd4920d667a5f2173b03560f006309fc23677'],
    ['ref_v.jpg', 'e47898893abbe7dd1f4a835f1c620b6e3b9557e712e66647a2d80291b4133d2d'],
    ['space.jpg', '3db2fdfc3ffec8ff73c7b067708c9892a533b8cbf52d3376ce30ae5b32b32385'],
    ['time.jpg', '0488cbc1fed3ae6fae01cb89f9de4ea965fc9b0b27ee99a38cfe55beed6fabe0'],
    ['sharp.jpg', '5fbc192e26fa620bbf11cf149e56563a4d2f726daa9e1310a8533976aa10dbd7'],
    ['live.jpg', '2b91e5b28adb5eaad2895fadc5a09eb10ee6a421dd6c4433493770c4c5389524'],
    ['ref_b.jpg', '663193366318bee1900edd66c7b8a3c4d8edc64580a1dfd36c6a39a250a62023'],
    ['blind_result.jpg', '7049ba4191cdc45c82cc4d7a9a810aa34c49398f0b40f91a114f530362320c76'],
  ];
  const replacements = {};
  const currentPng = [
    ['docs/planning/hypha_drum_psr_g2_20261008/drum-v2-900-en.png', '65ee009ac500710559147593d1bcfed9efc7b3fa1adeee91ba2f49db2912a01f'],
  ];
  const signatures = { '.jpg': Buffer.from([0xff, 0xd8, 0xff]), '.gif': Buffer.from('GIF89a') };

  for (const [name, digest] of media) {
    const relativePath = `docs/media/readme/${name}`;
    const assetPath = path.join(repoRoot, relativePath);
    const signature = signatures[path.extname(name)];
    const shown = replacements[name] ?? relativePath;
    assert.match(readme, new RegExp(shown.replaceAll('.', '\\.')));
    if (replacements[name]) assert.ok(!readme.includes(relativePath), `${relativePath} is superseded`);
    assert.ok(fs.statSync(assetPath).size > 10_000, `${relativePath} must not be empty`);
    assert.equal(sha256File(assetPath), digest, `${relativePath} digest`);
    assert.deepEqual(fs.readFileSync(assetPath).subarray(0, signature.length), signature, `${relativePath} signature`);
  }
  assert.deepEqual(
    fs.readdirSync(path.join(repoRoot, 'docs/media/readme')).filter((name) => !name.startsWith('.')).sort(),
    media.map(([name]) => name).sort(),
    'the reviewed legacy media inventory remains controlled',
  );

  for (const [relativePath, digest] of currentPng) {
    const file = path.join(repoRoot, relativePath), bytes = fs.readFileSync(file);
    assert.ok(readme.includes(relativePath), `${relativePath} must be visible`);
    assert.ok(bytes.length > 10_000, `${relativePath} must not be empty`);
    assert.equal(sha256File(file), digest, `${relativePath} digest`);
    assert.deepEqual(bytes.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    assert.match(readme, /The BAND example uses synthetic measurements/);
    assert.doesNotMatch(readme, /\bG[234]\b/);
  }

  for (const retired of [
    'docs/images/hypha_record_mode.jpg',
    'docs/images/hypha_watch_mode.jpg',
    'docs/media/kirin-hypha-freq.jpg',
    'docs/media/kirin-hypha-freq-demo.mp4',
    'docs/media/kirin-hypha-sharp.jpg',
    'docs/media/kirin-hypha-live.jpg',
    'docs/media/kirin-hypha-pre-post.jpg',
    'docs/media/kirin-hypha-pre-post-demo.mp4',
    'docs/media/kirin-hypha-record-keep-demo.mp4',
  ]) {
    assert.equal(fs.existsSync(path.join(repoRoot, retired)), false, `${retired} must stay retired`);
    assert.doesNotMatch(readme, new RegExp(retired.replaceAll('.', '\\.')));
  }
}
