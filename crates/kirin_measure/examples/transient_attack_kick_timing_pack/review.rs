const EXPECTED_CLIPS: usize = 11;
const WAVEFORM_BINS: usize = 125;

pub(crate) struct ReviewClip {
    pub(crate) clip_id: String,
    pub(crate) waveform: Vec<u16>,
}

pub(crate) fn waveform_envelope(samples: &[f32]) -> Vec<u16> {
    let mut peaks = (0..WAVEFORM_BINS)
        .map(|index| {
            let start = index * samples.len() / WAVEFORM_BINS;
            let end = (index + 1) * samples.len() / WAVEFORM_BINS;
            samples[start..end]
                .iter()
                .map(|sample| sample.abs())
                .fold(0.0_f32, f32::max)
        })
        .collect::<Vec<_>>();
    let maximum = peaks.iter().copied().fold(0.0_f32, f32::max);
    if maximum > 0.0 {
        for peak in &mut peaks {
            *peak /= maximum;
        }
    }
    peaks
        .into_iter()
        .map(|peak| (peak * 1_000.0).round() as u16)
        .collect()
}

pub(crate) fn render_review_html(clips: &[ReviewClip]) -> Result<Vec<u8>, String> {
    if clips.len() != EXPECTED_CLIPS {
        return Err(format!(
            "timing review requires exactly {EXPECTED_CLIPS} clips"
        ));
    }
    let mut previous = "";
    let mut trials = String::new();
    for (index, clip) in clips.iter().enumerate() {
        if clip.clip_id.as_str() <= previous || clip.waveform.len() != WAVEFORM_BINS {
            return Err(format!("unexpected timing review clip: {}", clip.clip_id));
        }
        previous = &clip.clip_id;
        trials.push_str(&format!(
            r#"<div class="trial incomplete" data-review-id="{number}" data-clip-id="{clip_id}">
  <div class="trial-title">Trial {number}<small>{clip_id}</small></div>
  <div class="player">{waveform}<label>周辺 0–500 ms<audio controls preload="metadata" src="clips/{clip_id}.wav"></audio></label><label>集中 150–300 ms<audio controls preload="metadata" src="focus/{clip_id}_focus.wav"></audio></label></div>
  <div class="answer"><b>低いkickの開始位置</b><p class="hint">波形をクリック（青線）</p><label><input type="radio" name="mode-{number}" value="position"> 位置を記録</label><label><input type="radio" name="mode-{number}" value="uncertain"> 判別困難</label><output class="position-label">未選択</output><input class="position" type="hidden"></div>
  <label>確信度<select class="confidence"><option value="">選択</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option></select></label>
  <label>メモ（任意）<input class="note"></label>
</div>"#,
            number = index + 1,
            clip_id = clip.clip_id,
            waveform = waveform_svg(&clip.waveform)
        ));
    }
    Ok(TEMPLATE.replace("{{TRIALS}}", &trials).into_bytes())
}

fn waveform_svg(peaks: &[u16]) -> String {
    let mut points = Vec::with_capacity(peaks.len() * 2);
    for (index, peak) in peaks.iter().enumerate() {
        let x = index as f64 * 500.0 / (peaks.len() - 1) as f64;
        let y = 44.0 - f64::from(*peak) * 37.0 / 1_000.0;
        points.push(format!("{x:.1},{y:.1}"));
    }
    for (index, peak) in peaks.iter().enumerate().rev() {
        let x = index as f64 * 500.0 / (peaks.len() - 1) as f64;
        let y = 44.0 + f64::from(*peak) * 37.0 / 1_000.0;
        points.push(format!("{x:.1},{y:.1}"));
    }
    format!(
        r#"<svg class="waveform" viewBox="0 0 500 88" preserveAspectRatio="none" role="button" tabindex="0" aria-label="低いkickの開始位置をクリック"><rect x="100" y="0" width="200" height="88"/><polygon points="{}"/><line class="midi" x1="200" y1="0" x2="200" y2="88"/><line class="picked" x1="0" y1="0" x2="0" y2="88"/><text x="204" y="13">MIDI 200 ms</text><text x="104" y="83">クリック範囲 100–300 ms</text></svg>"#,
        points.join(" ")
    )
}

