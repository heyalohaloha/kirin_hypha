(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const pack = JSON.parse($('review-data').textContent), model = globalThis.HyphaReviewModel;
  const ui = pack.ui ?? {};
  const key = `${pack.pack_id}:${pack.manifest_sha256}`, audio = $('audio');
  let state = model.fresh(pack), storageBlocked = false, index = 0, start = null, end = null;
  let stopAt = 30, playToken = 0, raf = 0, lastAudioTime = null, savedSecond = 0;
  for (const [id, value] of [['study-eyebrow', ui.eyebrow], ['study-heading', ui.heading],
    ['study-subtitle', ui.subtitle], ['about-primary', ui.about_primary],
    ['about-secondary', ui.about_secondary], ['footer-copy', ui.footer]]) {
    if (value) $(id).textContent = value;
  }
  document.title = ui.document_title ?? document.title;
  $('tasks').setAttribute('aria-label', `判定する${pack.items.length}項目`);
  function notice(text) { $('notice').textContent = text; $('notice').hidden = !text; }
  try {
    const stored = localStorage.getItem(key);
    if (stored) state = model.validate(JSON.parse(stored), pack);
  } catch {
    storageBlocked = true;
    notice('ブラウザ内の回答を読み込めませんでした。既存データは上書きしません。回答JSONを読み込むか、今回の回答をJSONで保存してください。');
  }
  function persist() {
    if (storageBlocked) { $('save-status').textContent = 'ブラウザ保存不可 / JSON保存を使ってください'; return; }
    try { localStorage.setItem(key, JSON.stringify(state)); $('save-status').textContent = 'このブラウザに自動保存済み'; }
    catch { storageBlocked = true; $('save-status').textContent = '保存できません / JSON保存が必要です';
      notice('自動保存ができません。閉じる前に「回答JSONを保存」を押してください。'); }
  }
  const item = () => pack.items[index], answer = () => state.answers[item().id];
  const seconds = sample => sample / item().rate;
  const wave = new globalThis.HyphaReviewWave($('wave'), sample => {
    $('cursor').value = (sample / item().rate).toFixed(6);
  });
  function changed() {
    answer().complete = false; answer().updated_at = new Date().toISOString();
    $('validation').textContent = ''; persist(); refreshNavigation();
  }
  function refreshNavigation() {
    const counts = model.counts(state);
    $('progress').textContent = `${counts.complete} / ${pack.items.length} 回答済み（判定 ${counts.judged} / 判断不能 ${counts.uncertain} / 再生不可 ${counts.unavailable}）`;
    $('tasks').replaceChildren(...pack.items.map((entry, number) => {
      const button = document.createElement('button'); button.type = 'button'; button.dataset.task = entry.id;
      button.setAttribute('aria-current', String(number === index));
      const modeNumber = pack.items.slice(0, number + 1).filter(item => item.mode === entry.mode).length;
      const title = document.createElement('span'); title.textContent = entry.display_label
        ?? `${entry.mode === 'space' ? 'SPACE' : 'ATTACK'} ${modeNumber}`;
      const label = document.createElement('span'); label.className = 'nav-state';
      const a = state.answers[entry.id]; label.textContent = a.complete ? '済' : a.updated_at ? '途中' : '未';
      button.append(title, label); button.onclick = () => show(number); return button;
    }));
    $('task-status').textContent = answer().complete ? '回答済み' : answer().updated_at ? '途中保存' : '未回答';
  }
  function refreshListening() {
    if (!item().minimum_listened_fraction) { $('listen-progress').textContent = ''; return; }
    const total = seconds(item().preview[1] - item().preview[0]);
    const heard = Math.min(total, model.coverageSeconds(answer(), item()));
    $('listen-progress').textContent = `判定範囲の確認 ${heard.toFixed(1)} / ${total.toFixed(1)} 秒`;
  }
  function stop() {
    playToken++; audio.pause(); cancelAnimationFrame(raf); raf = 0;
    lastAudioTime = null; stopAt = 30; wave.playhead = null; wave.draw();
  }
  function refreshRange() {
    $('wave-range').textContent = `${seconds(wave.left).toFixed(2)}〜${seconds(wave.right).toFixed(2)}秒 / 印は${seconds(item().preview[0]).toFixed(1)}〜${seconds(item().preview[1]).toFixed(1)}秒に付けます`;
  }
  function refreshMarks() {
    wave.marks = answer().marks; wave.draw();
    $('marks').replaceChildren(...answer().marks.map((mark, number) => {
      const li = document.createElement('li'), text = document.createElement('span'), remove = document.createElement('button');
      const seconds = sample => (sample / item().rate).toFixed(3);
      text.textContent = `${number + 1}. ${seconds(mark.start)} s${item().mode === 'space' ? ' → ' + seconds(mark.end) + ' s' : ''}`;
      remove.textContent = '削除'; remove.setAttribute('aria-label', `印 ${number + 1} を削除`);
      remove.onclick = () => { answer().marks.splice(number, 1); changed(); refreshMarks(); };
      li.append(text, remove); return li;
    }));
    const s = sample => sample === null ? '未指定' : (sample / item().rate).toFixed(3) + '秒';
    $('draft-range').textContent = item().mode === 'space' ? `追加する区間：${s(start)} → ${s(end)}` : '印は聞こえた位置に付けます。機械の検出候補はありません。';
    $('add-interval').disabled = start === null || end === null || end <= start;
  }
  function show(number) {
    stop(); index = Math.max(0, Math.min(pack.items.length - 1, number)); start = null; end = null;
    state.active_id = item().id; savedSecond = Math.floor(answer().played_seconds);
    const space = item().mode === 'space';
    const modeNumber = pack.items.slice(0, index + 1).filter(entry => entry.mode === item().mode).length;
    const modeTotal = pack.items.filter(entry => entry.mode === item().mode).length;
    $('task-kind').textContent = item().task_kind ?? (space ? 'SESSION 1 / SPACE' : 'SESSION 2 / 2MIX ATTACK');
    $('task-title').textContent = item().display_label
      ?? `${space ? '減衰を追える区間' : '音の立ち上がり'} ${modeNumber} / ${modeTotal}`;
    $('question').textContent = item().question ?? (space ? '音が立ち上がった後、小さくなっていく様子を追えますか？' : '独立した音の立ち上がりを、どこに感じますか？');
    $('guide').textContent = item().guide ?? (space
      ? '判定する10秒を聴き、追える減衰があれば代表的な区間を1つ以上指定してください。残響かどうかを当てる必要はありません。'
      : '判定する10秒を聴き、分かる立ち上がりに印を付けてください。初回は代表的な位置だけでも構いません。全イベントを注釈した精度評価とは分けて扱います。');
    $('present-label').textContent = item().present_label ?? (space ? '減衰を追える区間がある' : '独立した立ち上がりがある');
    $('none-label').textContent = item().none_label ?? (space ? '追える減衰区間がない' : '独立した立ち上がりがない');
    $('add-point').hidden = space; $('interval-controls').hidden = !space;
    $('confidence').value = answer().confidence; $('note').value = answer().note;
    document.querySelectorAll('[name=decision]').forEach(radio => { radio.checked = radio.value === answer().decision; });
    $('audio-error').hidden = true; $('validation').textContent = '';
    audio.src = item().audio; audio.load(); audio.volume = state.volume;
    $('cursor').max = String((item().frames - 1) / item().rate);
    $('cursor').value = seconds(item().preview[0]).toFixed(3); wave.load(item(), answer().marks); refreshRange(); refreshMarks(); refreshNavigation(); refreshListening();
    $('previous').disabled = index === 0; $('next').disabled = index === pack.items.length - 1;
    $('next').textContent = answer().complete ? '次の項目へ' : '未回答のまま次へ';
    const sourceStart = item().source_start_sample / item().rate;
    const sourceEnd = sourceStart + item().frames / item().rate;
    $('source-info').textContent = `${item().hide_source_title ? item().display_label : item().title} / ${item().rate.toLocaleString()} Hz / ${item().channels} ch / 原曲の${sourceStart.toFixed(0)}〜${sourceEnd.toFixed(0)}秒。`;
    const previewSeconds = seconds(item().preview[1] - item().preview[0]);
    $('play-preview').textContent = `判定する${previewSeconds.toFixed(0)}秒を再生`;
    $('play-context').hidden = item().preview[0] === 0 && item().preview[1] === item().frames;
    persist();
  }
  function recordListening(from, to) {
    const first = Math.max(item().preview[0], Math.floor(from * item().rate));
    const last = Math.min(item().preview[1], Math.ceil(to * item().rate));
    if (last <= first) return;
    const ranges = [...answer().listened_ranges, { start: first, end: last }].sort((a, b) => a.start - b.start);
    answer().listened_ranges = [];
    for (const range of ranges) {
      const previous = answer().listened_ranges.at(-1);
      if (previous && range.start <= previous.end + 1) previous.end = Math.max(previous.end, range.end);
      else answer().listened_ranges.push(range);
    }
  }
  function tick() {
    if (audio.paused) { raf = 0; return; }
    const now = audio.currentTime;
    if (lastAudioTime !== null && now > lastAudioTime && now - lastAudioTime < .5) {
      answer().played_seconds += now - lastAudioTime; recordListening(lastAudioTime, now);
    }
    lastAudioTime = now;
    if (Math.floor(answer().played_seconds) > savedSecond) { savedSecond = Math.floor(answer().played_seconds); persist(); refreshListening(); }
    wave.playhead = now * item().rate; wave.draw();
    if (now >= stopAt) { stop(); persist(); return; }
    raf = requestAnimationFrame(tick);
  }
  async function play(from, until) {
    stop(); const token = playToken; stopAt = until; audio.currentTime = from;
    $('audio-error').hidden = true;
    try { await audio.play(); if (token !== playToken) return; }
    catch { if (token === playToken) { $('audio-error').hidden = false;
      $('audio-error').textContent = '再生できません。音源フォルダとブラウザを確認してください。「再生できない」も回答できます。'; } }
  }
  audio.addEventListener('play', () => { lastAudioTime = audio.currentTime; if (!raf) raf = requestAnimationFrame(tick); });
  audio.addEventListener('pause', () => { cancelAnimationFrame(raf); raf = 0; lastAudioTime = null; persist(); });
  audio.addEventListener('seeking', () => { lastAudioTime = null; });
  audio.addEventListener('error', () => {
    $('audio-error').hidden = false;
    $('audio-error').textContent = audio.error?.message?.includes('AUDIO_RENDERER_ERROR')
      ? '音源は読み込めましたが、ブラウザの音声出力に失敗しました。回答JSONを保存し、出力先を確認してページを再読込してください。'
      : '音源の読込みまたは再生に失敗しました。index.htmlとaudioフォルダを一緒に置き、Chromeで開いてください。';
  });
  $('play-preview').onclick = () => play(seconds(item().preview[0]), seconds(item().preview[1]));
  $('play-context').onclick = () => play(0, 30);
  $('play-cursor').onclick = () => play(seconds(wave.cursor), wave.cursor < item().preview[1] ? seconds(item().preview[1]) : 30);
  $('stop').onclick = stop;
  $('volume').value = String(state.volume * 100);
  const volumeLabel = () => { $('volume-value').textContent = `${Math.round(state.volume * 100)}%`; };
  volumeLabel();
  $('volume').oninput = () => { state.volume = Number($('volume').value) / 100; audio.volume = state.volume; volumeLabel(); persist(); };
  audio.addEventListener('volumechange', () => { state.volume = audio.volume; $('volume').value = String(state.volume * 100); volumeLabel(); persist(); });
  $('cursor').onchange = () => {
    const value = Number($('cursor').value);
    if ($('cursor').value === '' || !Number.isFinite(value)) { $('validation').textContent = '秒数を入力してください。'; return; }
    wave.setCursor(value * item().rate);
  };
  $('zoom-in').onclick = () => { wave.zoom(.5); refreshRange(); };
  $('zoom-out').onclick = () => { wave.zoom(2); refreshRange(); };
  $('view-all').onclick = () => { wave.left = 0; wave.right = item().frames; wave.draw(); refreshRange(); };
  $('view-preview').onclick = () => { [wave.left, wave.right] = item().preview; wave.draw(); refreshRange(); };
  function validCursor() {
    if (wave.cursor < item().preview[0] || wave.cursor >= item().preview[1]) {
      $('validation').textContent = `今回の判定範囲は${seconds(item().preview[0]).toFixed(1)}秒以上、${seconds(item().preview[1]).toFixed(1)}秒未満です。`; return false;
    } return true;
  }
  function addMark(a, b) {
    if (answer().marks.length >= 300) { $('validation').textContent = 'この項目の印は300件までです。'; return; }
    if (answer().marks.some(mark => mark.start === a && mark.end === b)) return;
    answer().marks.push({ start: a, end: b }); answer().marks.sort((a, b) => a.start - b.start);
    changed(); start = null; end = null; refreshMarks();
  }
  $('add-point').onclick = () => { if (validCursor()) addMark(wave.cursor, wave.cursor); };
  $('set-start').onclick = () => { if (validCursor()) { start = wave.cursor; refreshMarks(); } };
  $('set-end').onclick = () => { if (validCursor()) { end = wave.cursor; refreshMarks(); } };
  $('add-interval').onclick = () => { if (start !== null && end > start) addMark(start, end); };
  document.querySelectorAll('[name=decision]').forEach(radio => { radio.onchange = () => { answer().decision = radio.value; changed(); }; });
  $('confidence').onchange = () => { answer().confidence = $('confidence').value; changed(); };
  $('note').oninput = () => { answer().note = $('note').value; changed(); };
  $('reviewer').value = state.reviewer;
  $('reviewer').oninput = () => { state.reviewer = $('reviewer').value; persist(); };
  $('previous').onclick = () => show(index - 1); $('next').onclick = () => show(index + 1);
  $('complete').onclick = () => {
    const error = model.finishError(answer(), item());
    if (error) { $('validation').textContent = error; return; }
    answer().complete = true; answer().updated_at = new Date().toISOString(); persist();
    const next = pack.items.findIndex((entry, n) => n > index && !state.answers[entry.id].complete);
    const remaining = next >= 0 ? next : pack.items.findIndex(entry => !state.answers[entry.id].complete);
    if (remaining >= 0) show(remaining);
    else { stop(); refreshNavigation(); notice(`${pack.items.length}件の回答が揃いました。「回答JSONを保存」で書き出し、そのJSONをCodexへ添付してください。`); }
  };
  function download(content, suffix, type) {
    const url = URL.createObjectURL(new Blob([content], { type }));
    const a = document.createElement('a'); a.href = url;
    a.download = `${pack.pack_id}-answers-${new Date().toISOString().replace(/[:.]/g, '-')}.${suffix}`;
    document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 30000);
  }
  $('export-json').onclick = () => {
    const sources = pack.items.map(({ wave, ...entry }) => entry);
    download(JSON.stringify({ ...state, exported_at: new Date().toISOString(), sources,
      annotation_scope: pack.annotation_scope ?? 'pilot_representative_marks_not_exhaustive', product_qualified: false }, null, 2), 'json', 'application/json');
  };
  $('export-tsv').onclick = () => download(model.tsv(state, pack), 'tsv', 'text/tab-separated-values;charset=utf-8');
  $('import-button').onclick = () => { stop(); $('import-file').click(); };
  $('import-file').onchange = async () => {
    const file = $('import-file').files[0]; if (!file) return;
    try {
      if (file.size > 2 * 1024 * 1024) throw new Error('回答ファイルが大きすぎます。2 MiB以下のJSONを選んでください。');
      const imported = model.validate(JSON.parse((await file.text()).replace(/^\uFEFF/, '')), pack);
      if (Object.values(state.answers).some(a => a.updated_at)
        && !confirm('読み込むと現在の回答を置き換えます。必要な回答JSONは保存済みですか？')) return;
      stop(); state = imported; storageBlocked = false; notice('回答を読み込みました。');
      $('reviewer').value = state.reviewer; $('volume').value = String(state.volume * 100); volumeLabel();
      show(pack.items.findIndex(entry => entry.id === state.active_id));
    } catch (error) { notice(`読み込めませんでした。現在の回答は変更していません。${error.message}`); }
    finally { $('import-file').value = ''; }
  };
  document.addEventListener('visibilitychange', () => { if (document.hidden) { stop(); persist(); } });
  window.addEventListener('pagehide', () => { stop(); persist(); });
  window.addEventListener('storage', event => {
    if (event.key === key) { stop(); storageBlocked = true;
      notice('別のタブで回答が更新されました。上書きを止めています。このタブの回答をJSON保存してから再読込してください。'); }
  });
  const remaining = pack.items.findIndex(entry => !state.answers[entry.id].complete);
  index = remaining >= 0 ? remaining : pack.items.findIndex(entry => entry.id === state.active_id);
  show(index);
})();
