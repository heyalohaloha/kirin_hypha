// Usage: node browser-test.mjs PACK_DIRECTORY PLAYWRIGHT_MODULE_DIRECTORY NEW_EVIDENCE_DIRECTORY
// https://playwright.dev/docs/api/class-page and class-browsertype
import { readFile, mkdir } from 'node:fs/promises';
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [packDir, moduleDir, evidenceDir] = process.argv.slice(2);
if (!evidenceDir) throw new Error('Expected pack, Playwright module and new evidence directory');
const { chromium } = await import(pathToFileURL(path.join(moduleDir, 'index.mjs')));
await mkdir(evidenceDir, { recursive: false });
// Isolate browser operation tests from the host's output driver. This is NOT audible hardware verification.
// Chromium media_switches.cc documents the fake stream switches.
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--disable-audio-output'] });
const context = await browser.newContext({ viewport: { width: 1440, height: 1100 }, acceptDownloads: true });
await context.setOffline(true);
const page = await context.newPage(), errors = [], network = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (/^https?:/.test(request.url())) network.push(request.url()); });
const url = pathToFileURL(path.join(packDir, 'index.html')).href;
const manifest = JSON.parse(await readFile(path.join(packDir, 'manifest.json'), 'utf8'));
const key = `${manifest.pack_id}:${manifest.manifest_sha256}`;
const checks = [];
try {
  await page.goto(url);
  await page.waitForFunction(() => document.querySelectorAll('#tasks button').length === 6);
  assert((await page.locator('#save-status').textContent()).includes('自動保存済み')); checks.push('file URL / offline / storage');
  await page.evaluate(() => { const input = document.createElement('input'); input.type = 'file'; input.multiple = true;
    input.id = 'test-decode-input'; input.hidden = true; document.body.append(input); });
  await page.locator('#test-decode-input').setInputFiles(manifest.items.map(item => path.join(packDir, item.audio)));
  const decoded = await page.evaluate(async sources => {
    const files = [...document.querySelector('#test-decode-input').files], values = [];
    for (let i = 0; i < files.length; i++) {
      const source = sources[i], ctx = new OfflineAudioContext(source.channels, 1, source.rate);
      const pcm = await ctx.decodeAudioData(await files[i].arrayBuffer());
      let peak = 0, energy = 0;
      for (let ch = 0; ch < pcm.numberOfChannels; ch++) for (const sample of pcm.getChannelData(ch)) {
        peak = Math.max(peak, Math.abs(sample)); energy += sample * sample;
      }
      values.push({ frames: pcm.length, channels: pcm.numberOfChannels, rate: pcm.sampleRate,
        peak, rms: Math.sqrt(energy / (pcm.length * pcm.numberOfChannels)) });
    } return values;
  }, manifest.items);
  for (const [i, value] of decoded.entries()) {
    const expected = manifest.items[i];
    assert.equal(value.frames, expected.frames); assert.equal(value.rate, expected.rate); assert.equal(value.channels, expected.channels);
    assert(Math.abs(value.peak - expected.peak) < 1e-7); assert(Math.abs(value.rms - expected.rms) < 1e-10);
  }
  checks.push('browser offline decode: native frame count, rate, channels, peak and RMS match for all 6');
  for (const item of manifest.items) {
    await page.locator(`[data-task="${item.id}"]`).click();
    await page.waitForFunction(() => document.querySelector('audio').readyState >= 2);
    assert(Math.abs(await page.locator('audio').evaluate(audio => audio.duration) - 30) < .005);
    await page.locator('#play-preview').click();
    await page.waitForFunction(() => document.querySelector('audio').currentTime > 11.1);
    assert.equal(await page.locator('audio').evaluate(audio => audio.paused), false);
    await page.locator('#stop').click();
  }
  checks.push('6 native WAV decodes / durations / actual playback progress');
  await page.locator('[data-task="space_development-12"]').click();
  await page.locator('#cursor').fill('10.25'); await page.locator('#cursor').dispatchEvent('change'); await page.locator('#set-start').click();
  await page.locator('#cursor').fill('10.75'); await page.locator('#cursor').dispatchEvent('change'); await page.locator('#set-end').click();
  await page.locator('#add-interval').click(); assert.equal(await page.locator('#marks li').count(), 1);
  await page.locator('[name=decision][value=present]').check(); await page.locator('#confidence').selectOption('4');
  await page.locator('#note').fill('確認用の回答です。\n"引用"\t続き');
  await page.locator('#complete').click(); assert((await page.locator('#progress').textContent()).startsWith('1 / 6'));
  await page.reload(); assert((await page.locator('#task-title').textContent()).includes('2 / 3'));
  await page.locator('[data-task="space_development-12"]').click(); assert.equal(await page.locator('#marks li').count(), 1);
  assert((await page.locator('#note').inputValue()).includes('確認用')); checks.push('SPACE range / completion / reload and resume');
  await page.locator('[data-task="attack_development-17"]').click();
  await page.locator('#cursor').fill('12.345'); await page.locator('#cursor').dispatchEvent('change'); await page.locator('#add-point').click();
  await page.locator('[name=decision][value=present]').check(); await page.locator('#confidence').selectOption('5');
  await page.locator('#complete').click(); assert((await page.locator('#progress').textContent()).startsWith('2 / 6'));
  checks.push('ATTACK native point');
  const downloadPromise = page.waitForEvent('download'); await page.locator('#export-json').click(); const download = await downloadPromise;
  const jsonPath = path.join(evidenceDir, 'test-answers.json'); await download.saveAs(jsonPath);
  const exported = JSON.parse(await readFile(jsonPath, 'utf8'));
  assert.equal(exported.sources.length, 6); assert.equal(exported.answers['attack_development-17'].marks[0].start, Math.round(12.345 * 44100));
  assert.equal(exported.product_qualified, false);
  const tsvPromise = page.waitForEvent('download'); await page.locator('#export-tsv').click();
  const tsvPath = path.join(evidenceDir, 'test-answers.tsv'); await (await tsvPromise).saveAs(tsvPath);
  assert((await readFile(tsvPath, 'utf8')).startsWith('\uFEFF'));
  const freshContext = await browser.newContext(); await freshContext.setOffline(true);
  const restored = await freshContext.newPage(); await restored.goto(url);
  await restored.locator('#import-file').setInputFiles(jsonPath);
  await restored.waitForFunction(() => document.querySelector('#notice').textContent.includes('読み込みました'));
  assert((await restored.locator('#progress').textContent()).startsWith('2 / 6')); await freshContext.close();
  checks.push('JSON / TSV downloads / import into empty browser');
  const invalid = { ...exported, manifest_sha256: 'wrong' };
  await page.locator('#import-file').setInputFiles({ name: 'wrong.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(invalid)) });
  await page.waitForFunction(() => document.querySelector('#notice').textContent.includes('読み込めません'));
  assert((await page.locator('#progress').textContent()).startsWith('2 / 6')); checks.push('wrong manifest rejected without overwrite');
  await page.locator('#play-preview').click(); await page.waitForFunction(() => !document.querySelector('audio').paused);
  await page.locator('[data-task="space_development-03"]').click(); assert(await page.locator('audio').evaluate(audio => audio.paused));
  checks.push('navigation stops playback');
  for (const [width, height] of [[1440, 1100], [780, 1100], [390, 844]]) {
    await page.setViewportSize({ width, height });
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
    await page.screenshot({ path: path.join(evidenceDir, `review-${width}.png`), fullPage: true });
  }
  checks.push('desktop / narrow / mobile layout without horizontal overflow');
  await page.setViewportSize({ width: 1440, height: 1100 });
  await page.evaluate(() => { document.body.style.zoom = '2'; });
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
  assert(await page.locator('#note').evaluate(el => el.getBoundingClientRect().right <= document.querySelector('#review-card').getBoundingClientRect().right));
  await page.screenshot({ path: path.join(evidenceDir, 'review-zoom-200.png'), fullPage: true });
  checks.push('200% CSS zoom overflow check');
  await page.evaluate(() => { document.body.style.zoom = ''; document.querySelector('audio').src = 'audio/intentionally-missing.wav'; });
  await page.waitForFunction(() => !document.querySelector('#audio-error').hidden); checks.push('missing audio notification');
  // Isolated contexts: these deliberately fail storage; never touch the user's browser.
  const failedContext = await browser.newContext();
  await failedContext.addInitScript(() => { Storage.prototype.setItem = () => { throw new DOMException('quota', 'QuotaExceededError'); }; });
  const failed = await failedContext.newPage(); await failed.goto(url);
  assert((await failed.locator('#notice').textContent()).includes('自動保存ができません')); await failedContext.close();
  const corruptContext = await browser.newContext();
  // Inject before application startup; editing a live page's store is legitimately
  // replaced by its pagehide save and would not test corrupt-startup handling.
  await corruptContext.addInitScript(k => localStorage.setItem(k, '{invalid'), key);
  const corrupt = await corruptContext.newPage(); await corrupt.goto(url);
  assert((await corrupt.locator('#notice').textContent()).includes('既存データは上書きしません'));
  assert.equal(await corrupt.evaluate(k => localStorage.getItem(k), key), '{invalid'); await corruptContext.close();
  checks.push('storage failure and corrupt-state preservation');
  assert.deepEqual(errors, []); assert.deepEqual(network, []);
  console.log(JSON.stringify({ result: 'PASS', audio_sink: 'fake: no audible hardware verification',
    browser: await browser.version(), checks, pageErrors: errors, httpRequests: network }, null, 2));
} finally { await context.close(); await browser.close(); }