const TEMPLATE: &str = r##"<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kirin Hypha ATTACK kick 開始位置</title>
<style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;background:#11161c;color:#d8dfe6}*{box-sizing:border-box}body{margin:0;padding:24px;max-width:1500px}h1{font-size:24px;margin:0 0 8px}.lead{line-height:1.65;color:#b4c0ca}.rules{background:#182029;border:1px solid #4b5e70;border-radius:9px;padding:15px;line-height:1.7}.rules strong{color:#f0c979}.chain{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:16px 0}.chain label,.trial>label{display:flex;flex-direction:column;gap:5px}.toolbar{position:sticky;top:0;z-index:3;background:#11161cf2;border-bottom:1px solid #34404c;padding:12px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}.progress{font-weight:700}.warning{color:#e6bd73}button{background:#263746;color:#e2e8ee;border:1px solid #52677a;border-radius:6px;padding:8px 11px;font-weight:700}button:disabled{opacity:.45}.study{margin:24px 0;padding:18px;border:1px solid #34404c;border-radius:9px;background:#141b22}.trial{padding:16px 8px;border-top:1px solid #2b3640;display:grid;grid-template-columns:88px minmax(450px,650px) 250px 110px minmax(150px,1fr);gap:14px;align-items:center}.trial.incomplete{background:#241f1a}.trial-title{font-weight:700}.trial-title small{display:block;margin-top:4px;color:#7f8d9a;font-weight:400}.player{display:grid;grid-template-columns:1fr 1fr;gap:4px 8px}.player label{font-size:12px;color:#9aa7b3}.player audio{display:block;width:100%;height:38px;margin-top:2px}.waveform{grid-column:1/-1;width:100%;height:90px;background:#0e1419;border:1px solid #52677a;border-radius:5px;cursor:crosshair}.waveform:focus{outline:2px solid #74b9e7}.waveform rect{fill:#5d4c2140}.waveform polygon{fill:#607d92aa}.waveform .midi{stroke:#e6bd73;stroke-width:2}.waveform .picked{stroke:#5dc2ff;stroke-width:3;visibility:hidden}.waveform text{fill:#e6bd73;font-size:10px}.answer{border:1px solid #465666;border-radius:7px;padding:10px;display:flex;gap:8px;flex-wrap:wrap}.answer b,.answer .hint,.position-label{width:100%}.hint{font-size:12px;color:#9fb0bd;margin:0}.position-label{color:#68c7ff;font-weight:700}select,input{background:#121920;color:#dbe3ea;border:1px solid #465666;border-radius:5px;padding:7px}@media(max-width:1100px){.trial{grid-template-columns:1fr}.player{max-width:760px}}
</style></head><body>
<h1>Kirin Hypha ATTACK — kick開始位置</h1>
<p class="lead">11個だけです。キックの有無はすでに確認済みなので、今回は開始時刻だけを記録します。</p>
<div class="rules"><strong>判断するのは1つだけ：</strong>低いキック音が<strong>始まった瞬間</strong>を波形上でクリックしてください。先に鳴るスネアやハットは選びません。黄色線はMIDI位置200 ms、クリックした位置は青線です。周辺音源で音を見分け、集中音源で位置を詰めます。どうしても開始点を決められない場合だけ「判別困難」を選びます。全clipで同じ再生音量を保ってください。</div>
<div class="chain"><label>Interface<input id="interface" placeholder="例 Anubis"></label><label>Monitor / Headphone<input id="monitor" placeholder="使用した出力"></label><label>Sample rate<input id="sample-rate" placeholder="例 96 kHz"></label><label>再生level<input id="playback-level" placeholder="例 -20 dB"></label><label>部屋 / 場所<input id="location" placeholder="例 自室"></label></div>
<div class="toolbar"><span class="progress" id="progress">0 / 11 完了</span><span class="warning" id="warning">入力状態を自動保存します</span><button id="next">次の未完了へ</button><button id="partial">現在の入力をTSVに保存</button><button id="complete" disabled>全件完了TSVを保存</button></div>
<section class="study"><h2>ATTACK kick timing — 11 trials</h2>{{TRIALS}}</section>
<script>
const KEY='kirin-hypha-attack-kick-timing-b562-v1',PARTIAL='Kirin_Hypha_ATTACK_kick_timing_B562_partial.tsv',DONE='Kirin_Hypha_ATTACK_kick_timing_B562_completed.tsv';
const trials=[...document.querySelectorAll('.trial')],chainIds=['interface','monitor','sample-rate','playback-level','location'];
const progress=document.getElementById('progress'),warning=document.getElementById('warning'),complete=document.getElementById('complete');
function load(){try{return JSON.parse(localStorage.getItem(KEY)||'{}')}catch{return {}}}
function collect(){const chain=Object.fromEntries(chainIds.map(id=>[id,document.getElementById(id).value]));const rows=Object.fromEntries(trials.map(tr=>{const id=tr.dataset.reviewId;return[id,{clipId:tr.dataset.clipId,mode:tr.querySelector('input[type=radio]:checked')?.value||'',position:tr.querySelector('.position').value,confidence:tr.querySelector('.confidence').value,note:tr.querySelector('.note').value}]}));return{chain,rows}}
function validRow(v){return Boolean(v.confidence&&((v.mode==='position'&&v.position)||v.mode==='uncertain'))}
function marker(tr){const value=Number(tr.querySelector('.position').value),line=tr.querySelector('.picked'),label=tr.querySelector('.position-label');if(Number.isFinite(value)&&value>=100&&value<=300){line.setAttribute('x1',value);line.setAttribute('x2',value);line.style.visibility='visible';label.textContent=value.toFixed(1)+' ms'}else{line.style.visibility='hidden';label.textContent='未選択'}}
function refresh(persist=true){const state=collect();let done=0,hasInput=Object.values(state.chain).some(Boolean);for(const tr of trials){const value=state.rows[tr.dataset.reviewId],ok=validRow(value);tr.classList.toggle('incomplete',!ok);marker(tr);if(ok)done++;if(value.mode||value.position||value.confidence||value.note)hasInput=true}const chainOk=chainIds.every(id=>state.chain[id].trim());if(persist&&hasInput)localStorage.setItem(KEY,JSON.stringify(state));progress.textContent=done+' / '+trials.length+' 完了';warning.textContent=!chainOk?'再生系の5項目を入力してください':done===trials.length?'全件完了。TSVを保存できます':'未完了 '+(trials.length-done)+' 件';complete.disabled=done!==trials.length||!chainOk}
function choosePosition(tr,ms){tr.querySelector('input[value=position]').checked=true;tr.querySelector('.position').value=ms.toFixed(1);refresh(true)}
const saved=load();for(const id of chainIds){document.getElementById(id).value=saved.chain?.[id]||'';document.getElementById(id).addEventListener('input',()=>refresh(true))}for(const tr of trials){const value=saved.rows?.[tr.dataset.reviewId]||{};for(const radio of tr.querySelectorAll('input[type=radio]'))radio.checked=radio.value===value.mode;tr.querySelector('.position').value=value.position||'';tr.querySelector('.confidence').value=value.confidence||'';tr.querySelector('.note').value=value.note||'';tr.addEventListener('input',()=>refresh(true));tr.addEventListener('change',()=>refresh(true));const svg=tr.querySelector('.waveform');svg.addEventListener('click',event=>{const rect=svg.getBoundingClientRect(),ms=(event.clientX-rect.left)*500/rect.width;if(ms>=100&&ms<=300)choosePosition(tr,ms);else warning.textContent='100–300 msの薄い範囲をクリックしてください'});svg.addEventListener('keydown',event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();choosePosition(tr,200)}})}
for(const audio of document.querySelectorAll('audio'))audio.addEventListener('play',()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio)other.pause()});
function cell(value){const text=String(value??'');return /[\t\n\r"]/.test(text)?'"'+text.replaceAll('"','""')+'"':text}
function exportTsv(name){const state=collect(),header=['review_id','clip_id','kick_onset_ms','uncertain','confidence','note','interface','monitor_or_headphone','sample_rate','playback_level','room_or_location'],lines=[header.join('\t')];for(const tr of trials){const id=tr.dataset.reviewId,v=state.rows[id]||{};lines.push([id,tr.dataset.clipId,v.mode==='position'?v.position:'',v.mode==='uncertain'?'yes':'no',v.confidence,v.note,state.chain.interface,state.chain.monitor,state.chain['sample-rate'],state.chain['playback-level'],state.chain.location].map(cell).join('\t'))}const blob=new Blob(['\uFEFF'+lines.join('\n')+'\n'],{type:'text/tab-separated-values;charset=utf-8'}),a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}
document.getElementById('next').addEventListener('click',()=>{const s=collect();trials.find(tr=>!validRow(s.rows[tr.dataset.reviewId]))?.scrollIntoView({block:'center'})});document.getElementById('partial').addEventListener('click',()=>exportTsv(PARTIAL));complete.addEventListener('click',()=>exportTsv(DONE));refresh(false);addEventListener('pageshow',()=>refresh(true));for(const delay of [100,500])setTimeout(()=>refresh(true),delay);
</script></body></html>
"##;

#[cfg(test)]
mod tests {
    use super::*;

    fn clips() -> Vec<ReviewClip> {
        [
            "K005", "K006", "K010", "K015", "K025", "K032", "K035", "K037", "K040", "K041", "K043",
        ]
        .into_iter()
        .map(|clip_id| ReviewClip {
            clip_id: clip_id.to_string(),
            waveform: vec![500; WAVEFORM_BINS],
        })
        .collect()
    }

    #[test]
    fn timing_review_is_clear_candidate_blind_and_clickable() {
        let text = String::from_utf8(render_review_html(&clips()).unwrap()).unwrap();
        assert_eq!(text.matches("<div class=\"trial incomplete\"").count(), 11);
        assert_eq!(text.matches("<audio controls").count(), 22);
        assert!(text.contains("低いキック音が<strong>始まった瞬間</strong>"));
        assert!(text.contains("先に鳴るスネアやハットは選びません"));
        assert!(text.contains("svg.addEventListener('click'"));
        for secret in ["matched", "eligible_peak", "performance_id", "drummer4"] {
            assert!(!text.contains(secret));
        }
    }

    #[test]
    fn timing_review_rejects_wrong_count_order_or_waveform() {
        assert!(render_review_html(&[]).is_err());
        let mut invalid = clips();
        invalid.swap(0, 1);
        assert!(render_review_html(&invalid).is_err());
        let mut invalid = clips();
        invalid[0].waveform.pop();
        assert!(render_review_html(&invalid).is_err());
    }

    #[test]
    fn waveform_is_normalized_only_for_visual_timing() {
        let mut samples = vec![0.0; 1_000];
        samples[500] = 0.25;
        let envelope = waveform_envelope(&samples);
        assert_eq!(envelope.len(), WAVEFORM_BINS);
        assert_eq!(envelope.iter().copied().max(), Some(1_000));
    }
}
