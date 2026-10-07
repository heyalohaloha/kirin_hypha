/* G0 presentation fixtures: synthetic examples, never production observations. */
window.G0_FIXTURES = (() => {
 const lane=(key,value,unit,kind='WholePoint',n=8,reason='',age=0)=>({key,value,unit,kind,n:kind==='Single'?Math.min(n,1):n,N:kind==='Single'?1:8,reason,age});
 const base={envelope:'measured',target:'Δ',band:'1k',cutoff:10,span:3.5,revision:1,mode:'LIVE'};
 const cases={
  whole:{...base,title:'全対象が成立',en:'Whole cohort',lanes:[lane('delay','+2.7','ms'),lane('attack','+6.4','ms'),lane('release','+28','ms'),lane('level','−0.8','dB')]},
  subset:{...base,title:'確定部分・古いREL',en:'Subset / older REL',lanes:[lane('delay','+2.7','ms','ConfirmedSubset',7,'pending',.1),lane('attack','+6.4','ms','ConfirmedSubset',7,'pending',.1),lane('release','+20','ms','ConfirmedSubset',1,'unknown',3),lane('level','−0.8','dB','ConfirmedSubset',7,'pending',.1)]},
  bound:{...base,title:'全体の限界を優先',en:'Whole bounds first',lanes:[lane('delay','[−2.4, +3.2]','ms','WholeInterval',0),lane('attack','(0.0, +4.0]','ms','WholeInterval',0),{...lane('release','≥+200','ms','WholeInterval',1),conditionalValue:'−10',counts:[1,7,0,0,0]},lane('level','≤−52.0','dB','WholeInterval',0)]},
  widest:{...base,title:'最大区間・桁幅',en:'Maximum interval widths',lanes:[lane('delay','[−10.0, +10.0]','ms','WholeInterval',0),lane('attack','[−40.0, +40.0]','ms','WholeInterval',0),lane('release','[−300, +300]','ms','WholeInterval',0),lane('level','[−842.0, +842.0]','dB','WholeInterval',0)]},
  huge:{...base,title:'有限区間・共有指数',en:'Finite / shared exponent',lanes:[lane('delay','[−10.0,+10.0]','ms','WholeInterval',0),lane('attack','[−40.0,+40.0]','ms','WholeInterval',0),lane('release','[−300,+300]','ms','WholeInterval',0),{...lane('level','[−3202.6,+3202.6]','dB','WholeInterval',0),displayValue:'[−3.21,+3.21]',displayUnit:'×10³ dB'}]},
  extreme:{...base,title:'有限型の最大指数',en:'Maximum finite exponent',lanes:[lane('delay','[−10.0,+10.0]','ms','WholeInterval',0),lane('attack','[−40.0,+40.0]','ms','WholeInterval',0),lane('release','[−300,+300]','ms','WholeInterval',0),{...lane('level','[−1.7976931348623157e308,+1.7976931348623157e308]','dB','WholeInterval',0),displayValue:'[−1.80,+1.80]',displayUnit:'×10³⁰⁸ dB'}]},
  unbounded:{...base,title:'全体の範囲なし・確定1/8',en:'Unconstrained whole / exact 1 of 8',lanes:['delay','attack','release','level'].map(k=>({...lane(k,'−10',k==='level'?'dB':'ms','ConfirmedSubset',1,'unbounded',3),counts:[1,7,0,0,0],wholeInterval:'(−∞,+∞)'}))},
  pending:{...base,envelope:'unmeasured',title:'全指標取得中',en:'All pending',lanes:['delay','attack','release','level'].map(k=>lane(k,'---',k==='level'?'dB':'ms','NoScalar',0,'pending'))},
  silent:{...base,title:'片側無音',en:'One side silent',lanes:[lane('delay','---','ms','NoScalar',0,'silent'),lane('attack','---','ms','NoScalar',0,'silent'),lane('release','---','ms','NoScalar',0,'silent'),lane('level','≤−52.0','dB','WholeInterval',0)]},
  bothsilent:{...base,envelope:'both-silent',title:'帯域の両側無音',en:'Both sides silent in band',lanes:['delay','attack','release','level'].map(k=>lane(k,'---',k==='level'?'dB':'ms','NoScalar',0,'bothsilent'))},
  solo:{...base,target:'POST',title:'POST単体',en:'POST alone',lanes:[lane('delay','---','ms','NoScalar',0,'pre'),lane('attack','35.0','ms'),lane('release','31','ms'),lane('level','−16.5','dBFS')]},
  oldpre:{...base,target:'POST',title:'旧PRE・比較なし',en:'Older PRE / no comparison',notice:'oldpre',lanes:[lane('delay','---','ms','NoScalar',0,'pre'),lane('attack','35.0','ms'),lane('release','31','ms'),lane('level','−16.5','dBFS')]},
  all:{...base,target:'POST',band:'ALL',title:'ALL最新B・時間軸未到達',en:'ALL latest B before viewport',selectedKey:'B',eventTime:10.125,lanes:[lane('transient','---','dB','Single',0,'pending'),lane('strength','−10.0','dBFS','Single'),lane('crest','12.0','dB','Single'),lane('sharpness','---','acum','Single',0,'pending')]},
  lock:{...base,target:'POST',band:'ALL',mode:'LOCK',selectedKey:'A',eventTime:2.1,title:'窓外に固定した一打',en:'Locked past hit',lanes:[lane('transient','+3.2','dB','Single'),lane('strength','−30.0','dBFS','Single'),lane('crest','12.0','dB','Single'),lane('sharpness','1.25','acum','Single')]}
 };
 return {cases,sequence:['whole','subset','bound','pending','whole'],metrics:{
  delay:['DELAY','到達差'],attack:['ATT','立上'],release:['REL','減衰'],level:['LEVEL','ピーク'],transient:['TRANSIENT','頭/胴'],strength:['STRENGTH','頭音量'],crest:['CREST','ピーク差'],sharpness:['SHARPNESS','鋭さ']
 }};
})();
