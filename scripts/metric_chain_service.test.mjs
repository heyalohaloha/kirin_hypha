import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
const source = name => fs.readFileSync(new URL(`../juce_shell/src/${name}`, import.meta.url), 'utf8');

test('Reference zero service belongs to POST processor lifetime, not its editor', () => {
  const constructor = source('PluginProcessor.cpp');
  const constructorBody = constructor.slice(constructor.indexOf('KirinHyphaProcessorBase::KirinHyphaProcessorBase'),
    constructor.indexOf('void KirinHyphaProcessorBase::releaseResources'));
  assert.match(constructorBody, /if \(role == Role::Post\) triggerAsyncUpdate\(\)/);
  assert.doesNotMatch(constructorBody, /\b(?:requestServiceTimer|startTimer)\s*\(/);
  const timer = source('PluginProcessorService.cpp');
  const deferred = timer.slice(timer.indexOf('void KirinHyphaProcessorBase::handleAsyncUpdate()'),
    timer.indexOf('void KirinHyphaProcessorBase::timerCallback()'));
  assert.match(deferred, /startTimer \(50\)/);
  const processorLifecycle = source('PluginProcessorLifecycle.cpp');
  assert.match(processorLifecycle, /cancelPendingUpdate\(\)/);
  assert.match(timer, /serviceReferenceMetrics\(\)/);
  assert.match(timer, /role != Role::Post && !metricChainDemand\.local\(\)/);
  const editor = source('PluginEditorLu.h');
  assert.doesNotMatch(editor, /shareReference|shareMetricZero/);
  const lifecycle = source('PluginEditorLifecycle.cpp');
  assert.match(lifecycle, /setReferenceMetricObservationEnabled \(false\)/);
  const service = source('PluginProcessorDisplayState.cpp');
  assert.match(service, /metricChainDemand\.local \(enabled\)/);
  assert.match(service, /metricChainDemand\.local\(\) \|\| metricChainDemand\.requested/);
  assert.match(service, /if \(!ownership\) return/);
  assert.match(service, /setMetricOwnershipLease \(ownership\)/);
  assert.match(service, /shareMetricZero \(scope, hypha::metric_needle::referenceZero/);
});

test('PRE requests the shared worker without constructing a second native scan', () => {
  const demand = source('HyphaMetricChainDemand.h');
  assert.match(demand, /vu_calibration::writeAtomicText/);
  assert.match(demand, /now - written < 1500/);
  assert.match(demand, /fields\[0\] != juce::String \(rate\)/);
  assert.match(demand, /fields\[1\] != juce::String \(channels\)/);
  assert.doesNotMatch(demand, /kirin_reference_metrics_create|scanPreparedSource|metrics::Runtime/);
  const owner = source('HyphaMetricChainOwner.h') + source('HyphaMetricChainOwner.cpp');
  assert.match(owner, /LOCK_EX \| LOCK_NB/);
  assert.match(owner, /OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT/);
  // Match calls, not prose explaining why a stable lock inode must remain.
  assert.doesNotMatch(owner, /\b(?:deleteFile|unlink|unlinkat|remove|DeleteFile[AW]?)\s*\(/);
  assert.doesNotMatch(owner, /\b(?:Thread::sleep|sleep|usleep|nanosleep|Sleep|SleepEx)\s*\(/);
  assert.match(owner, /parent\.isSymbolicLink\(\)/);
});


test('only a selected and demanded POST may hold or publish a chain lease', () => {
  const service = source('PluginProcessorDisplayState.cpp');
  const branch = service.slice(service.indexOf('void KirinHyphaProcessorBase::serviceReferenceMetrics()'));
  assert.ok(branch.indexOf('if (!controller)') < branch.indexOf('const auto ownership ='));
  assert.match(branch, /selection.libraryReceived && hypha::lu_needle::zeroRole \(selection\) != nullptr/);
  assert.match(branch, /if \(!selected \|\| !wanted\)/);
  const revoke = branch.indexOf('setMetricOwnershipLease ({})');
  const release = branch.indexOf('selectionOwnership (scope, selected, wanted)');
  assert.ok(revoke >= 0 && revoke < release, 'revoke controller before releasing processor owner');
  assert.ok(branch.indexOf('if (!ownership) return') < branch.indexOf('shareMetricZero'));
});


test('legacy LU publication remains editor-driven, independent of metric demand', () => {
  const editor = source('PluginEditorLu.h');
  const post = editor.slice(editor.indexOf('inline void refreshPost'), editor.indexOf('inline void refreshPre'));
  assert.match(post, /processor\.shareLuZero \(facts\)/);
  assert.ok(post.indexOf('processor.shareLuZero (facts)') < post.indexOf('setReferenceMetricObservationEnabled'));
  const service = source('PluginProcessorDisplayState.cpp');
  const metrics = service.slice(service.indexOf('void KirinHyphaProcessorBase::serviceReferenceMetrics()'),
    service.indexOf('void KirinHyphaProcessorBase::shareMetricZero'));
  assert.doesNotMatch(metrics, /shareLuZero/);
});


test('lease publication waits for both worker runtimes to apply the current epoch', () => {
  const control = source('reference_audition/ReferenceRuntimeV2Controller.cpp');
  assert.match(control, /metricOwnershipLease != lease\) metricOwnershipEpoch.changed\(\)/);
  assert.match(control, /!metricOwnershipLease \|\| !metricOwnershipEpoch.current\(\)/);
  const worker = source('reference_audition/ReferenceRuntimeV2Lifecycle.cpp');
  assert.ok(worker.indexOf('metricOwnershipEpoch.ticket()') < worker.indexOf('metricRuntime->setPublisherLease'));
  assert.ok(worker.indexOf('metricSetRuntime->setPublisherLease') < worker.indexOf('metricOwnershipEpoch.acknowledge'));
});


test('metric writer uses the retained lease scope rather than fetching a new scope', () => {
  const sourceText = source('PluginProcessorDisplayState.cpp');
  const writer = sourceText.slice(sourceText.indexOf('void KirinHyphaProcessorBase::shareMetricZero'),
    sourceText.indexOf('hypha::metric_needle::SharedReferenceZero KirinHyphaProcessorBase::sharedMetricZero'));
  assert.match(writer, /metricSharedZero.publish \(scope, sequenced\)/);
  assert.doesNotMatch(writer, /hybridVuCalibrationScope/);
  const demand = source('HyphaMetricChainDemand.h');
  assert.doesNotMatch(demand, /files.size\(\) > 64/);
  assert.match(demand, /peerCursor\+\+ % count/);
});
