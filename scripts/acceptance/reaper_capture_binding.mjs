import crypto from 'node:crypto';

export function candidateKeyProof(key, candidate) {
  if (typeof key !== 'string' || !key || !candidate?.id || !/^[a-f0-9]{40}$/.test(candidate.commit || '')) throw new Error('Candidate key configuration invalid');
  return crypto.createHmac('sha256', key).update(`hypha-reaper-candidate-key-v1\0${candidate.id}\0${candidate.commit}`).digest('hex');
}

export function checkCapture(c) {
  if (!c || typeof c.projectHash !== 'string' || !c.projectHash.trim() || typeof c.sessionId !== 'string' || !c.sessionId.trim()
      || !/^[a-f0-9]{64}$/.test(c.sourceSha256 || '') || !Number.isSafeInteger(c.sampleRate) || c.sampleRate <= 0
      || !Number.isSafeInteger(c.frames) || c.frames <= 0 || !Number.isSafeInteger(c.selection?.startFrame)
      || c.selection.startFrame < 0 || !Number.isSafeInteger(c.selection?.endFrame)
      || c.selection.endFrame - c.selection.startFrame !== c.frames
      || typeof c.instances?.PRE !== 'string' || !c.instances.PRE.trim()
      || typeof c.instances?.POST !== 'string' || !c.instances.POST.trim() || c.instances.PRE === c.instances.POST) {
    throw new Error('Invalid capture binding');
  }
}
export function checkCaptureRecord(c, data, processedHash) {
  checkCapture(c);
  const wav = data?.expected_wav, take = data?.bounce_take;
  if (!['PRE', 'POST'].includes(data?.role) || data.project_hash !== c.projectHash || data.record_session_id !== c.sessionId
      || data.instance_id !== c.instances[data.role] || data.sample_rate !== c.sampleRate
      || wav?.expected_sample_rate !== c.sampleRate || wav?.expected_duration_samples !== c.frames
      || !/^[a-f0-9]{64}$/.test(processedHash || '') || wav.wav_hash !== processedHash
      || take?.sample_rate !== c.sampleRate || take.wav_start_sample !== 0 || take.wav_end_sample !== c.frames
      || take.duration_samples !== c.frames) throw new Error('Record capture binding differs');
}
export function checkCaptureAudio(c, audio) {
  checkCapture(c);
  if (audio?.sampleRate !== c.sampleRate || audio.frames !== c.frames || audio.reference?.sha256 !== c.sourceSha256
      || !/^[a-f0-9]{64}$/.test(audio.processed?.sha256 || '')) throw new Error('Audio capture binding differs');
}
