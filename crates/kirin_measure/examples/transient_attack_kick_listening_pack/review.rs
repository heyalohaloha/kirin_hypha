const EXPECTED_CLIPS: usize = 45;

pub(crate) fn render_review_html(clip_ids: &[String]) -> Result<Vec<u8>, String> {
    if clip_ids.len() != EXPECTED_CLIPS {
        return Err(format!("review requires exactly {EXPECTED_CLIPS} clips"));
    }
    let mut trials = String::new();
    for (index, clip_id) in clip_ids.iter().enumerate() {
        if clip_id != &format!("K{:03}", index + 1) {
            return Err(format!("unexpected review clip ID: {clip_id}"));
        }
        trials.push_str(&format!(
            r#"<div class="trial incomplete" data-review-id="{number}" data-clip-id="{clip_id}">
  <div class="trial-title">Trial {number}<small>{clip_id}</small></div>
  <div class="player"><audio controls preload="metadata" src="clips/{clip_id}.wav"></audio><span class="time-guide">150 ms ── <b>200 ms</b> ── 250 ms</span></div>
  <fieldset><legend>明瞭なattack</legend><label><input type="radio" name="choice-{number}" value="yes"> ある</label><label><input type="radio" name="choice-{number}" value="no"> ない</label><label><input type="radio" name="choice-{number}" value="uncertain"> 判断困難</label></fieldset>
  <label>確信度<select class="confidence"><option value="">選択</option><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option><option value="5">5</option></select></label>
  <label>最寄りattack位置（ms）<input class="position" inputmode="decimal" placeholder="ある場合 150–250"></label>
  <label>メモ（任意）<input class="note"></label>
</div>"#,
            number = index + 1,
            clip_id = clip_id
        ));
    }
    Ok(TEMPLATE.replace("{{TRIALS}}", &trials).into_bytes())
}

