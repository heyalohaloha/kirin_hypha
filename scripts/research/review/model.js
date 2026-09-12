// Shared browser/Node model. No DOM, audio, storage or network side effects.
(() => {
  'use strict';
  const schema = 'hypha.review.answers.v1';
  const decisions = ['', 'present', 'none', 'uncertain', 'unavailable'];
  const blank = () => ({ decision: '', confidence: '', note: '', marks: [], listened_ranges: [],
    complete: false, played_seconds: 0, updated_at: null });
  function fresh(pack) {
    return { schema, pack_id: pack.pack_id, manifest_sha256: pack.manifest_sha256,
      protocol: pack.protocol, reviewer: '', candidate_exposure: false, volume: 0.35,
      active_id: pack.items[0].id, answers: Object.fromEntries(pack.items.map(item => [item.id, blank()])) };
  }
  function coverageSeconds(answer, item) {
    return answer.listened_ranges.reduce((sum, range) => sum + range.end - range.start, 0) / item.rate;
  }
  function finishError(answer, item) {
    if (!answer.decision) return '判定を選んでください。';
    if (answer.decision === 'unavailable') return '';
    if (answer.played_seconds < 1) return '音を再生してから判定してください。';
    if (item.minimum_listened_fraction) {
      const required = (item.preview[1] - item.preview[0]) / item.rate * item.minimum_listened_fraction;
      if (coverageSeconds(answer, item) < required)
        return `判定範囲全体を確認してください（現在 ${coverageSeconds(answer, item).toFixed(1)} 秒）。`;
    }
    if (['present', 'none'].includes(answer.decision) && !answer.confidence) return '確信度を選んでください。';
    if (answer.decision === 'present' && !answer.marks.length)
      return item.mode === 'space' ? '減衰を追える区間を少なくとも1つ指定してください。' : '立ち上がり位置を少なくとも1つ指定してください。';
    if (answer.decision === 'none' && answer.marks.length) return '「該当なし」の場合は印を削除するか、判定を変更してください。';
    return '';
  }
  function validate(value, pack) {
    const fail = () => { throw new Error('このパックの回答形式と一致しません。現在の回答は変更していません。'); };
    if (!value || value.schema !== schema || value.pack_id !== pack.pack_id
      || value.manifest_sha256 !== pack.manifest_sha256 || value.protocol !== pack.protocol
      || value.candidate_exposure !== false || typeof value.reviewer !== 'string' || value.reviewer.length > 80
      || !Number.isFinite(value.volume) || value.volume < 0 || value.volume > 1
      || !pack.items.some(item => item.id === value.active_id)
      || !value.answers || Object.keys(value.answers).length !== pack.items.length) fail();
    const clean = fresh(pack);
    for (const key of ['reviewer', 'volume', 'active_id']) clean[key] = value[key];
    for (const item of pack.items) {
      const answer = value.answers[item.id];
      if (!answer || !decisions.includes(answer.decision) || !['', '1', '2', '3', '4', '5'].includes(answer.confidence)
        || typeof answer.note !== 'string' || answer.note.length > 4000 || typeof answer.complete !== 'boolean'
        || !Number.isFinite(answer.played_seconds) || answer.played_seconds < 0 || answer.played_seconds > 1e7
        || !(answer.updated_at === null || (typeof answer.updated_at === 'string' && Number.isFinite(Date.parse(answer.updated_at))))
        || !Array.isArray(answer.marks) || answer.marks.length > 300) fail();
      const marks = answer.marks.map(mark => {
        if (!mark || !Number.isSafeInteger(mark.start) || !Number.isSafeInteger(mark.end)
          || mark.start < item.preview[0] || mark.end >= item.preview[1] || mark.end < mark.start
          || (item.mode === 'attack' && mark.start !== mark.end)
          || (item.mode === 'space' && mark.start === mark.end)) fail();
        return { start: mark.start, end: mark.end };
      });
      if (!(answer.listened_ranges === undefined || Array.isArray(answer.listened_ranges))
        || (answer.listened_ranges?.length ?? 0) > 1000) fail();
      const listened = (answer.listened_ranges ?? []).map(range => {
        if (!range || !Number.isSafeInteger(range.start) || !Number.isSafeInteger(range.end)
          || range.start < item.preview[0] || range.end > item.preview[1] || range.end <= range.start) fail();
        return { start: range.start, end: range.end };
      }).sort((a, b) => a.start - b.start);
      const listened_ranges = [];
      for (const range of listened) {
        const previous = listened_ranges.at(-1);
        if (previous && range.start <= previous.end) previous.end = Math.max(previous.end, range.end);
        else listened_ranges.push(range);
      }
      const normalized = { ...blank(), decision: answer.decision, confidence: answer.confidence,
        note: answer.note, complete: answer.complete, played_seconds: answer.played_seconds,
        updated_at: answer.updated_at, marks, listened_ranges };
      if (normalized.complete && finishError(normalized, item)) fail();
      clean.answers[item.id] = normalized;
    }
    return clean;
  }
  function counts(state) {
    const values = Object.values(state.answers);
    return { complete: values.filter(a => a.complete).length,
      judged: values.filter(a => a.complete && ['present', 'none'].includes(a.decision)).length,
      uncertain: values.filter(a => a.complete && a.decision === 'uncertain').length,
      unavailable: values.filter(a => a.complete && a.decision === 'unavailable').length };
  }
  function tsv(state, pack) {
    const cell = value => {
      let text = String(value ?? '');
      if (/^[\s]*[=+@-]/.test(text)) text = "'" + text;
      return '"' + text.replaceAll('"', '""') + '"';
    };
    const headers = ['pack_id', 'manifest_sha256', 'reviewer', 'id', 'mode', 'audio_sha256',
      'sample_rate', 'source_start_sample', 'review_start_sample', 'review_end_exclusive',
      'decision', 'confidence', 'complete', 'played_seconds', 'listened_coverage_seconds',
      'listened_ranges_native_samples', 'marks_native_samples', 'note'];
    const rows = pack.items.map(item => {
      const answer = state.answers[item.id];
      return [state.pack_id, state.manifest_sha256, state.reviewer, item.id, item.mode, item.audio_sha256,
        item.rate, item.source_start_sample, ...item.preview, answer.decision, answer.confidence,
        answer.complete, answer.played_seconds.toFixed(3), coverageSeconds(answer, item).toFixed(3),
        JSON.stringify(answer.listened_ranges), JSON.stringify(answer.marks), answer.note];
    });
    return '\uFEFF' + [headers, ...rows].map(row => row.map(cell).join('\t')).join('\r\n') + '\r\n';
  }
  globalThis.HyphaReviewModel = { fresh, validate, counts, coverageSeconds, finishError, tsv };
})();
