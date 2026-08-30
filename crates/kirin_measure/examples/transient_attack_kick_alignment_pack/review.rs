const EXPECTED_CLIP_IDS: [&str; 11] = [
    "K005", "K006", "K010", "K015", "K025", "K032", "K035", "K037", "K040", "K041", "K043",
];

pub(crate) struct ReviewClip {
    pub(crate) clip_id: String,
}

pub(crate) fn render_review_html(clips: &[ReviewClip]) -> Result<Vec<u8>, String> {
    let actual = clips
        .iter()
        .map(|clip| clip.clip_id.as_str())
        .collect::<Vec<_>>();
    if actual != EXPECTED_CLIP_IDS {
        return Err("alignment review clip set/order mismatch".to_string());
    }
    let mut trials = String::new();
    for (index, clip) in clips.iter().enumerate() {
        trials.push_str(&format!(
            r#"<div class="trial incomplete" data-review-id="{number}" data-clip-id="{clip_id}">
  <div class="trial-title">Trial {number}<small>{clip_id}</small></div>
  <div class="players"><label>周辺 0–500 ms<audio controls preload="metadata" src="clips/{clip_id}.wav"></audio></label><label>集中 150–300 ms<audio controls preload="metadata" src="focus/{clip_id}_focus.wav"></audio></label><label class="guided">ガイド付き 0–500 ms<audio class="guide-audio" controls preload="metadata" src="guide/{clip_id}_200.wav"></audio></label></div>
  <div class="align"><b>高いガイド音を低いkickの開始へ合わせる</b><div class="range-row"><button class="minus" type="button">−10 ms</button><input class="preview" type="range" min="100" max="300" step="10" value="200" aria-label="ガイド位置"><button class="plus" type="button">+10 ms</button></div><output class="preview-label">ガイド 200 ms</output><button class="confirm" type="button">この位置で決定</button><label><input class="uncertain" type="checkbox"> 判別困難</label><input class="chosen" type="hidden"><output class="decision">未決定</output></div>
  <label>確信度<select class="confidence"><option value="">選択</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option></select></label>
  <label>メモ（任意）<input class="note"></label>
</div>"#,
            number = index + 1,
            clip_id = clip.clip_id,
        ));
    }
    Ok(TEMPLATE.replace("{{TRIALS}}", &trials).into_bytes())
}

