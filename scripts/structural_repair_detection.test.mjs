import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

const between = (source, start, end) => {
  const first = source.indexOf(start);
  assert.notEqual(first, -1, `missing start marker: ${start}`);
  const last = source.indexOf(end, first + start.length);
  assert.notEqual(last, -1, `missing end marker: ${end}`);
  return source.slice(first, last);
};

const shippingAdapterIsExact = (source) => {
  const midSideMethod = between(
    source,
    'bool setMidSideSpectrumVisible',
    'bool setAbsoluteVisible',
  );
  const alias = between(source, 'using ShippingFfiAdapter', '>;');
  return midSideMethod.includes('MidSideVisible (handle, value)')
    && !midSideMethod.includes('AbsoluteVisible (handle, value)')
    && alias.includes('&kirin_hypha_set_mid_side_spectrum_visible')
    && alias.indexOf('&kirin_hypha_set_mid_side_spectrum_visible')
      < alias.indexOf('&kirin_hypha_set_absolute_visible');
};

const engineLifecycleIsOrdered = (processor, demand) => {
  const prepare = between(
    processor,
    'void KirinHyphaProcessorBase::prepareToPlay',
    'void KirinHyphaProcessorBase::releaseResources',
  );
  const enable = between(
    processor,
    'void KirinHyphaProcessorBase::enableWritesNow()',
    'startLocalBlindCaptureForPreparedFormat();',
  );
  const writes = enable.indexOf('writesEnabled.store (true');
  const ready = enable.indexOf('analysisApplication.engineReady()');
  const apply = enable.indexOf('serviceRequestedAnalysisUnderHandleLock()');
  return prepare.includes('analysisApplication.engineDestroyed()')
    && prepare.includes('analysisApplication.engineCreated()')
    && prepare.indexOf('analysisApplication.engineDestroyed()')
      < prepare.indexOf('kirin_hypha_destroy (hyphaHandle)')
    && prepare.indexOf('kirin_hypha_create')
      < prepare.indexOf('analysisApplication.engineCreated()')
    && writes >= 0 && writes < ready && ready < apply
    && demand.includes('appliedGeneration = 0;')
    && demand.includes('appliedDemand = {};');
};

const rejectedApplicationStaysPending = (source) => {
  const service = between(
    source,
    'bool KirinHyphaProcessorBase::serviceRequestedAnalysisUnderHandleLock()',
    'bool KirinHyphaProcessorBase::setAnalysisDemand',
  );
  const rejected = service.slice(service.indexOf('else'));
  return rejected.includes('analysisApplication.applicationFailed()')
    && !rejected.includes('analysisApplication.applicationSucceeded (demand)');
};

const correlationRegionsAreSeparated = (layout, painter) => {
  const geometry = between(layout, 'Geometry makeGeometry', 'return result;');
  const readout = geometry.indexOf('result.correlation.readout = lane.removeFromTop');
  const data = geometry.indexOf('result.correlation.data =');
  return readout >= 0 && readout < data
    && geometry.includes('static_cast<float> (lane.getY() + 1)')
    && painter.includes('&geometry.correlation, delta, presentation');
};

test('structural repair detectors reject the five known mutation classes', () => {
  const adapter = read('juce_shell/src/HyphaAnalysisFfiAdapter.h');
  const demand = read('juce_shell/src/HyphaAnalysisDemand.h');
  const processor = read('juce_shell/src/PluginProcessor.cpp');
  const analysis = read('juce_shell/src/PluginProcessorAnalysis.cpp');
  const layout = read('juce_shell/src/HyphaTimeHistoryLayout.cpp');
  const painter = read('juce_shell/src/HyphaTimeHistoryPainter.cpp');

  assert.ok(shippingAdapterIsExact(adapter));
  assert.ok(engineLifecycleIsOrdered(processor, demand));
  assert.ok(rejectedApplicationStaysPending(analysis));
  assert.ok(correlationRegionsAreSeparated(layout, painter));

  const wrongMidSide = adapter.replace(
    'MidSideVisible (handle, value)',
    'AbsoluteVisible (handle, value)',
  );
  assert.ok(!shippingAdapterIsExact(wrongMidSide), 'M+S misrouting must be detected');

  const earlyRestore = processor.replace(
    'writesEnabled.store (true, std::memory_order_release);\n    analysisApplication.engineReady();',
    'analysisApplication.engineReady();\n    writesEnabled.store (true, std::memory_order_release);',
  );
  assert.ok(!engineLifecycleIsOrdered(earlyRestore, demand), 'early restore must be detected');

  const staleGeneration = processor.replace(
    'analysisApplication.engineCreated();',
    '/* missing engine generation invalidation */',
  );
  assert.ok(!engineLifecycleIsOrdered(staleGeneration, demand), 'stale engine generation must be detected');

  const overlappingCorrelation = layout.replace(
    'static_cast<float> (lane.getY() + 1)',
    'static_cast<float> (result.correlation.bounds.getY() + 1)',
  );
  assert.ok(
    !correlationRegionsAreSeparated(overlappingCorrelation, painter),
    'CORR text/data overlap must be detected',
  );

  const falseSuccess = analysis.replace(
    'analysisApplication.applicationFailed();',
    'analysisApplication.applicationSucceeded (demand);',
  );
  assert.ok(!rejectedApplicationStaysPending(falseSuccess), 'API rejection as success must be detected');
});
