/* G0 standalone prototype. Does not read audio, plugin files, network, or Work. */
(() => {
 const $=id=>document.getElementById(id), F=window.G0_FIXTURES;
 const dims=[[300,200,4,76,292,120],[375,250,6,86,363,158],[450,300,8,96,434,164],[600,400,10,108,580,248],[900,600,14,136,872,412]];
 const fonts={primary:[14,15,18,22,29],label:[11,11,11.5,13,16],scope:[11,11,12,14,17],action:[11,12,13,15,18]};
 let pointerStart=null, suppressClick=false, mainTarget='POST';
 let viewRows=false, overrideBand=null;
 let chosen='whole', locked=false, lockedPacket=null, selected='B', cluster=null, evidence=false, playing=false, gap=false;
 let wall=0, sourceC=10, viewport=9.85, anchorV=9.85, anchorWall=0, held=false, nextSummary=0, cycle=0, start=0, latestFrame=0;
 let trace=[], active=null, prevStamp='', scopeShift=0, recentered=0;
 const ja=()=> $('lang').value==='ja';
 const text=(a,b)=>ja()?a:b;
 const clone=x=>JSON.parse(JSON.stringify(x));
 const shown=l=>l.displayValue||l.value.replaceAll(', ', ',');
 const unit=l=>l.displayUnit||l.unit;
 const counts=l=>l.scalarClass==='bound'?[0,1,0,0,0]:l.counts||[l.n,l.kind==='WholeInterval'?l.N-l.n:0,l.reason==='unknown'?l.N-l.n:0,l.reason==='pending'?l.N-l.n:0,['pre','silent','bothsilent'].includes(l.reason)?l.N-l.n:0];
 const scope=l=>l.kind==='Single'?text(locked?'固定した一打':'最新の一打',locked?'Locked hit':'Latest hit'):l.kind==='ConfirmedSubset'?text(`確定${l.n}/${l.N}`,`Exact ${l.n}/${l.N}`):text(`全${l.N}打`,`All ${l.N}`);
 const reason=l=>({pending:l.kind==='Single'?text('取得中','pending'):text(`取得${l.N-l.n}`,`wait ${l.N-l.n}`),unknown:text(`不明${l.N-l.n}`,`? ${l.N-l.n}`),silent:text('音なし','no sound'),bothsilent:text('両無音','both silent'),pre:text('PRE必要','need PRE'),unbounded:text('全体不定','whole ?')})[l.reason]||'';
 const age=l=>l.kind==='ConfirmedSubset'?`${l.age.toFixed(1)}s` : '';
 const maxAge=data=>Math.max(0,...data.lanes.filter(x=>x.kind==='ConfirmedSubset').map(x=>x.age));
 const stamp=data=>[data.revision,data.target,data.band,data.selectedKey||'',data.lanes.map(x=>x.kind+':'+x.n+':'+x.value).join('|')].join('/');
 function optionList(){ $('scenario').innerHTML=Object.entries(F.cases).map(([key,x])=>`<option value="${key}">${ja()?x.title:x.en}</option>`).join(''); $('scenario').value=chosen; }
 function chart(data,w,h,single=false){
  const range=6, C=sourceC, V=viewport;
  const times=data.band==='ALL'?[6.1,6.6,7.1,7.6,8.1,8.6,9.1,10.125]:[6.1,6.6,7.1,7.6,8.1,8.6,9.1,9.6];
  const points=[];
  for(let i=0;i<=240;i++){const t=V-range+i*range/240;let y=0;for(const hit of times){if(hit>C)continue;const dt=t-hit;if(dt>=0&&dt<.35)y+=Math.exp(-dt*17)*.68;}points.push(`${i*w/240},${h-3-y*(h-5)}`);}
  let path=`<polyline points="${points.join(' ')}" fill="none" stroke="#e0bd7e" stroke-width="1.3"/>`;
  for(let i=0;i<times.length;i++){const t=times[i];if(t>C||t<C-6)continue;const x=(t-(V-range))*w/range;if(x<0||x>w)continue;const key=i===7?'B':`H${i}`;path+=`<line data-event="${key}" x1="${x}" x2="${x}" y1="2" y2="${h-1}" stroke="${locked&&key===selected?'#b5e6ef':'#8a7554'}" stroke-width="${key===selected?2:1}"/>`;}
  return `<svg viewBox="0 0 ${w} ${h}" aria-label="${text('全帯域履歴・直近6秒','Full-band history / six seconds')}">${path}</svg>`;
 }
 function envelope(w,h,data,region='HEAD',idx=0){
  const split=viewRows&&data.target==='Δ';
  const cap=Math.ceil(fonts.label[idx]*1.2), axFont=[11,11,11,12,14][idx], ax=Math.ceil(axFont*1.12), plotH=Math.max(0,h-cap-ax-8);
  const absent=data.envelope==='unmeasured', subset=data.lanes.some(l=>l.kind==='ConfirmedSubset');
  if(absent)return `<svg viewBox="0 0 ${w} ${h}" aria-label="no measured envelope"><text x="5" y="14" fill="#e8e2d8" font-size="11">${text('包絡未取得','No envelope')}</text></svg>`;
  const parts=subset?[[0,31],[35,63]]:[[0,63]];
  let paths='';for(let j=data.target==='POST'?1:0;j<2;j++)for(const [lo,hi] of parts){let p=[];for(let i=lo;i<=hi;i++){const space=split?(plotH-4)/2:plotH;const y=cap+4+(split?j*(space+4):0)+space*(data.envelope==='both-silent'?.95:.9-.65*Math.exp(-i/18))+(data.envelope==='both-silent'||split?0:j*3);p.push(`${i*w/63},${y}`);}paths+=`<polyline points="${p.join(' ')}" fill="none" stroke="${j?'#e0bd7e':'#d8d0c4'}" stroke-width="1.5"/>`;}
  return `<svg viewBox="0 0 ${w} ${h}" aria-label="synthetic measured mask example"><text x="5" y="${cap-2}" fill="#e8e2d8" font-size="${fonts.label[idx]}">${region} · ${text(locked?'単打':'平均(dB)',locked?'Single':'Mean(dB)')} · ${subset?'1–8':locked?'1':'8'} · ${data.target==='POST'?'POST':'PRE / POST'}</text>${paths}<text x="5" y="${h-2}" fill="#e8e2d8" font-size="${axFont}">${region==='HEAD'?'−20..+40 ms':'0..+300 ms'}</text></svg>`;
 }
 function current(){if(lockedPacket)return clone(lockedPacket);const data=clone(F.cases[chosen]);data.revision=playing?($('surface').value==='psr'?Math.floor(wall*10)+1:F.cases[chosen].band==='ALL'?Math.floor(wall*30)+1:cycle+1):1;data.cutoff=sourceC;if(overrideBand)data.band=overrideBand;data.mode=locked?'LOCK':held?'HOLD':'LIVE';data.selectedKey=locked?selected:data.selectedKey||'B';if(data.band==='ALL'&&sourceC>=10.255){data.lanes[0].value='+3.2';data.lanes[0].reason='';data.lanes[3].value='1.25';data.lanes[3].reason='';}return data;}
 function selectSingle(key){
  selected=key;locked=true;const d=clone(F.cases[chosen]);if(overrideBand)d.band=overrideBand;d.selectedKey=key;d.mode='LOCK';d.cutoff=sourceC;d.eventTime=key==='A'?2.1:key==='B'?d.band==='ALL'?10.125:9.6:key==='H6'?9.1:8.6;d.revision=100+['H5','H6','B'].indexOf(key);
  if(d.band==='ALL'){d.lanes=clone(F.cases.lock.lanes);d.lanes[1].value=key==='B'?'−10.0':'−30.0';}
  else {d.lanes=[{key:'delay',value:d.target==='POST'?'---':'+1.5',unit:'ms',reason:d.target==='POST'?'pre':''},{key:'attack',value:d.target==='POST'?'35.0':'+35.0',unit:'ms',reason:''},{key:'release',value:'≥147',unit:'ms',reason:''},{key:'level',value:d.target==='POST'?'−15.0':'−1.2',unit:d.target==='POST'?'dBFS':'dB',reason:''}].map(l=>({...l,kind:'Single',n:l.reason||l.key==='release'?0:1,N:1,age:0,scalarClass:l.reason?'not-applicable':l.key==='release'?'bound':'exact'}));}
  lockedPacket=d;
 }
 function details(data,b,idx){
  const isPsr=$('surface').value==='psr';
  const body=isPsr?`<p>${text('ピークと短時間平均音量の開き。ΔはPOST−PRE。','The gap between peak and short-term loudness. Δ is POST−PRE.')}</p><p>${text('PSR＝直近400 msのsample peak−3秒のShort-term loudness。True Peakや音質評価とは異なります。','PSR = sample peak over 400 ms minus short-term loudness over 3 s. This is not True Peak or a quality score.')}</p>`:
   `<p>${text('検出打音を先に選び、測れない打音を過去の打音で補充しません。','Select detected hits first; do not backfill missing hits.')}</p>${data.lanes.map(l=>`<p>${F.metrics[l.key][0]} · ${scope(l)} · ${l.value} ${l.unit}<br>${text('確定／限界／不明／取得／不成立','exact / bound / unknown / pending / N/A')} = ${counts(l).join('/')}<br>${l.conditionalValue?text(`確定部分 ${l.conditionalValue} ${l.unit}（${l.n}/${l.N}打）`,`Exact subset ${l.conditionalValue} ${l.unit} (${l.n}/${l.N})`):''}${l.wholeInterval?text(`全体 ${l.wholeInterval}：範囲を限定できない`,`Whole ${l.wholeInterval}: unconstrained`):''}<br>${reason(l)} ${l.kind==='ConfirmedSubset'?text(`最新確定は${l.age.toFixed(1)}秒前（snapshot時点）`,`Latest exact ${l.age.toFixed(1)}s ago at snapshot`):''}${l.displayValue?'<br>'+text('共有指数は両端に適用。raw端点は上記の値、表示は外向丸め。','Shared exponent applies to both endpoints. Raw values above; display rounded outward.'):''}</p>`).join('')}<p>${text('ATT/RELは包絡の実測時間で、コンプレッサーの設定値ではありません。値軸と分解能はこの根拠面へ置きます。','ATT/REL measure envelopes, not compressor settings. Value axes and resolution belong here.')}</p><p>${text('平均（dB）と中央値は別の統計。図は同じ参加集合だけを結びます。','Mean in dB and median are distinct. Connect only identical participant sets.')}</p><div style="height:80px">${envelope(b[4]-30,80,data,'HEAD',idx)}</div>`;
  return `<div class="evidence" style="left:2px;top:2px;width:${b[4]-4}px;height:${b[5]-4}px;font-size:${fonts.scope[idx]}px"><h2>${text('対象と根拠','Scope & evidence')}</h2>${data.notice==='oldpre'?`<p>${text('旧PREとの帯域比較に未対応。DRUMはPOST実測です。','Old PRE band comparison unsupported. DRUM shows POST measurements.')}</p>`:''}${body}<button id="close-evidence">${text('戻る','Back')}</button></div>`;
 }
 function drum(data,b,idx){
  const compact=idx<=2, cellLine=[43,45,50,0,0][idx];
  const controlsH=[0,24,24,22,32][idx];
  const historyH=b[5]-controlsH-(compact?2*cellLine:idx===3?144:220);
  const bandChoices=['ALL','63','125','250','500','1k','2k','4k','8k'];
  const bandControls=idx===0?'':`<div class="band-controls" style="height:${controlsH}px;font-size:${fonts.action[idx]}px">${bandChoices.map(x=>`<button data-band="${x}" class="${data.band===x?'selected-band':''}">${x}</button>`).join('')}<button id="view">VIEW ${viewRows?'OVERLAY':'2 ROWS'}</button></div>`;
  const contextH=Math.max(Math.ceil(fonts.scope[idx]*1.12),Math.ceil(fonts.action[idx]*1.2)), waveH=Math.max(12,historyH-contextH-8);
  const caption=locked?`${data.band} · ${text('固定','LOCK')} ${data.selectedKey} · ${(data.eventTime||0).toFixed(3)}s`:`${data.band} · ${data.lanes[0].N}${text('打',' hits')} · ${data.span.toFixed(1)}s${maxAge(data)>0?text(` · 確定最長${maxAge(data).toFixed(1)}s前`,` · exact age ${maxAge(data).toFixed(1)}s`):''}${data.notice==='oldpre'?text(' · 旧PRE:比較なし',' · old PRE: POST'):''}`;
  const locator=data.band==='ALL'?`<button id="locator">${text(locked?'固定した一打':'最新一打',locked?'LOCKED':'LATEST')} ${data.selectedKey}${' · '+(data.eventTime||10.125).toFixed(3)+'s'}</button>`:'';
  const clusterChip=cluster?`<span>${`${data.band} · ${(data.eventTime||0).toFixed(3)}s · ${text('候補','Candidates')}`} ${cluster.index+1}/3 <button id="prev-candidate">‹</button><button id="next-candidate">›</button></span>`:'';
  const ctx=`<div class="context" style="height:${contextH}px;font-size:${fonts.scope[idx]}px"><span data-read="caption">${cluster?clusterChip:data.band==='ALL'?locator:caption}</span><span><button class="live" id="live">${data.mode==='LOCK'?text('LIVEへ','LIVE'):data.mode}</button><button id="evidence">${text('根拠','Info')}</button></span></div>`;
  let hist=compact?`<div id="history-hit" class="history" style="height:${historyH}px">${ctx}<div style="height:${waveH}px">${chart(data,b[4]-6,waveH)}</div></div>`:`<div id="history-hit" class="history" style="height:${historyH}px">${ctx}<div class="panes" style="height:${historyH-contextH}px">${envelope((b[4]-22)/2,historyH-contextH,data,'HEAD',idx)}${envelope((b[4]-22)/2,historyH-contextH,data,'TAIL',idx)}</div></div>`;
  const cells=data.lanes.map(l=>{
   const captionLabel=F.metrics[l.key][0]+(ja()?' '+F.metrics[l.key][1]:'');
   const sub=reason(l)+(age(l)?' · '+age(l):'');
   let content=`<div class="metric" style="height:${Math.ceil(fonts.label[idx]*1.2)}px;line-height:${Math.ceil(fonts.label[idx]*1.2)}px;font-size:${fonts.label[idx]}px"><span data-read="label">${captionLabel}</span><span data-read="unit">${unit(l)}</span></div><div class="scope" style="height:${Math.ceil(fonts.scope[idx]*1.12)}px;line-height:${Math.ceil(fonts.scope[idx]*1.12)}px;font-size:${fonts.scope[idx]}px"><span data-read="scope">${scope(l)}</span><span class="reason" data-read="reason">${sub}</span></div><div class="value" data-read="value" style="height:${[16,18,21,25,33][idx]}px;line-height:${[16,18,21,25,33][idx]}px;font-size:${fonts.primary[idx]}px">${shown(l)}</div>`;
   if(!compact){
    const labelW=idx===3?108:160,scopeW=idx===3?128:188,valueW=idx===3?218:300;
    content=`<div class="metric" style="position:absolute;left:6px;top:2px;width:${labelW}px;font-size:${fonts.label[idx]}px;display:block;line-height:${Math.ceil(fonts.label[idx]*1.2)}px"><span data-read="label">${captionLabel}</span><br><span data-read="unit">${unit(l)}</span></div><div class="scope" style="position:absolute;left:${labelW+14}px;top:1px;width:${scopeW}px;display:block;font-size:${fonts.scope[idx]}px;line-height:${Math.max(Math.ceil(fonts.scope[idx]*1.12),idx===3?17:0)}px"><span data-read="scope">${scope(l)}</span><span class="reason" data-read="reason" style="display:block;float:none">${sub}</span></div><div class="value" data-read="value" style="position:absolute;right:6px;top:2px;width:${valueW}px;text-align:right;font-size:${fonts.primary[idx]}px;line-height:${Math.ceil(fonts.primary[idx]*1.12)}px">${shown(l)}</div>`;
   }
   return `<div class="cell" data-lane="${l.key}" style="height:${compact?cellLine:(b[5]-historyH-controlsH)/4}px;padding:0 6px">${content}</div>`;
  }).join('');
  return `${bandControls}${hist}<div class="readouts" style="grid-template-columns:${compact?'1fr 1fr':'1fr'};height:${b[5]-historyH-controlsH}px">${cells}</div>${evidence?details(data,b,idx):''}`;
 }
 function psr(data,b,idx){
  if(idx===0)return `<div class="psr-lane" style="font-size:11px;height:${b[5]}px"><span data-read="main-values">S −10.7 LUFS · TP −0.6 dBTP</span><br><span data-read="psr-hidden">${text('PSRは125%以上で表示','PSR appears at 125% and above')}</span></div>`;
  const waiting=chosen==='pending'||chosen==='oldpre',solo=chosen==='solo';
  const expired=playing&&held&&wall-(sourceC-(F.cases[chosen].band==='ALL'?10.2:10))>=.4;
  const tick=Math.round(sourceC*10)%2;
  const value=waiting||expired?'Δ ---':solo?`POST ${tick?'10.2':'10.1'} dB`:`Δ ${tick?'−3.1':'−3.3'} dB`;
  const gh=[0,37,39,64,128][idx];
  const graph=(end,color,present=true)=>{let pts=[];const left=sourceC-6;for(let i=0;i<=60;i++){const t=left+i*.1;if(t>end)break;const x=(t-left)/6*(b[4]-20);pts.push(`${x},${(gh-13)*(.5-.25*Math.sin(t*2)-.1*Math.cos(t*6))}`);}return `<svg style="height:${gh}px" viewBox="0 0 ${b[4]-20} ${gh}" aria-label="synthetic history ending ${end.toFixed(3)}s"><polyline points="${present?pts.join(' '):''}" fill="none" stroke="${color}"/><text x="5" y="${gh-2}" fill="#e8e2d8" font-size="11">${end.toFixed(3)}s</text></svg>`;};
  return `<div class="psr-lane" style="padding:4px 10px;height:${b[5]}px;font-size:${fonts.scope[idx]}px;line-height:${Math.ceil(fonts.scope[idx]*1.2)}px"><div><button id="main-target" data-read="main-target">${text('全体','MAIN')} ${mainTarget}</button><span data-read="main-values"> ${mainTarget==='Δ'?'Δ M +1.1 · S +0.8 LU · TP −0.2 dB':'M −11.2 · S −10.7 LUFS · TP −0.6 dBTP'}</span></div><div data-read="main-aux">${mainTarget==='Δ'?'PLR Δ −1.3 dB · CORR Δ +0.02':'PLR 12.1 dB · CORR +0.92'}</div>${graph(sourceC,'#e0bd7e')}<div class="psr-read"><span>PSR</span><b data-read="psr-value" style="font-size:${fonts.primary[idx]}px;line-height:${Math.ceil(fonts.primary[idx]*1.12)}px">${value}</b><button id="psr-help" data-read="psr-help" style="margin-left:auto">${text('意味','Info')}</button></div><div class="psr-reason" data-read="psr-reason">${waiting?text('同時刻のPREなし','No matched PRE'):expired?text('比較値の期限切れ','Comparison expired'):'&nbsp;'}</div>${graph(sourceC-.1,'#7fcfd8',!waiting)}${evidence?details(data,b,idx):''}</div>`;
 }
 function render(){
  const idx=+$('size').value,b=dims[idx],data=current();active=data;
  $('instrument').style.width=b[0]+'px';$('instrument').style.height=b[1]+'px';
  const compact=idx<2, title=compact?14:18;
  $('instrument').innerHTML=`<div class="shell" style="height:${[25,29,34,39,49][idx]}px;font-size:${title}px"><b>POST HYPHA</b><span style="font-size:${fonts.label[idx]}px">${data.target==='Δ'||chosen==='oldpre'?'PAIR': 'POST'}</span></div><div class="nav" style="top:${[30,35,40,46,59][idx]}px;height:22px;font-size:${fonts.label[idx]}px"><span>LEVEL　<span class="active">TIME</span>　FREQ　SPACE</span><span>${$('surface').value==='psr'?mainTarget:data.target} · ${[100,125,150,200,300][idx]}%</span></div><div class="time-nav" style="top:${b[3]-23}px;height:19px;font-size:${fonts.label[idx]}px"><span class="${$('surface').value==='psr'?'active':''}">HISTORY</span><span class="${$('surface').value==='drum'?'active':''}">DRUM</span><span>SHARP</span><span>LIVE</span>${idx<3&&$('surface').value==='drum'?`<span class="full-history-label">${text('全帯域履歴','FULL BAND')}</span>`:''}</div><div class="content" style="left:${b[2]}px;top:${b[3]}px;width:${b[4]}px;height:${b[5]}px">${$('surface').value==='drum'?drum(data,b,idx):psr(data,b,idx)}</div>${idx>=2?`<div class="footer" style="font-size:${fonts.label[idx]}px"><span>${held?'HOLD':'LIVE'}</span><span>VU　 MENU　${idx>=3?'CAPTURE':''}</span></div>`:''}`;
  ['live','locator','evidence','close-evidence','psr-help','history-hit','prev-candidate','next-candidate','view','main-target'].forEach(id=>{const el=$(id);if(!el)return;if(id==='history-hit'){el.onpointerdown=e=>{pointerStart=[e.clientX,e.clientY];suppressClick=false;};el.onpointerup=e=>{suppressClick=pointerStart&&Math.hypot(e.clientX-pointerStart[0],e.clientY-pointerStart[1])>3;};}
   el.onclick=event=>{event.stopPropagation();if(id==='history-hit'&&suppressClick){suppressClick=false;return;}
   if(id==='main-target'){mainTarget=mainTarget==='POST'?'Δ':'POST';}
   if(id==='view'){viewRows=!viewRows;}
   if(id==='live'){locked=false;lockedPacket=null;cluster=null;}
   if(id==='locator'){selectSingle(data.selectedKey||'B');}
   if(id==='evidence'||id==='psr-help')evidence=true;
   if(id==='close-evidence')evidence=false;
   if(id==='history-hit'){if(!cluster)cluster={keys:['H5','H6','B'],index:0};else cluster.index=(cluster.index+1)%3;selectSingle(cluster.keys[cluster.index]);}
   if(id==='prev-candidate'||id==='next-candidate'){cluster.index=(cluster.index+(id==='prev-candidate'?-1:1)+3)%3;selectSingle(cluster.keys[cluster.index]);}
   render();
  };});
  document.querySelectorAll('[data-band]').forEach(el=>{el.onclick=()=>{stop();overrideBand=el.dataset.band;chosen=overrideBand==='ALL'?'all':'whole';sourceC=overrideBand==='ALL'?10.2:10;viewport=sourceC-.15;$('scenario').value=chosen;locked=false;lockedPacket=null;cluster=null;render();};});
  $('status').textContent=`G0 / SYNTHETIC · ${b[0]}×${b[1]} · body ${b[4]}×${b[5]} · packet ${data.revision}\nC=${sourceC.toFixed(3)}s / V=${viewport.toFixed(3)}s · ${held?'HOLD':'LIVE'} · ${data.band==='ALL'?'single native 30Hz':$('cadence').value+'Hz summary'} · ${locked?'LOCK '+selected:'LIVE'}${cluster?' / frozen candidates '+(cluster.index+1)+'/3':''}\n表示遅延は測定結果・窓応答とは別。scope/type/key/shapeは一つのpacketから描画。`;
  const nowStamp=stamp(data);if(prevStamp&&prevStamp!==nowStamp)scopeShift++;prevStamp=nowStamp;
 }
 function loop(now){
  if(!playing)return;wall=(now-start)/1000;
  if(wall>=2){stop();render();return;}
  const nextC=(F.cases[chosen].band==='ALL'?10.2:10)+Math.floor(wall*10)/10;
  if(!(gap&&wall>.6&&wall<1.1)&&nextC>sourceC){sourceC=nextC;if(held){anchorV=sourceC-.15;anchorWall=wall;held=false;recentered++;}}
  const candidate=anchorV+(wall-anchorWall);
  if(candidate>=sourceC){viewport=sourceC;held=true;}else if(!held)viewport=candidate;
  if(wall>=nextSummary&&!locked&&F.cases[chosen].band!=='ALL'&&$('surface').value==='drum'){const choices=F.sequence;chosen=choices[cycle%choices.length];$('scenario').value=chosen;cycle++;nextSummary=wall+1/(+$('cadence').value);}
  if(wall-latestFrame>=1/30){latestFrame=wall;render();trace.push({wall,C:sourceC,V:viewport,revision:active.revision,kind:active.lanes[0].kind,key:selected,held});if(trace.length>1200)trace.shift();}
  requestAnimationFrame(loop);
 }
 function stop(){playing=false;$('play').textContent=text('動的比較','Animate');}
 $('play').onclick=()=>{if(playing){stop();return;}playing=true;wall=0;sourceC=F.cases[chosen].band==='ALL'?10.2:10;viewport=anchorV=sourceC-.15;anchorWall=0;nextSummary=0;cycle=0;latestFrame=0;trace=[];recentered=0;held=false;start=performance.now();$('play').textContent=text('停止','Stop');requestAnimationFrame(loop);};
 $('gap').onclick=()=>{gap=!gap;$('gap').textContent=gap?text('欠落注入中','Gap enabled'):text('欠落を注入','Inject gap');};
 $('snapshot').onclick=()=>{const packet={g0:true,synthetic:true,presentation:active,display:active.lanes.map(l=>({key:l.key,value:shown(l),unit:unit(l)})),C:sourceC,V:viewport,held,cluster,renderKinds:active.lanes.map(x=>x.kind),trace};const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(packet,null,2)],{type:'application/json'}));a.download='hypha-g0-presentation.json';a.click();URL.revokeObjectURL(a.href);};
 ['size','surface','lang','scenario','cadence'].forEach(id=>{$(id).onchange=()=>{if(id==='scenario'){stop();chosen=$('scenario').value;overrideBand=null;locked=chosen==='lock';lockedPacket=null;selected=F.cases[chosen].selectedKey||'B';cluster=null;if(chosen==='all'){sourceC=10.2;viewport=10.04;}else{sourceC=10;viewport=9.85;}if(locked)selectSingle('A');}if(id==='lang')optionList();render();};});
 document.addEventListener('keydown',e=>{if(!['ArrowLeft','ArrowRight','Home','End','Escape'].includes(e.key))return;e.preventDefault();if(e.key==='End'){locked=false;lockedPacket=null;cluster=null;}else if(e.key==='Escape'){cluster=null;}else{if(!cluster)cluster={keys:['H5','H6','B'],index:0};cluster.index=e.key==='Home'?0:(cluster.index+(e.key==='ArrowLeft'?-1:1)+3)%3;selectSingle(cluster.keys[cluster.index]);}render();});
 window.G0={render,getPacket:()=>({presentation:active,C:sourceC,V:viewport,held,cluster,trace,scopeShift,recentered}),dims,fonts};
 optionList();render();
})();
