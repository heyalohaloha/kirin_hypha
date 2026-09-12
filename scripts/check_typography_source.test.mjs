import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { findTypographyViolations, scanTypographySources } from './check_typography_source.mjs';

test('typed multiline font calls and non-compressing labels pass', () => {
  const source = `
    g.setFont (monoFont (
        presentation, typography::TextRole::axis,
        typography::Composition::visualization));
    label.setMinimumHorizontalScale (1.0f);
  `;
  assert.deepEqual(findTypographyViolations(source), []);
});

test('comments and strings do not create false positives', () => {
  const source = `
    // monoFont (8.0f)
    const char* note = "juce::Font (12.0f) drawFittedText (x)";
    /* font.withHorizontalScale (0.5f); */
  `;
  assert.deepEqual(findTypographyViolations(source), []);
});

test('rectangle height changes are not mistaken for font mutations', () => {
  const source = `
    const auto band = inner.withY (42.0f).withHeight (inner.getHeight() * 0.12f);
    plot.setHeight (availableHeight);
  `;
  assert.deepEqual(findTypographyViolations(source), []);
});

test('semantic font results remain protected even when the variable is not named font', () => {
  const source = `
    auto metric = monoFont (presentation, typography::TextRole::axis);
    metric.setHeight (9.0f);
  `;
  assert.match(findTypographyViolations(source)[0]?.reason ?? '', /height mutation/);
});

for (const [name, source, reason] of [
  ['legacy numeric font', 'g.setFont (monoFont (8.0f));', /requires presentation context/],
  ['legacy variable font', 'g.setFont (labelFont (height));', /requires presentation context/],
  ['direct font', 'auto font = juce::Font ("Arial", 12.0f, 0);', /direct juce::Font/],
  ['font options', 'auto options = juce::FontOptions { 12.0f };', /FontOptions/],
  ['height mutation', 'auto smaller = font.withHeight (9.0f);', /height mutation/],
  ['horizontal scale', 'auto narrow = font.withHorizontalScale (0.7f);', /compression/],
  ['fitted text', 'g.drawFittedText (text, area, centred, 1, 0.7f);', /drawFittedText/],
  ['label compression', 'label.setMinimumHorizontalScale (0.72f);', /must remain 1.0/],
  ['untyped setFont', 'g.setFont (someFont);', /must consume a semantic typography factory/],
]) test(`${name} fails`, () => {
  assert.match(findTypographyViolations(source)[0]?.reason ?? '', reason);
});

test('untracked source files are included by the filesystem walk', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-typography-source-'));
  try {
    const directory = path.join(root, 'juce_shell', 'src');
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'NewSurface.cpp'), 'auto font = labelFont (size);\n');
    assert.deepEqual(scanTypographySources(root).map(finding => finding.path),
      ['juce_shell/src/NewSurface.cpp']);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