const TEMPLATE: &str = r##"<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kirin Hypha ATTACK kick 聴取確認</title>
<style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;background:#11161c;color:#d8dfe6}*{box-sizing:border-box}body{margin:0;padding:24px;max-width:1560px}h1{font-size:24px;margin:0 0 8px}.lead,.rules{line-height:1.65;color:#afbac4}.rules{background:#182029;border:1px solid #3d4a57;border-radius:8px;padding:14px}.chain{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px;margin:16px 0}.chain label,.trial>label{display:flex;flex-direction:column;gap:5px}.toolbar{position:sticky;top:0;z-index:3;background:#11161cf2;border-bottom:1px solid #34404c;padding:12px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}.progress{font-weight:700}.warning{color:#e6bd73}button{background:#263746;color:#e2e8ee;border:1px solid #52677a;border-radius:6px;padding:8px 11px;font-weight:700}button:disabled{opacity:.45}.study{margin:24px 0;padding:18px;border:1px solid #34404c;border-radius:9px;background:#141b22}.trial{padding:14px 8px;border-top:1px solid #2b3640;display:grid;grid-template-columns:96px minmax(320px,520px) 300px 120px 190px minmax(160px,1fr);gap:12px;align-items:center}.trial.incomplete{background:#241f1a}.trial-title{font-weight:700}.trial-title small{display:block;margin-top:4px;color:#7f8d9a;font-weight:400}.player audio{display:block;width:100%;height:42px}.time-guide{display:block;text-align:center;color:#8f9ba7;font-size:12px;margin-top:4px}.time-guide b{color:#e6bd73}fieldset{border:1px solid #465666;border-radius:6px;display:flex;gap:12px;flex-wrap:wrap}select,input{background:#121920;color:#dbe3ea;border:1px solid #465666;border-radius:5px;padding:7px}.invalid{border-color:#d87d70!important;box-shadow:0 0 0 1px #d87d70}@media(max-width:1180px){.trial{grid-template-columns:1fr}.player{max-width:640px}}
</style></head><body>
<h1>Kirin Hypha ATTACK — kick 聴取確認</h1>
<p class="lead">45個の短いclipを聴き、MIDI参照位置の近くに明瞭なattackがあるかを記録します。検出結果、演奏者、診断classは完成TSVの保存まで表示しません。</p>
<div class="rules">全clipで同じ音量設定と同じ再生系を維持してください。各clipは500 ms、中央のMIDI参照位置は200 msです。150–250 ms内で最も近い明瞭なattackを判定します。繰り返し再生は可、clipごとの音量調整・正規化は不可です。</div>
<div class="chain"><label>Interface<input id="interface" placeholder="例 Anubis"></label><label>Monitor / Headphone<input id="monitor" placeholder="使用した出力"></label><label>Sample rate<input id="sample-rate" placeholder="例 44.1 kHz"></label><label>再生level<input id="playback-level" placeholder="例 monitor -20 dB"></label><label>部屋 / 場所<input id="location" placeholder="例 Studio control room"></label></div>
<div class="toolbar"><span class="progress" id="progress">0 / 45 完了</span><span class="warning" id="warning">入力状態を自動保存します</span><button id="next">次の未完了へ</button><button id="partial">現在の入力をTSVに保存</button><button id="complete" disabled>全件完了TSVを保存</button></div>
<section class="study"><h2>ATTACK kick — 45 trials</h2>{{TRIALS}}</section>
<script>
const KEY='kirin-hypha-attack-kick-audit-b556-v1';
const PARTIAL_FILE='Kirin_Hypha_ATTACK_kick_audit_B556_partial.tsv';
const COMPLETED_FILE='Kirin_Hypha_ATTACK_kick_audit_B556_completed.tsv';
const trials=[...document.querySelectorAll('.trial')];
const chainIds=['interface','monitor','sample-rate','playback-level','location'];
const progress=document.getElementById('progress'),warning=document.getElementById('warning'),complete=document.getElementById('complete');
function load(){try{return JSON.parse(localStorage.getItem(KEY)||'{}')}catch{return {}}}
function collect(){const chain=Object.fromEntries(chainIds.map(id=>[id,document.getElementById(id).value]));const rows=Object.fromEntries(trials.map(tr=>{const id=tr.dataset.reviewId;return[id,{clipId:tr.dataset.clipId,choice:tr.querySelector('input[type=radio]:checked')?.value||'',confidence:tr.querySelector('.confidence').value,position:tr.querySelector('.position').value,note:tr.querySelector('.note').value}]}));return{chain,rows}}
function validPosition(value){if(value.choice!=='yes')return true;const n=Number(value.position);return value.position.trim()!==''&&Number.isFinite(n)&&n>=150&&n<=250}
function validRow(value){return Boolean(value.choice&&value.confidence&&validPosition(value))}
function refresh(persist=true){const state=collect();let done=0,hasInput=Object.values(state.chain).some(Boolean);for(const tr of trials){const value=state.rows[tr.dataset.reviewId];const ok=validRow(value);tr.classList.toggle('incomplete',!ok);tr.querySelector('.position').classList.toggle('invalid',value.choice==='yes'&&!validPosition(value));if(ok)done++;if(value.choice||value.confidence||value.position||value.note)hasInput=true}const chainOk=chainIds.every(id=>state.chain[id].trim());if(persist&&hasInput)localStorage.setItem(KEY,JSON.stringify(state));progress.textContent=done+' / '+trials.length+' 完了';warning.textContent=!chainOk?'再生系の5項目を入力してください':done===trials.length?'全件完了。TSVを保存できます':'未完了 '+(trials.length-done)+' 件';complete.disabled=done!==trials.length||!chainOk}
const saved=load();for(const id of chainIds){document.getElementById(id).value=saved.chain?.[id]||'';document.getElementById(id).addEventListener('input',()=>refresh(true))}for(const tr of trials){const value=saved.rows?.[tr.dataset.reviewId]||{};for(const radio of tr.querySelectorAll('input[type=radio]'))radio.checked=radio.value===value.choice;tr.querySelector('.confidence').value=value.confidence||'';tr.querySelector('.position').value=value.position||'';tr.querySelector('.note').value=value.note||'';tr.addEventListener('input',()=>refresh(true));tr.addEventListener('change',()=>refresh(true))}
for(const audio of document.querySelectorAll('audio'))audio.addEventListener('play',()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio)other.pause()});
function cell(value){const text=String(value??'');return /[\t\n\r"]/.test(text)?'"'+text.replaceAll('"','""')+'"':text}
function exportTsv(name){const state=collect();const header=['review_id','clip_id','audible_attack','confidence','nearest_attack_ms','note','interface','monitor_or_headphone','sample_rate','playback_level','room_or_location'];const lines=[header.join('\t')];for(const tr of trials){const id=tr.dataset.reviewId,value=state.rows[id]||{};lines.push([id,tr.dataset.clipId,value.choice,value.confidence,value.position,value.note,state.chain.interface,state.chain.monitor,state.chain['sample-rate'],state.chain['playback-level'],state.chain.location].map(cell).join('\t'))}const blob=new Blob(['\uFEFF'+lines.join('\n')+'\n'],{type:'text/tab-separated-values;charset=utf-8'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}
document.getElementById('next').addEventListener('click',()=>{const state=collect();trials.find(tr=>!validRow(state.rows[tr.dataset.reviewId]))?.scrollIntoView({block:'center'})});
document.getElementById('partial').addEventListener('click',()=>exportTsv(PARTIAL_FILE));complete.addEventListener('click',()=>exportTsv(COMPLETED_FILE));refresh(false);addEventListener('pageshow',()=>refresh(true));for(const delay of [100,500,1500])setTimeout(()=>refresh(true),delay);
</script></body></html>
"##;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn review_is_candidate_blind_and_complete() {
        let ids = (1..=45)
            .map(|index| format!("K{index:03}"))
            .collect::<Vec<_>>();
        let text = String::from_utf8(render_review_html(&ids).unwrap()).unwrap();
        assert_eq!(text.matches("<div class=\"trial incomplete\"").count(), 45);
        assert_eq!(text.matches("<audio controls").count(), 45);
        for secret in ["drummer4", "matched", "eligible_peak", "performance_id"] {
            assert!(!text.contains(secret));
        }
        assert!(text.contains("localStorage"));
        assert!(text.contains("全件完了TSVを保存"));
    }

    #[test]
    fn review_rejects_wrong_count_or_order() {
        assert!(render_review_html(&[]).is_err());
        let mut ids = (1..=45)
            .map(|index| format!("K{index:03}"))
            .collect::<Vec<_>>();
        ids.swap(0, 1);
        assert!(render_review_html(&ids).is_err());
    }
}