const TEMPLATE: &str = r##"<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kirin Hypha ATTACK kick 聴覚アラインメント</title>
<style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;background:#11161c;color:#d8dfe6}*{box-sizing:border-box}body{margin:0;padding:24px;max-width:1540px}h1{font-size:24px;margin:0 0 8px}.lead{line-height:1.65;color:#b4c0ca}.rules{background:#182029;border:1px solid #4b5e70;border-radius:9px;padding:15px;line-height:1.75}.rules strong{color:#f0c979}.chain{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:16px 0}.chain label,.trial>label{display:flex;flex-direction:column;gap:5px}.toolbar{position:sticky;top:0;z-index:3;background:#11161cf2;border-bottom:1px solid #34404c;padding:12px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}.progress{font-weight:700}.warning{color:#e6bd73}button{background:#263746;color:#e2e8ee;border:1px solid #52677a;border-radius:6px;padding:8px 11px;font-weight:700}button:disabled{opacity:.45}.study{margin:24px 0;padding:18px;border:1px solid #34404c;border-radius:9px;background:#141b22}.trial{padding:16px 8px;border-top:1px solid #2b3640;display:grid;grid-template-columns:88px minmax(480px,650px) minmax(360px,480px) 110px minmax(150px,1fr);gap:14px;align-items:center}.trial.incomplete{background:#241f1a}.trial-title{font-weight:700}.trial-title small{display:block;margin-top:4px;color:#7f8d9a;font-weight:400}.players{display:grid;grid-template-columns:1fr 1fr;gap:7px 10px}.players label{font-size:12px;color:#9aa7b3}.players .guided{grid-column:1/-1;color:#d9c17e}.players audio{display:block;width:100%;height:38px;margin-top:2px}.align{border:1px solid #465666;border-radius:7px;padding:10px;display:grid;gap:8px}.range-row{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:8px}.preview{min-width:150px}.preview-label,.decision{color:#68c7ff;font-weight:700}.confirm{justify-self:start}.align label{font-size:13px}select,input{background:#121920;color:#dbe3ea;border:1px solid #465666;border-radius:5px;padding:7px}@media(max-width:1180px){.trial{grid-template-columns:1fr}.players{max-width:760px}.align{max-width:760px}}
</style></head><body>
<h1>Kirin Hypha ATTACK — kick開始の聴覚アラインメント</h1>
<p class="lead">11個だけです。波形から位置を推測せず、耳で聞こえた開始位置を記録します。</p>
<div class="rules"><strong>判断するのは1つだけ：</strong>「ガイド付き」を再生し、<strong>左側から聞こえる短い高音</strong>が、低いkickの<strong>始まった瞬間</strong>と重なるように位置を動かしてください。先に鳴るスネアやハットには合わせません。周辺と集中でkickを確認し、−10 / +10 msで詰めて「この位置で決定」を押します。聞き分けられない場合だけ「判別困難」を選びます。全clipで再生音量を変えないでください。</div>
<div class="chain"><label>Interface<input id="interface" placeholder="例 Anubis"></label><label>Monitor / Headphone<input id="monitor" placeholder="使用した出力"></label><label>Sample rate<input id="sample-rate" placeholder="例 96 kHz"></label><label>再生level<input id="playback-level" placeholder="例 -20 dB"></label><label>部屋 / 場所<input id="location" placeholder="例 自室"></label></div>
<div class="toolbar"><span class="progress" id="progress">0 / 11 完了</span><span class="warning" id="warning">入力状態を自動保存します</span><button id="next">次の未完了へ</button><button id="partial">現在の入力をTSVに保存</button><button id="complete" disabled>全件完了TSVを保存</button></div>
<section class="study"><h2>ATTACK kick auditory alignment — 11 trials</h2>{{TRIALS}}</section>
<script>
const KEY='kirin-hypha-attack-kick-alignment-b563-v1',PARTIAL='Kirin_Hypha_ATTACK_kick_alignment_B563_partial.tsv',DONE='Kirin_Hypha_ATTACK_kick_alignment_B563_completed.tsv';
const trials=[...document.querySelectorAll('.trial')],chainIds=['interface','monitor','sample-rate','playback-level','location'];
const progress=document.getElementById('progress'),warning=document.getElementById('warning'),complete=document.getElementById('complete');
function load(){try{return JSON.parse(localStorage.getItem(KEY)||'{}')}catch{return {}}}
function collect(){const chain=Object.fromEntries(chainIds.map(id=>[id,document.getElementById(id).value]));const rows=Object.fromEntries(trials.map(tr=>{const id=tr.dataset.reviewId;return[id,{clipId:tr.dataset.clipId,preview:tr.querySelector('.preview').value,chosen:tr.querySelector('.chosen').value,uncertain:tr.querySelector('.uncertain').checked,confidence:tr.querySelector('.confidence').value,note:tr.querySelector('.note').value}]}));return{chain,rows}}
function validRow(v){return Boolean(v.confidence&&(v.uncertain||v.chosen))}
function setPreview(tr,value,clearDecision){const ms=Math.max(100,Math.min(300,Math.round(Number(value)/10)*10));tr.querySelector('.preview').value=ms;tr.querySelector('.preview-label').textContent='ガイド '+ms+' ms';const audio=tr.querySelector('.guide-audio');audio.src='guide/'+tr.dataset.clipId+'_'+ms+'.wav';audio.load();if(clearDecision){tr.querySelector('.chosen').value='';tr.querySelector('.uncertain').checked=false;tr.querySelector('.decision').textContent='未決定'}refresh(true)}
function refresh(persist=true){const state=collect();let done=0,hasInput=Object.values(state.chain).some(Boolean);for(const tr of trials){const value=state.rows[tr.dataset.reviewId],ok=validRow(value);tr.classList.toggle('incomplete',!ok);const decision=tr.querySelector('.decision');if(value.uncertain)decision.textContent='判別困難';else if(value.chosen)decision.textContent='決定 '+value.chosen+' ms';else decision.textContent='未決定';if(ok)done++;if(value.chosen||value.uncertain||value.confidence||value.note)hasInput=true}const chainOk=chainIds.every(id=>state.chain[id].trim());if(persist&&hasInput)localStorage.setItem(KEY,JSON.stringify(state));progress.textContent=done+' / '+trials.length+' 完了';warning.textContent=!chainOk?'再生系の5項目を入力してください':done===trials.length?'全件完了。TSVを保存できます':'未完了 '+(trials.length-done)+' 件';complete.disabled=done!==trials.length||!chainOk}
const saved=load();for(const id of chainIds){document.getElementById(id).value=saved.chain?.[id]||'';document.getElementById(id).addEventListener('input',()=>refresh(true))}for(const tr of trials){const value=saved.rows?.[tr.dataset.reviewId]||{};tr.querySelector('.confidence').value=value.confidence||'';tr.querySelector('.note').value=value.note||'';tr.querySelector('.chosen').value=value.chosen||'';tr.querySelector('.uncertain').checked=Boolean(value.uncertain);setPreview(tr,value.preview||value.chosen||200,false);tr.querySelector('.preview').addEventListener('input',event=>setPreview(tr,event.target.value,true));tr.querySelector('.minus').addEventListener('click',()=>setPreview(tr,Number(tr.querySelector('.preview').value)-10,true));tr.querySelector('.plus').addEventListener('click',()=>setPreview(tr,Number(tr.querySelector('.preview').value)+10,true));tr.querySelector('.confirm').addEventListener('click',()=>{tr.querySelector('.chosen').value=tr.querySelector('.preview').value;tr.querySelector('.uncertain').checked=false;refresh(true)});tr.querySelector('.uncertain').addEventListener('change',event=>{if(event.target.checked)tr.querySelector('.chosen').value='';refresh(true)});tr.querySelector('.confidence').addEventListener('change',()=>refresh(true));tr.querySelector('.note').addEventListener('input',()=>refresh(true))}
for(const audio of document.querySelectorAll('audio'))audio.addEventListener('play',()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio)other.pause()});
function cell(value){const text=String(value??'');return /[\t\n\r"]/.test(text)?'"'+text.replaceAll('"','""')+'"':text}
function exportTsv(name){const state=collect(),header=['review_id','clip_id','kick_onset_ms','uncertain','confidence','note','interface','monitor_or_headphone','sample_rate','playback_level','room_or_location'],lines=[header.join('\t')];for(const tr of trials){const id=tr.dataset.reviewId,v=state.rows[id]||{};lines.push([id,tr.dataset.clipId,v.uncertain?'':v.chosen,v.uncertain?'yes':'no',v.confidence,v.note,state.chain.interface,state.chain.monitor,state.chain['sample-rate'],state.chain['playback-level'],state.chain.location].map(cell).join('\t'))}const blob=new Blob(['\uFEFF'+lines.join('\n')+'\n'],{type:'text/tab-separated-values;charset=utf-8'}),a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}
document.getElementById('next').addEventListener('click',()=>{const s=collect();trials.find(tr=>!validRow(s.rows[tr.dataset.reviewId]))?.scrollIntoView({block:'center'})});document.getElementById('partial').addEventListener('click',()=>exportTsv(PARTIAL));complete.addEventListener('click',()=>exportTsv(DONE));refresh(false);addEventListener('pageshow',()=>refresh(true));for(const delay of [100,500])setTimeout(()=>refresh(true),delay);
</script></body></html>
"##;

#[cfg(test)]
mod tests {
    use super::*;

    fn clips() -> Vec<ReviewClip> {
        EXPECTED_CLIP_IDS
            .into_iter()
            .map(|clip_id| ReviewClip {
                clip_id: clip_id.to_string(),
            })
            .collect()
    }

    #[test]
    fn review_is_auditory_candidate_blind_and_complete() {
        let text = String::from_utf8(render_review_html(&clips()).unwrap()).unwrap();
        assert_eq!(text.matches("<div class=\"trial incomplete\"").count(), 11);
        assert_eq!(text.matches("<audio").count(), 33);
        assert!(text.contains("左側から聞こえる短い高音"));
        assert!(text.contains("低いkickの<strong>始まった瞬間</strong>"));
        assert!(text.contains("先に鳴るスネアやハットには合わせません"));
        assert!(text.contains("audio.src='guide/'"));
        assert!(text.contains("この位置で決定"));
        for secret in ["matched", "eligible_peak", "performance_id", "drummer4"] {
            assert!(!text.contains(secret));
        }
    }

    #[test]
    fn review_rejects_wrong_count_or_order() {
        assert!(render_review_html(&[]).is_err());
        let mut invalid = clips();
        invalid.swap(0, 1);
        assert!(render_review_html(&invalid).is_err());
    }
}
