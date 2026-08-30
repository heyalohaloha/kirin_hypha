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
        return Err("presence review clip set/order mismatch".to_string());
    }
    let mut trials = String::new();
    for (index, clip) in clips.iter().enumerate() {
        trials.push_str(&format!(
            r#"<div class="trial incomplete" data-review-id="{number}" data-clip-id="{clip_id}"><div class="trial-title">{number}<small>{clip_id}</small></div><button class="play" type="button">▶ 150–300 msを再生</button><audio preload="auto" src="focus/{clip_id}_focus.wav"></audio><fieldset><legend>この区間で低いキックが鳴っていますか？</legend><label><input type="radio" name="answer-{number}" value="yes"> キックあり</label><label><input type="radio" name="answer-{number}" value="no"> キックなし</label><label><input type="radio" name="answer-{number}" value="uncertain"> わからない</label></fieldset></div>"#,
            number = index + 1,
            clip_id = clip.clip_id,
        ));
    }
    Ok(TEMPLATE.replace("{{TRIALS}}", &trials).into_bytes())
}

const TEMPLATE: &str = r##"<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Kirin Hypha ATTACK kick確認</title><style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;background:#11161c;color:#dce3e9}*{box-sizing:border-box}body{margin:0;padding:24px;max-width:1050px}h1{font-size:25px;margin:0 0 8px}.lead{color:#b7c2cb;line-height:1.6}.toolbar{position:sticky;top:0;z-index:2;background:#11161cf2;border-bottom:1px solid #34404c;padding:12px 0;display:flex;gap:12px;align-items:center;flex-wrap:wrap}.progress{font-weight:700}.warning{color:#e6bd73}button{background:#263746;color:#e6edf2;border:1px solid #52677a;border-radius:7px;padding:12px 16px;font-weight:700}.trial{display:grid;grid-template-columns:60px 230px 1fr;gap:16px;align-items:center;padding:18px 12px;border-bottom:1px solid #34404c}.trial.incomplete{background:#241f1a}.trial-title{font-size:20px;font-weight:700}.trial-title small{display:block;font-size:12px;color:#7f8d9a}fieldset{border:1px solid #52677a;border-radius:8px;padding:12px;display:flex;gap:20px;flex-wrap:wrap}legend{font-weight:700;padding:0 6px}label{font-size:17px}.play.playing{border-color:#65c4f1;color:#78d0fa}@media(max-width:760px){.trial{grid-template-columns:1fr}.trial-title small{display:inline;margin-left:8px}}
</style></head><body><h1>Kirin Hypha ATTACK — kick確認</h1><p class="lead">各音源を聞いて、150–300 msの区間に低いキックが鳴っているかだけを選んでください。</p><div class="toolbar"><span class="progress" id="progress">0 / 11 完了</span><span class="warning" id="warning">回答は自動保存されます</span><button id="next">次の未回答へ</button><button id="save" disabled>完了TSVを保存</button></div><main>{{TRIALS}}</main><script>
const KEY='kirin-hypha-attack-kick-presence-b564-v1',DONE='Kirin_Hypha_ATTACK_kick_presence_B564_completed.tsv',trials=[...document.querySelectorAll('.trial')],progress=document.getElementById('progress'),save=document.getElementById('save');
function load(){try{return JSON.parse(localStorage.getItem(KEY)||'{}')}catch{return {}}}function collect(){return Object.fromEntries(trials.map(tr=>[tr.dataset.reviewId,tr.querySelector('input:checked')?.value||'']))}function refresh(){const rows=collect();let done=0;for(const tr of trials){const ok=Boolean(rows[tr.dataset.reviewId]);tr.classList.toggle('incomplete',!ok);if(ok)done++}localStorage.setItem(KEY,JSON.stringify(rows));progress.textContent=done+' / '+trials.length+' 完了';save.disabled=done!==trials.length}
const saved=load();for(const tr of trials){for(const radio of tr.querySelectorAll('input')){radio.checked=radio.value===saved[tr.dataset.reviewId];radio.addEventListener('change',refresh)}const audio=tr.querySelector('audio'),button=tr.querySelector('.play');button.addEventListener('click',async()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio){other.pause();other.currentTime=0}audio.currentTime=0;await audio.play()});audio.addEventListener('play',()=>{for(const b of document.querySelectorAll('.play'))b.classList.remove('playing');button.classList.add('playing');button.textContent='再生中…'});audio.addEventListener('ended',()=>{button.classList.remove('playing');button.textContent='▶ 150–300 msを再生'});audio.addEventListener('pause',()=>{if(!audio.ended){button.classList.remove('playing');button.textContent='▶ 150–300 msを再生'}})}
function cell(v){return String(v).replaceAll('\t',' ').replaceAll('\n',' ')}save.addEventListener('click',()=>{const rows=collect(),lines=['review_id\tclip_id\tkick_in_150_300_ms'];for(const tr of trials)lines.push([tr.dataset.reviewId,tr.dataset.clipId,rows[tr.dataset.reviewId]].map(cell).join('\t'));const blob=new Blob(['\uFEFF'+lines.join('\n')+'\n'],{type:'text/tab-separated-values;charset=utf-8'}),a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=DONE;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)});document.getElementById('next').addEventListener('click',()=>{const rows=collect();trials.find(tr=>!rows[tr.dataset.reviewId])?.scrollIntoView({block:'center'})});refresh();
</script></body></html>"##;

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
    fn review_has_only_one_listening_question() {
        let text = String::from_utf8(render_review_html(&clips()).unwrap()).unwrap();
        assert_eq!(text.matches("<div class=\"trial incomplete\"").count(), 11);
        assert_eq!(text.matches("<audio").count(), 11);
        assert_eq!(
            text.matches("この区間で低いキックが鳴っていますか？")
                .count(),
            11
        );
        for removed in [
            "確信度",
            "ガイド",
            "位置",
            "波形",
            "matched",
            "performance_id",
        ] {
            assert!(!text.contains(removed));
        }
    }

    #[test]
    fn review_rejects_wrong_set_or_order() {
        assert!(render_review_html(&[]).is_err());
        let mut invalid = clips();
        invalid.swap(0, 1);
        assert!(render_review_html(&invalid).is_err());
    }
}
