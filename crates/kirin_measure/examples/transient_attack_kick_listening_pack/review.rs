const EXPECTED_CLIPS: usize = 45;
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
        return Err(format!("review requires exactly {EXPECTED_CLIPS} clips"));
    }
    let mut trials = String::new();
    for (index, clip) in clips.iter().enumerate() {
        if clip.clip_id != format!("K{:03}", index + 1) || clip.waveform.len() != WAVEFORM_BINS {
            return Err(format!("unexpected review clip: {}", clip.clip_id));
        }
        let waveform = waveform_svg(&clip.waveform);
        trials.push_str(&format!(
            r#"<div class="trial incomplete" data-review-id="{number}" data-clip-id="{clip_id}">
  <div class="trial-title">Trial {number}<small>{clip_id}</small></div>
  <div class="player">{waveform}<label>周辺 0–500 ms<audio controls preload="metadata" src="clips/{clip_id}.wav"></audio></label><label>判定区間 150–300 ms<audio controls preload="metadata" src="focus/{clip_id}_focus.wav"></audio></label></div>
  <fieldset><legend>150–250 ms内のキック</legend><label><input type="radio" name="choice-{number}" value="yes"> キックあり</label><label><input type="radio" name="choice-{number}" value="no"> キックなし</label><label><input type="radio" name="choice-{number}" value="uncertain"> 区別困難</label></fieldset>
  <label>確信度<select class="confidence"><option value="">選択</option><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option><option value="5">5</option></select></label>
  <label>最寄りキック位置（任意・ms）<input class="position" inputmode="decimal" placeholder="分かる場合 150–250の数値"></label>
  <label>メモ（任意）<input class="note"></label>
</div>"#,
            number = index + 1,
            clip_id = clip.clip_id,
            waveform = waveform
        ));
    }
    Ok(TEMPLATE.replace("{{TRIALS}}", &trials).into_bytes())
}

fn waveform_svg(peaks: &[u16]) -> String {
    let mut points = Vec::with_capacity(peaks.len() * 2);
    for (index, peak) in peaks.iter().enumerate() {
        let x = index as f64 * 500.0 / (peaks.len() - 1) as f64;
        let y = 40.0 - f64::from(*peak) * 34.0 / 1_000.0;
        points.push(format!("{x:.1},{y:.1}"));
    }
    for (index, peak) in peaks.iter().enumerate().rev() {
        let x = index as f64 * 500.0 / (peaks.len() - 1) as f64;
        let y = 40.0 + f64::from(*peak) * 34.0 / 1_000.0;
        points.push(format!("{x:.1},{y:.1}"));
    }
    format!(
        r#"<svg class="waveform" viewBox="0 0 500 80" preserveAspectRatio="none" aria-label="位置確認用波形"><rect x="150" y="0" width="100" height="80"/><polygon points="{}"/><line x1="200" y1="0" x2="200" y2="80"/><text x="204" y="12">MIDI kick 200 ms</text></svg>"#,
        points.join(" ")
    )
}

