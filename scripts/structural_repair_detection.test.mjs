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

const engineLifecycleIsOrdered = ({ format, processor, demand }) => {
  const prepare = between(
    format,
    'void KirinHyphaProcessorBase::prepareToPlay',
    'void KirinHyphaProcessorBase::applyHeldFormatIfRecordReleased',
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

const analysisSelectionIsAtomic = (runtime, worker) => {
  const fields = between(runtime, 'pub struct SpectrumRuntime {', '// SAFETY:');
  return fields.includes('selection: AtomicU64,')
    && !/^\s+(view|generation|analysis_mode|channel_mode|mid_side_enabled): Atomic/m.test(fields)
    && runtime.includes('selection: self.selection.load(Ordering::Acquire)')
    && worker.includes('AnalysisSelection::decode(block.selection, self.layout)')
    && worker.includes('let channel_mode = selection.channel_mode();');
};

const analysisIdentitiesAreSeparated = (runtime) => {
  const fields = between(runtime, 'pub struct SpectrumRuntime {', '// SAFETY:');
  const ingress = between(runtime, 'struct SpectrumIngressBlock {', 'struct SpectrumConsumers');
  const drop = between(runtime, 'fn note_drop(&self)', 'fn clear_mid_side_frame');
  return fields.includes('selection: AtomicU64,')
    && fields.includes('stream_generation: AtomicU64,')
    && ingress.includes('selection: u64,')
    && ingress.includes('stream_generation: u64,')
    && drop.includes('self.stream_generation.compare_exchange(')
    && !drop.includes('advance_selection_generation');
};

const comparisonRefusalIsTransported = ({ producer, header, ffi, editor, presentation }) => {
  const poll = between(
    ffi,
    'pub unsafe extern "C" fn kirin_hypha_poll_observatory_frame',
    'pub unsafe extern "C" fn kirin_hypha_poll_meter_history',
  );
  return producer.includes('ComparisonSnapshot::transition(')
    && producer.includes('DeltaMode::LayoutMismatch')
    && header.includes('uint8_t comparison_state;')
    && header.includes('uint8_t comparison_reason;')
    && header.includes('uint64_t comparison_generation;')
    && header.includes('uint64_t comparison_identity;')
    && poll.includes('comparison_projection(')
    && poll.includes('comparison_generation: comparison.generation')
    && editor.includes('frame.comparison_generation > comparisonActionAfterGeneration')
    && editor.includes('notifiesExplicitAction (')
    && presentation.includes('KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH')
    && presentation.includes('MATCH PRE / POST BUS');
};

const hiddenCompatibilityPathIsRemoved = ({ editor, meter, widgets, cmake }) => {
  const shippingEditor = editor + meter;
  return meter.includes('void KirinHyphaEditor::refreshWatchSnapshot()')
    && meter.includes('processorRef.pollWatchDisplay (watch)')
    && meter.includes('uint8_t KirinHyphaEditor::refreshRecordPhase()')
    && meter.includes('processorRef.pollRecordDisplay (observed)')
    && meter.includes('observatoryView.setKeepActive (keepActive)')
    && !shippingEditor.includes('pollDelta')
    && !shippingEditor.includes('DisplaySmoother')
    && !shippingEditor.includes('configureForKind')
    && !shippingEditor.includes('fillDelta')
    && !shippingEditor.includes('fillAbs')
    && !widgets.includes('class MetricCell')
    && !widgets.includes('class LoudnessSelector')
    && !cmake.includes('PostControls.cpp');
};

test('structural repair detectors reject the nine known mutation classes', () => {
  const adapter = read('juce_shell/src/HyphaAnalysisFfiAdapter.h');
  const demand = read('juce_shell/src/HyphaAnalysisDemand.h');
  const format = read('juce_shell/src/PluginProcessorFormat.cpp');
  const processor = read('juce_shell/src/PluginProcessor.cpp');
  const analysis = read('juce_shell/src/PluginProcessorAnalysis.cpp');
  const layout = read('juce_shell/src/HyphaTimeHistoryLayout.cpp');
  const painter = read('juce_shell/src/HyphaTimeHistoryPainter.cpp');
  const spectrumRuntime = read('crates/kirin_measure/src/spectrum_runtime.rs');
  const spectrumWorker = read('crates/kirin_measure/src/spectrum_runtime_worker.rs');
  const comparisonProducer = read('crates/kirin_measure/src/io_thread_post_tick.rs')
    + read('crates/kirin_measure/src/io_thread_post_delta.rs');
  const ffiHeader = read('crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h');
  const ffiSource = read('crates/kirin_hypha_ffi/src/lib.rs');
  const observatoryEditor = read('juce_shell/src/PluginEditorObservatory.cpp');
  const comparisonPresentation = read('juce_shell/src/HyphaComparisonPresentation.h');
  const editor = read('juce_shell/src/PluginEditor.cpp');
  const meter = read('juce_shell/src/PluginEditorMeter.cpp');
  const widgets = read('juce_shell/src/HyphaWidgets.h');
  const cmake = read('juce_shell/CMakeLists.txt');

  assert.ok(shippingAdapterIsExact(adapter));
  const lifecycle = { format, processor, demand };
  assert.ok(engineLifecycleIsOrdered(lifecycle));
  assert.ok(rejectedApplicationStaysPending(analysis));
  assert.ok(correlationRegionsAreSeparated(layout, painter));
  assert.ok(analysisSelectionIsAtomic(spectrumRuntime, spectrumWorker));
  assert.ok(analysisIdentitiesAreSeparated(spectrumRuntime));
  const comparisonTransport = {
    producer: comparisonProducer,
    header: ffiHeader,
    ffi: ffiSource,
    editor: observatoryEditor,
    presentation: comparisonPresentation,
  };
  assert.ok(comparisonRefusalIsTransported(comparisonTransport));
  const compatibilityPath = { editor, meter, widgets, cmake };
  assert.ok(hiddenCompatibilityPathIsRemoved(compatibilityPath));

  const wrongMidSide = adapter.replace(
    'MidSideVisible (handle, value)',
    'AbsoluteVisible (handle, value)',
  );
  assert.ok(!shippingAdapterIsExact(wrongMidSide), 'M+S misrouting must be detected');

  const earlyRestore = processor.replace(
    'writesEnabled.store (true, std::memory_order_release);\n    analysisApplication.engineReady();',
    'analysisApplication.engineReady();\n    writesEnabled.store (true, std::memory_order_release);',
  );
  assert.ok(
    !engineLifecycleIsOrdered({ ...lifecycle, processor: earlyRestore }),
    'early restore must be detected',
  );

  const staleGeneration = format.replace(
    'analysisApplication.engineCreated();',
    '/* missing engine generation invalidation */',
  );
  assert.ok(
    !engineLifecycleIsOrdered({ ...lifecycle, format: staleGeneration }),
    'stale engine generation must be detected',
  );

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

  const splitSelection = spectrumRuntime.replace(
    'selection: AtomicU64,',
    'view: AtomicU8,\n    generation: AtomicU64,\n    channel_mode: AtomicU8,',
  );
  assert.ok(
    !analysisSelectionIsAtomic(splitSelection, spectrumWorker),
    'split analysis selection must be detected',
  );

  const conflatedGeneration = spectrumRuntime.replace(
    'self.stream_generation.compare_exchange(',
    'self.advance_selection_generation();\n        self.stream_generation.compare_exchange(',
  );
  assert.ok(
    !analysisIdentitiesAreSeparated(conflatedGeneration),
    'audio discontinuity must not overwrite the control selection generation',
  );

  const droppedRefusal = ffiHeader.replace(
    'uint8_t comparison_reason;',
    'uint8_t reserved_comparison_reason;',
  );
  assert.ok(
    !comparisonRefusalIsTransported({ ...comparisonTransport, header: droppedRefusal }),
    'comparison refusal reason must reach the shipping Observatory frame',
  );

  const restoredHiddenDelta = meter.replace(
    'processorRef.pollWatchDisplay (watch)',
    'processorRef.pollDelta (watch)',
  );
  assert.ok(
    !hiddenCompatibilityPathIsRemoved({ ...compatibilityPath, meter: restoredHiddenDelta }),
    'an independent hidden delta polling path must be detected',
  );
});

test('release source gate resolves tracked ABI headers without ambient include paths', () => {
  const releaseGate = read('scripts/test_release_source.sh');
  assert.ok(releaseGate.includes(
    'ABI_INCLUDE_DIR="$ROOT/crates/kirin_hypha_ffi/include"',
  ));
  const uiCompile = between(releaseGate, 'run "${CXX:-c++}"', 'run "$UI_CONTRACT_BIN"');
  assert.ok(uiCompile.includes('-I "$ABI_INCLUDE_DIR"'));
});
