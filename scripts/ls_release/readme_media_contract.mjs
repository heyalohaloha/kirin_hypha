import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

function sha256File(filePath) {
  return createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

export function verifyReadmeMedia(repoRoot) {
  const readme = fs.readFileSync(path.join(repoRoot, 'README.md'), 'utf8');
  // Retain the previously reviewed synthetic media; changed views use current native fixtures.
  const media = [
    ['vu.jpg', '5b24f1a2189932cffad99e1f7b4dc4dbf1c2cb513c268a98a34181c1b8184d16'],
    ['tour.gif', 'ed7610dcb69b25e7c49516b3cc7ce2bf9b76a8764e3063e697d61b8d14cc0aec'],
    ['level.jpg', '6e9adc35ebbf12d230f36a7be7e5d0ac36d679a1eaf7059ee88c335a788022a3'],
    ['blind.jpg', '9c5d4e6afbbe9ec92d0bfec71a9867ef2afecf2c1071b48c0061ccb5c1a891df'],
    ['ref_c.jpg', '72fe1a4afd7b471d99cdb9872f10137ed1714e53d77f0650487b9c50e5c9f9a8'],
    ['listen.jpg', 'd25e030f5cf8f573c4a7e0cbd6485df11226b7f89b5726b76a8401d02c778888'],
    ['drum.jpg', 'df501505285df6f44b8f1de920ea87f47d5e3c9b4dfe44eea8ae2c70ae031ae0'],
    ['freq.jpg', 'd034f472a12c0c7cadf3680cb9f62f59bc5549a871465b186f33e564f5f3fb20'],
    ['ref_v.jpg', 'e47898893abbe7dd1f4a835f1c620b6e3b9557e712e66647a2d80291b4133d2d'],
    ['space.jpg', 'd20f333fffb8aafc7582892262fc7d461a97a247bc63496cde95357d29b33620'],
    ['time.jpg', '540d8ea4e524fbbf26d5cd711909e68584782903d16cc1ee08adf77feeb94d8b'],
    ['sharp.jpg', 'c63265df16e077346121e88a981cf3dfbcb15d6b9e0748ac3f8d7a70a303933e'],
    ['live.jpg', '6c3c93f3f658970a582e93b4ce30a5e692e12d46a7af08b530839512e910fd90'],
    ['ref_b.jpg', '663193366318bee1900edd66c7b8a3c4d8edc64580a1dfd36c6a39a250a62023'],
    ['blind_result.jpg', '15ccbba11e6315463b418a3bd6060bd623ecce16ee0b0304263885f44dd06cee'],
  ];
  const replacements = {
    'drum.jpg': 'docs/planning/hypha_drum_psr_g2_20261008/drum-v2-all-900-en.png',
    'time.jpg': 'docs/planning/hypha_drum_psr_g2_20261008/time-900-en.png',
  };
  const currentPng = [
    [replacements['drum.jpg'], 'f9da76b079eb1ebd7ce6e7bad3637159718ae078b0e2f9c5003eeb392b945b00'],
    [replacements['time.jpg'], 'f90acffd2d668624fcb40d2f229f9805a4ded9a0ab48887c3817d343d5ff8a27'],
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
    assert.match(readme, /G2 development fixture; DAW acceptance remains in G3/);
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