const TEMPLATE: &str = r##"<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kirin Hypha ATTACK kick 聴取確認</title>
<style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;background:#11161c;color:#d8dfe6}*{box-sizing:border-box}body{margin:0;padding:24px;max-width:1560px}h1{font-size:24px;margin:0 0 8px}.lead,.rules{line-height:1.65;color:#afbac4}.rules{background:#182029;border:1px solid #3d4a57;border-radius:8px;padding:14px}.chain{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px;margin:16px 0}.chain label,.trial>label{display:flex;flex-direction:column;gap:5px}.toolbar{position:sticky;top:0;z-index:3;background:#11161cf2;border-bottom:1px solid #34404c;padding:12px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}.progress{font-weight:700}.warning{color:#e6bd73}button{background:#263746;color:#e2e8ee;border:1px solid #52677a;border-radius:6px;padding:8px 11px;font-weight:700}button:disabled{opacity:.45}.study{margin:24px 0;padding:18px;border:1px solid #34404c;border-radius:9px;background:#141b22}.trial{padding:14px 8px;border-top:1px solid #2b3640;display:grid;grid-template-columns:96px minmax(400px,560px) 330px 120px 190px minmax(160px,1fr);gap:12px;align-items:center}.trial.incomplete{background:#241f1a}.trial-title{font-weight:700}.trial-title small{display:block;margin-top:4px;color:#7f8d9a;font-weight:400}.player{display:grid;grid-template-columns:1fr 1fr;gap:4px 8px}.player label{font-size:12px;color:#9aa7b3}.player audio{display:block;width:100%;height:38px;margin-top:2px}.waveform{grid-column:1/-1;width:100%;height:80px;background:#0e1419;border:1px solid #34404c;border-radius:5px}.waveform rect{fill:#5d4c214d}.waveform polygon{fill:#607d92aa}.waveform line{stroke:#e6bd73;stroke-width:2}.waveform text{fill:#e6bd73;font-size:10px}fieldset{border:1px solid #465666;border-radius:6px;display:flex;gap:12px;flex-wrap:wrap}select,input{background:#121920;color:#dbe3ea;border:1px solid #465666;border-radius:5px;padding:7px}.invalid{border-color:#d87d70!important;box-shadow:0 0 0 1px #d87d70}@media(max-width:1180px){.trial{grid-template-columns:1fr}.player{max-width:720px}}
</style></head><body>
<h1>Kirin Hypha ATTACK — kick 聴取確認</h1>
<p class="lead">45個の短いclipを聴き、MIDI参照位置の近くに低いキック音が明瞭にあるかを記録します。検出結果、演奏者、診断classはこの画面に表示しません。</p>
<div class="rules"><b>判定するのはキックです。</b> たとえば最初にスネアが鳴っても、それだけでは「キックあり」にしません。スネアだけなら「キックなし」、スネアなどに混ざって区別できなければ「区別困難」です。波形の黄色線がMIDI上のkick位置200 ms、薄い帯が150–250 msです。周辺500 msで音の種類を確認し、判定区間150–300 msで聴き直してください。波形の縦方向は位置確認用にclipごとに自動拡大しています。全clipで同じ音量設定と同じ再生系を維持し、clipごとの音量調整・正規化はしないでください。</div>
<div class="chain"><label>Interface<input id="interface" placeholder="例 Anubis"></label><label>Monitor / Headphone<input id="monitor" placeholder="使用した出力"></label><label>Sample rate<input id="sample-rate" placeholder="例 44.1 kHz"></label><label>再生level<input id="playback-level" placeholder="例 monitor -20 dB"></label><label>部屋 / 場所<input id="location" placeholder="例 Studio control room"></label></div>
<div class="toolbar"><span class="progress" id="progress">0 / 45 完了</span><span class="warning" id="warning">入力状態を自動保存します</span><button id="next">次の未完了へ</button><button id="partial">現在の入力をTSVに保存</button><button id="complete" disabled>全件完了TSVを保存</button></div>
<section class="study"><h2>ATTACK kick — 45 trials</h2>{{TRIALS}}</section>
<script>
const KEY='kirin-hypha-attack-kick-audit-b558-v1';
const PARTIAL_FILE='Kirin_Hypha_ATTACK_kick_audit_B558_partial.tsv';
const COMPLETED_FILE='Kirin_Hypha_ATTACK_kick_audit_B558_completed.tsv';
const trials=[...document.querySelectorAll('.trial')];
const chainIds=['interface','monitor','sample-rate','playback-level','location'];
const progress=document.getElementById('progress'),warning=document.getElementById('warning'),complete=document.getElementById('complete');
function load(){try{return JSON.parse(localStorage.getItem(KEY)||'{}')}catch{return {}}}
function collect(){const chain=Object.fromEntries(chainIds.map(id=>[id,document.getElementById(id).value]));const rows=Object.fromEntries(trials.map(tr=>{const id=tr.dataset.reviewId;return[id,{clipId:tr.dataset.clipId,choice:tr.querySelector('input[type=radio]:checked')?.value||'',confidence:tr.querySelector('.confidence').value,position:tr.querySelector('.position').value,note:tr.querySelector('.note').value}]}));return{chain,rows}}
function validRow(value){return Boolean(value.choice&&value.confidence)}
function refresh(persist=true){const state=collect();let done=0,hasInput=Object.values(state.chain).some(Boolean);for(const tr of trials){const value=state.rows[tr.dataset.reviewId];const ok=validRow(value);tr.classList.toggle('incomplete',!ok);if(ok)done++;if(value.choice||value.confidence||value.position||value.note)hasInput=true}const chainOk=chainIds.every(id=>state.chain[id].trim());if(persist&&hasInput)localStorage.setItem(KEY,JSON.stringify(state));progress.textContent=done+' / '+trials.length+' 完了';warning.textContent=!chainOk?'再生系の5項目を入力してください':done===trials.length?'全件完了。TSVを保存できます':'未完了 '+(trials.length-done)+' 件';complete.disabled=done!==trials.length||!chainOk}
const saved=load();for(const id of chainIds){document.getElementById(id).value=saved.chain?.[id]||'';document.getElementById(id).addEventListener('input',()=>refresh(true))}for(const tr of trials){const value=saved.rows?.[tr.dataset.reviewId]||{};for(const radio of tr.querySelectorAll('input[type=radio]'))radio.checked=radio.value===value.choice;tr.querySelector('.confidence').value=value.confidence||'';tr.querySelector('.position').value=value.position||'';tr.querySelector('.note').value=value.note||'';tr.addEventListener('input',()=>refresh(true));tr.addEventListener('change',()=>refresh(true))}
for(const audio of document.querySelectorAll('audio'))audio.addEventListener('play',()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio)other.pause()});
function cell(value){const text=String(value??'');return /[\t\n\r"]/.test(text)?'"'+text.replaceAll('"','""')+'"':text}
function exportTsv(name){const state=collect();const header=['review_id','clip_id','audible_kick','confidence','nearest_kick_ms','note','interface','monitor_or_headphone','sample_rate','playback_level','room_or_location'];const lines=[header.join('\t')];for(const tr of trials){const id=tr.dataset.reviewId,value=state.rows[id]||{};lines.push([id,tr.dataset.clipId,value.choice,value.confidence,value.position,value.note,state.chain.interface,state.chain.monitor,state.chain['sample-rate'],state.chain['playback-level'],state.chain.location].map(cell).join('\t'))}const blob=new Blob(['\uFEFF'+lines.join('\n')+'\n'],{type:'text/tab-separated-values;charset=utf-8'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}
document.getElementById('next').addEventListener('click',()=>{const state=collect();trials.find(tr=>!validRow(state.rows[tr.dataset.reviewId]))?.scrollIntoView({block:'center'})});
document.getElementById('partial').addEventListener('click',()=>exportTsv(PARTIAL_FILE));complete.addEventListener('click',()=>exportTsv(COMPLETED_FILE));refresh(false);addEventListener('pageshow',()=>refresh(true));for(const delay of [100,500,1500])setTimeout(()=>refresh(true),delay);
</script></body></html>
"##;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn review_is_candidate_blind_and_complete() {
        let clips = (1..=45)
            .map(|index| ReviewClip {
                clip_id: format!("K{index:03}"),
                waveform: vec![500; WAVEFORM_BINS],
            })
            .collect::<Vec<_>>();
        let text = String::from_utf8(render_review_html(&clips).unwrap()).unwrap();
        assert_eq!(text.matches("<div class=\"trial incomplete\"").count(), 45);
        assert_eq!(text.matches("<audio controls").count(), 90);
        assert_eq!(text.matches("MIDI kick 200 ms").count(), 45);
        for secret in ["drummer4", "matched", "eligible_peak", "performance_id"] {
            assert!(!text.contains(secret));
        }
        assert!(text.contains("localStorage"));
        assert!(text.contains("全件完了TSVを保存"));
        assert!(text
            .contains("function validRow(value){return Boolean(value.choice&&value.confidence)}"));
        assert!(text.contains("最寄りキック位置（任意・ms）"));
    }

    #[test]
    fn review_rejects_wrong_count_or_order() {
        assert!(render_review_html(&[]).is_err());
        let mut clips = (1..=45)
            .map(|index| ReviewClip {
                clip_id: format!("K{index:03}"),
                waveform: vec![0; WAVEFORM_BINS],
            })
            .collect::<Vec<_>>();
        clips.swap(0, 1);
        assert!(render_review_html(&clips).is_err());
    }

    #[test]
    fn waveform_is_normalized_for_timing_only() {
        let mut samples = vec![0.0; 1_000];
        samples[500] = 0.25;
        let envelope = waveform_envelope(&samples);
        assert_eq!(envelope.len(), WAVEFORM_BINS);
        assert_eq!(envelope.iter().copied().max(), Some(1_000));
    }
}
