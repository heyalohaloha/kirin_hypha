// Only a complete product can be attached to an acceptance PASS.
export function requireCompleteRecordQuality(data) {
  const q = data?.record_quality, d = data?.trace_diagnostics;
  if (!q || !d || q.status !== 'complete' || q.complete !== true || q.usable !== true
      || q.expected_wav_ready !== true || q.sample_count_ready !== true || q.trace_slots_complete !== true
      || !Number.isSafeInteger(q.expected_frame_count) || q.expected_frame_count <= 0
      || q.measured_frame_count !== q.expected_frame_count || q.missing_trace_slots !== 0
      || d.expected_frame_count !== q.expected_frame_count || d.measured_frame_count !== q.measured_frame_count
      || d.missing_slots !== 0 || !Array.isArray(data.frames) || data.frames.length !== q.measured_frame_count) {
    throw new Error('Incomplete record quality or trace diagnostics');
  }
}
