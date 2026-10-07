/* Standalone G0 wire audit; never a product or host acceptance test. */
const fs=require('fs'), path=require('path');
const {chromium}=require(process.env.G0_PLAYWRIGHT||'playwright');
(async()=>{
 const base=__dirname,browser=await chromium.launch({headless:true,...(process.env.G0_CHROMIUM?{executablePath:process.env.G0_CHROMIUM}:{channel:'chrome'})});
 const page=await browser.newPage({viewport:{width:1100,height:1050},deviceScaleFactor:1});
 await page.goto('file://'+path.join(base,'preview.html'));
 const cases=await page.evaluate(()=>Object.keys(window.G0_FIXTURES.cases)),report={synthetic:true,productAcceptance:false,browserVersion:browser.version(),layout:[],interaction:[],errors:[]};
 for(const surface of ['drum','psr'])for(const lang of ['ja','en'])for(let size=0;size<5;size++)for(const scenario of cases){
  await page.selectOption('#surface',surface);await page.selectOption('#lang',lang);await page.selectOption('#size',String(size));await page.selectOption('#scenario',scenario);
  const audit=await page.evaluate(()=>{
   const container=document.querySelector('.content'),body=container.getBoundingClientRect(),r=el=>{const range=document.createRange();range.selectNodeContents(el);const q=range.getBoundingClientRect();return{x:q.x,y:q.y,right:q.right,bottom:q.bottom,width:q.width,height:q.height};};
   const errors=[],texts=[];
   for(const el of container.querySelectorAll('[data-read]')){const q=r(el);texts.push({role:el.dataset.read,text:el.textContent,box:q,font:parseFloat(getComputedStyle(el).fontSize)});if(!q.width&&!q.height)continue;if(q.x<body.x-.5||q.right>body.right+.5||q.y<body.y-.5||q.bottom>body.bottom+.5)errors.push({kind:'outside body',role:el.dataset.read,text:el.textContent,box:q});const cell=el.closest('.cell');if(cell){const cb=cell.getBoundingClientRect();if(q.x<cb.x-.5||q.right>cb.right+.5||q.y<cb.y-.6||q.bottom>cb.bottom+.6)errors.push({kind:'outside cell',role:el.dataset.read,text:el.textContent,box:q});}}
   for(const cell of container.querySelectorAll('.cell')){const spans=[...cell.querySelectorAll('[data-read]')];for(let i=0;i<spans.length;i++)for(let k=i+1;k<spans.length;k++){const a=r(spans[i]),b=r(spans[k]);if(a.width&&b.width&&Math.min(a.bottom,b.bottom)-Math.max(a.y,b.y)>.2&&Math.min(a.right,b.right)-Math.max(a.x,b.x)>.2)errors.push({kind:'cross-role text overlap',roles:[spans[i].dataset.read,spans[k].dataset.read],texts:[spans[i].textContent,spans[k].textContent]});}}
   for(const row of container.querySelectorAll('.context,.band-controls')){const kids=[...row.children];for(let i=1;i<kids.length;i++){const a=kids[i-1].getBoundingClientRect(),b=kids[i].getBoundingClientRect();if(a.right>b.x+.5)errors.push({kind:'control overlap',text:row.textContent});}if(row.scrollWidth>row.clientWidth+1)errors.push({kind:'row overflow',text:row.textContent,overflow:row.scrollWidth-row.clientWidth});}
   const cells=[...container.querySelectorAll('.cell')];if(cells.length&&cells.at(-1).getBoundingClientRect().bottom>body.bottom+.5)errors.push({kind:'readouts height'});
   return{errors,texts,body:{width:body.width,height:body.height}};
  });
  report.layout.push({surface,lang,scale:[100,125,150,200,300][size],scenario,...audit});
  if(audit.errors.length)report.errors.push({surface,lang,size,scenario,errors:audit.errors});
  if(surface==='drum'&&['whole','bound'].includes(scenario)&&lang==='ja'||surface==='drum'&&scenario==='huge'&&size===0&&lang==='en'||surface==='psr'&&size===1&&['whole','solo','pending'].includes(scenario)&&lang==='ja')await page.locator('#instrument').screenshot({path:path.join(base,'screenshots',`${surface}_${[100,125,150,200,300][size]}_${lang}_${scenario}.png`)});
 }
 async function check(name,run){try{await run();report.interaction.push({name,pass:true});}catch(e){report.interaction.push({name,pass:false,error:e.message});report.errors.push({name,error:e.message});}}
 const assert=(x,m)=>{if(!x)throw Error(m)};
 await page.selectOption('#surface','drum');await page.selectOption('#lang','ja');await page.selectOption('#size','0');await page.selectOption('#scenario','whole');
 await check('cluster freezes on drag / resize; End returns LIVE',async()=>{
  await page.click('#history-hit',{position:{x:65,y:23}});const a=await page.evaluate(()=>G0.getPacket());
  const box=await page.locator('#history-hit').boundingBox();await page.mouse.move(box.x+70,box.y+24);await page.mouse.down();await page.mouse.move(box.x+77,box.y+27);await page.mouse.up();const b=await page.evaluate(()=>G0.getPacket());assert(a.presentation.selectedKey===b.presentation.selectedKey,'drag changed selected key');
  await page.selectOption('#size','4');const c=await page.evaluate(()=>G0.getPacket());assert(a.presentation.selectedKey===c.presentation.selectedKey&&a.presentation.cutoff===c.presentation.cutoff,'resize changed frozen hit');
  await page.keyboard.press('ArrowRight');const d=await page.evaluate(()=>G0.getPacket());assert(d.cluster.index===1,'cluster cycle');
  await page.keyboard.press('End');assert((await page.evaluate(()=>G0.getPacket())).presentation.mode==='LIVE','End not LIVE');
 });
 await check('125% BAND / VIEW inside instrument',async()=>{
  await page.selectOption('#size','1');assert(await page.locator('[data-band]').count()===9,'band count');await page.click('[data-band="2k"]');assert((await page.evaluate(()=>G0.getPacket())).presentation.band==='2k','band not adopted');await page.click('#view');assert((await page.locator('#view').textContent()).includes('OVERLAY'),'VIEW not toggled');
 });
 await check('D4 primary / subset in evidence',async()=>{
  await page.selectOption('#scenario','bound');const cell=page.locator('[data-lane="release"]');assert((await cell.textContent()).includes('≥+200'),'bound missing');assert(!(await cell.textContent()).includes('−10'),'subset promoted');await page.click('#evidence');assert((await page.locator('.evidence').textContent()).includes('確定部分 −10 ms（1/8打）'),'subset not disclosed');await page.click('#close-evidence');
 });
 await check('ALL latest B / LOCK window-out same key',async()=>{
  await page.selectOption('#scenario','all');await page.selectOption('#size','0');await page.click('#locator');const a=await page.evaluate(()=>G0.getPacket());assert(a.presentation.selectedKey==='B'&&a.presentation.lanes[1].value==='−10.0','latest does not show B');await page.selectOption('#size','4');const b=await page.evaluate(()=>G0.getPacket());assert(b.presentation.selectedKey===a.presentation.selectedKey&&JSON.stringify(b.presentation.lanes)===JSON.stringify(a.presentation.lanes),'ALL resize');await page.selectOption('#scenario','lock');assert((await page.evaluate(()=>G0.getPacket())).presentation.eventTime===2.1,'past LOCK moved time');
 });

 await check('fixture exact count within 0..N',async()=>{
  const bad=await page.evaluate(()=>Object.entries(G0_FIXTURES.cases).flatMap(([key,x])=>x.lanes.filter(l=>l.n>l.N||l.n<0).map(l=>key+':'+l.key)));assert(!bad.length,'invalid exact count '+bad);
 });
 await check('PSR independent of global main target',async()=>{
  await page.selectOption('#surface','psr');await page.selectOption('#scenario','whole');await page.selectOption('#size','1');const a=await page.locator('[data-read="psr-value"]').textContent();await page.click('#main-target');const b=await page.locator('[data-read="psr-value"]').textContent();assert(a===b&&b.includes('Δ'),'PSR follows global toggle');await page.selectOption('#scenario','solo');assert((await page.locator('[data-read="psr-value"]').textContent()).includes('POST 10.1 dB'),'solo PSR');await page.selectOption('#surface','drum');
 });
 await check('presentation JSON retains raw / displayed interval',async()=>{
  await page.selectOption('#scenario','huge');const pending=page.waitForEvent('download');await page.click('#snapshot');const download=await pending;const file=await download.path();const d=JSON.parse(fs.readFileSync(file,'utf8'));assert(d.synthetic&&d.g0,'synthetic marker');assert(d.presentation.lanes[3].value==='[−3202.6,+3202.6]'&&d.display[3].value==='[−3.21,+3.21]'&&d.display[3].unit==='×10³ dB','raw / display lost');
 });
 for(const cadence of ['2','4','8'])await check('BAND '+cadence+' Hz / 500 ms gap',async()=>{
  await page.selectOption('#scenario','whole');await page.selectOption('#cadence',cadence);await page.click('#play');await page.click('#gap');await page.waitForTimeout(2100);const d=await page.evaluate(()=>G0.getPacket());assert(d.trace.every(t=>t.V<=t.C+1e-9),'V ahead of C');assert(d.trace.some(t=>t.held),'no HOLD');assert(d.recentered>=1,'no resume anchor');report.interaction.push({name:'trace '+cadence+' Hz',frames:d.trace.length,recentered:d.recentered,heldFrames:d.trace.filter(t=>t.held).length});await page.click('#gap');
 });
 await page.selectOption('#scenario','whole');await page.selectOption('#size','4');await page.click('#evidence');await page.locator('#instrument').screenshot({path:path.join(base,'screenshots','drum_300_ja_evidence.png')});
 fs.writeFileSync(path.join(base,'layout_measurements.json'),JSON.stringify(report)+'\n');
 console.log(JSON.stringify({cases:report.layout.length,errors:report.errors.length,interactions:report.interaction}));if(report.errors.length)console.log(JSON.stringify(report.errors.slice(0,6),null,2));await browser.close();process.exitCode=report.errors.length?1:0;
})().catch(e=>{console.error(e);process.exitCode=1});
