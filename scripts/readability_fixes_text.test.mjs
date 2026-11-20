import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
const read = name => fs.readFileSync(new URL(`../juce_shell/${name}`, import.meta.url), 'utf8')
test('lra-preparing product state expression, translation key and normal colour change together', () => {
  const metrics = read('src/HyphaObservatoryMetrics.cpp')
  assert.match(metrics, /"Preparing " \+ juce::String \(\(int\) std::floor \(observatoryFrame\.lra_elapsed_seconds\)\) \+ " s"/)
  assert.match(metrics, /textOverride\.startsWith \("Preparing "\)\s*\? COL_OBSERVATORY_VALUE/)
  assert.match(read('src/HyphaJapaneseObservatory.cpp'), /\{ "Preparing %1 s", u8"準備中 %1 秒" \}/)
  assert.doesNotMatch(metrics, /"WARMING "/)
})
test('bounce-notice complete requested sentences share the existing readable wrapping contract', () => {
  const english = 'Before a realtime bounce, press END and wait for the level to return'
  const japanese = 'リアルタイム書き出しは、ENDで音量が戻ってから'
  assert.ok(read('src/HyphaComparisonNotices.h').includes(`beforeRealtimeBounce = "${english}"`))
  assert.ok(read('src/HyphaJapaneseNotices.cpp').includes(`{ "${english}", u8"${japanese}" }`))
  assert.match(read('src/HyphaFooterNoticeFont.h'), /text == comparison_ui::beforeRealtimeBounce/)
})
