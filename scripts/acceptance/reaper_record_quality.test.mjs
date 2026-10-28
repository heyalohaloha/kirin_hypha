import assert from 'node:assert/strict';
import test from 'node:test';
import { requireCompleteRecordQuality } from './reaper_record_quality.mjs';

const complete = () => ({ frames: [{}], record_quality: { status: 'complete', complete: true, usable: true,
  expected_wav_ready: true, sample_count_ready: true, trace_slots_complete: true,
  expected_frame_count: 1, measured_frame_count: 1, missing_trace_slots: 0 },
trace_diagnostics: { expected_frame_count: 1, measured_frame_count: 1, missing_slots: 0 } });

test('Record completion requires coherent quality and trace diagnostics', () => {
  assert.doesNotThrow(() => requireCompleteRecordQuality(complete()));
  for (const change of [
    d => { delete d.record_quality; }, d => { delete d.trace_diagnostics; },
    d => { d.record_quality.status = 'usable_fallback'; d.record_quality.complete = false; },
    d => { d.record_quality.trace_slots_complete = false; }, d => { d.record_quality.expected_wav_ready = false; },
    d => { d.record_quality.sample_count_ready = false; }, d => { d.record_quality.measured_frame_count = 2; },
    d => { d.trace_diagnostics.missing_slots = 1; }, d => { d.trace_diagnostics.expected_frame_count = 2; },
    d => { d.frames.push({}); }, d => { d.record_quality.expected_frame_count = 0; },
  ]) { const data = complete(); change(data); assert.throws(() => requireCompleteRecordQuality(data), /record/i); }
});
