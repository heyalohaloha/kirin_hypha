// Usage: node space_exhaustive_browser_test.mjs PACK_DIRECTORY PLAYWRIGHT_MODULE_DIRECTORY NEW_EVIDENCE_DIRECTORY
import assert from 'node:assert/strict';
import { mkdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [packDir, moduleDir, evidenceDir] = process.argv.slice(2);
if (!evidenceDir) throw new Error('Expected pack, Playwright module and new evidence directory');
const { chromium } = await import(pathToFileURL(path.join(moduleDir, 'index.mjs')));
await mkdir(evidenceDir, { recursive: false });
const browser = await chromium.launch({ channel: 'chrome', headless: true,
  args: ['--disable-audio-output'] });
const context = await browser.newContext({ viewport: { width: 1440, height: 1100 },
  acceptDownloads: true });
await context.setOffline(true);
const page = await context.newPage(), errors = [], network = [], checks = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (/^https?:/.test(request.url())) network.push(request.url()); });
const url = pathToFileURL(path.join(packDir, 'index.html')).href;
const manifestText = await readFile(path.join(packDir, 'manifest.json'), 'utf8');
const manifest = JSON.parse(manifestText), key = `${manifest.pack_id}:${manifest.manifest_sha256}`;
try {
  assert.equal(manifest.protocol, 'development-space-exhaustive-v2');
  assert.equal(manifest.annotation_scope,
    'development_exhaustive_all_trackable_decay_intervals_30s_v1');
  assert.equal(manifest.items.length, 6);
  assert(manifest.items.every(item => item.mode === 'space' && item.preview[0] === 0
    && item.preview[1] === item.frames && item.minimum_listened_fraction === .95
    && item.hide_source_title === true));
  assert(!/(source_path|research_excerpt|Rihanna|Kendrick|Miley|Qobuz)/i.test(manifestText));
  checks.push('candidate-blind manifest / full 30-second scope / no source identity');

  await page.goto(url);
  await page.waitForFunction(() => document.querySelectorAll('#tasks button').length === 6);
  assert.equal(await page.title(), 'Hypha｜SPACE全区間判定 01');
  assert((await page.locator('#study-heading').textContent()).includes('30秒'));
  assert((await page.locator('#listen-progress').textContent()).includes('0.0 / 30.0'));
  checks.push('offline startup / protocol-specific copy / coverage display');

  await page.evaluate(() => { const input = document.createElement('input'); input.type = 'file';
    input.multiple = true; input.id = 'test-decode-input'; input.hidden = true; document.body.append(input); });
  await page.locator('#test-decode-input').setInputFiles(
    manifest.items.map(item => path.join(packDir, item.audio)));
  const decoded = await page.evaluate(async sources => {
    const files = [...document.querySelector('#test-decode-input').files], values = [];
    for (let i = 0; i < files.length; i++) {
      const source = sources[i], audioContext = new OfflineAudioContext(source.channels, 1, source.rate);
      const pcm = await audioContext.decodeAudioData(await files[i].arrayBuffer());
      let peak = 0, energy = 0;
      for (let channel = 0; channel < pcm.numberOfChannels; channel++)
        for (const sample of pcm.getChannelData(channel)) {
          peak = Math.max(peak, Math.abs(sample)); energy += sample * sample;
        }
      values.push({ frames: pcm.length, channels: pcm.numberOfChannels, rate: pcm.sampleRate,
        peak, rms: Math.sqrt(energy / (pcm.length * pcm.numberOfChannels)) });
    }
    return values;
  }, manifest.items);
  for (const [number, value] of decoded.entries()) {
    const expected = manifest.items[number];
    assert.equal(value.frames, expected.frames); assert.equal(value.rate, expected.rate);
    assert.equal(value.channels, expected.channels); assert(Math.abs(value.peak - expected.peak) < 1e-7);
    assert(Math.abs(value.rms - expected.rms) < 1e-10);
  }
  checks.push('all six WAV files decode with exact frames/rate/channels and expected peak/RMS');

  for (const item of manifest.items) {
    await page.locator(`[data-task="${item.id}"]`).click();
    await page.waitForFunction(() => document.querySelector('audio').readyState >= 2);
    assert(Math.abs(await page.locator('audio').evaluate(audio => audio.duration) - 30) < .005);
    await page.locator('#play-preview').click();
    await page.waitForFunction(() => document.querySelector('audio').currentTime > .25);
    await page.locator('#stop').click();
  }
  checks.push('all six media elements make real playback progress with a fake output sink');

  const first = manifest.items[0];
  await page.locator(`[data-task="${first.id}"]`).click();
  await page.locator('#play-preview').click();
  await page.waitForFunction(() => document.querySelector('audio').currentTime > 1.1);
  await page.locator('#stop').click();
  await page.locator('#cursor').fill('2'); await page.locator('#cursor').dispatchEvent('change');
  await page.locator('#set-start').click(); await page.locator('#cursor').fill('3');
  await page.locator('#cursor').dispatchEvent('change'); await page.locator('#set-end').click();
  await page.locator('#add-interval').click();
  await page.locator('[name=decision][value=present]').check();
  await page.locator('#confidence').selectOption('4'); await page.locator('#complete').click();
  assert((await page.locator('#validation').textContent()).includes('判定範囲全体'));
  const covered = await page.evaluate(storageKey => JSON.parse(localStorage.getItem(storageKey)), key);
  covered.answers[first.id].listened_ranges = [{ start: 0, end: Math.floor(first.frames * .95) }];
  covered.answers[first.id].played_seconds = 28.5;
  page.once('dialog', dialog => dialog.accept());
  await page.locator('#import-file').setInputFiles({ name: 'covered.json', mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify(covered)) });
  await page.waitForFunction(() => document.querySelector('#notice').textContent.includes('読み込みました'));
  await page.locator(`[data-task="${first.id}"]`).click(); await page.locator('#complete').click();
  assert((await page.locator('#progress').textContent()).startsWith('1 / 6'));
  checks.push('interval annotation / insufficient coverage rejection / 95% union coverage acceptance');

  const downloadPromise = page.waitForEvent('download'); await page.locator('#export-json').click();
  const answerPath = path.join(evidenceDir, 'test-answers.json');
  await (await downloadPromise).saveAs(answerPath);
  const exported = JSON.parse(await readFile(answerPath, 'utf8'));
  assert.equal(exported.annotation_scope, manifest.annotation_scope);
  assert.equal(exported.answers[first.id].marks.length, 1);
  assert.equal(exported.answers[first.id].listened_ranges.length, 1);
  checks.push('portable JSON keeps exhaustive scope, native intervals and listening coverage');

  for (const [width, height] of [[1440, 1100], [780, 1100], [390, 844]]) {
    await page.setViewportSize({ width, height });
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
    await page.screenshot({ path: path.join(evidenceDir, `space-${width}.png`), fullPage: true });
  }
  await page.setViewportSize({ width: 1440, height: 1100 });
  await page.evaluate(() => { document.body.style.zoom = '2'; });
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
  await page.screenshot({ path: path.join(evidenceDir, 'space-zoom-200.png'), fullPage: true });
  checks.push('desktop / narrow / mobile / 200% zoom without horizontal overflow');

  await page.evaluate(() => { document.body.style.zoom = '';
    document.querySelector('audio').src = 'audio/intentionally-missing.wav'; });
  await page.waitForFunction(() => !document.querySelector('#audio-error').hidden);
  checks.push('missing audio produces an explicit visible failure');
  assert.deepEqual(errors, []); assert.deepEqual(network, []);
  console.log(JSON.stringify({ result: 'PASS', audio_sink: 'fake: no audible hardware verification',
    browser: await browser.version(), checks, pageErrors: errors, httpRequests: network }, null, 2));
} finally {
  await context.close(); await browser.close();
}
