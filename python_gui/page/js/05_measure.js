// measurement
const MI=$('mi'),MR=$('mr'),MH=$('mh'),MN=$('mname'),MM=$('mmemo');
MM.addEventListener('input',()=>{MM.style.height='auto';MM.style.height=Math.min(MM.scrollHeight,140)+'px';});
const f1=x=>x==null?'-':x.toFixed(1);
const esc=t=>String(t==null?'':t).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const LEGEND='<details class="fold"><summary>항목 설명</summary><dl class="legend">'+
  '<dt>입력→출력</dt><dd>카메라 프레임이 Runtime에 도착한 순간부터 그 프레임의 추적 결과가 나온 순간까지. 카메라 안에서 걸리는 시간은 빠져 있습니다.</dd>'+
  '<dt>출력 간격</dt><dd>추적 결과가 나오는 간격. 카메라 프레임 간격(24 fps면 약 41 ms)보다 짧아질 수 없습니다.</dd>'+
  '<dt>처리</dt><dd>한 프레임을 넣고 결과를 받기까지 서버 안에서 걸린 시간. 파이프라인을 켜면 다음 프레임을 기다리는 시간이 들어갑니다.</dd>'+
  '<dt>NPU 대기</dt><dd>검출 모델(NPU) 결과를 받을 때까지 기다린 시간. NPU 계산 시간이 포함됩니다.</dd>'+
  '<dt>트래커</dt><dd>박스를 이전 프레임과 이어 붙이는 추적 계산 시간.</dd>'+
  '<dt>p50 · p95 · p99</dt><dd>프레임의 50% · 95% · 99%가 이 값 이하라는 뜻. 최대는 가장 느린 한 프레임입니다.</dd>'+
  '<dt>&gt;33</dt><dd>33 ms(30 fps 한 프레임 시간)를 넘긴 프레임의 비율. 33 ms를 넘긴 칸은 노란색입니다.</dd></dl></details>';
let sel=null, shownKey='', lastState=null, editing=false, miHold=0, exporting=false;
const note=t=>{MI.textContent=t;miHold=Date.now()+4000;};
const openState=(root,sel_)=>{const o={};root.querySelectorAll(sel_).forEach(d=>{if(d.dataset.k)o[d.dataset.k]=d.open;});return o;};
const applyOpen=(root,sel_,o)=>{root.querySelectorAll(sel_).forEach(d=>{if(d.dataset.k in o)d.open=o[d.dataset.k];});};
function table(r){
  if(!r.frames)return '<div class="sum">기록된 프레임이 없습니다.</div>';
  let h='<div class="sum">'+r.frames+'프레임 · '+r.seconds.toFixed(1)+'초 · 출력 '+f1(r.out_fps)+' fps · 카메라 '+f1(r.cam_fps)+' fps</div>';
  h+='<table><tr><th>ms</th><th>p50</th><th>p95</th><th>p99</th><th>최대</th><th>&gt;33</th></tr>';
  for(const [k,lab,sub] of [['delay','입력→출력','카메라 도착→결과'],['interval','출력 간격','결과 사이 시간'],['compute','처리','서버 안 계산'],['npu_wait','NPU 대기','검출 결과 대기'],['tracker','트래커','추적 계산']]){
    const s=r[k];if(!s)continue;
    h+='<tr><td>'+lab+'<small>'+sub+'</small></td>'+[s.p50,s.p95,s.p99,s.max].map(x=>'<td'+(x>33?' class="over"':'')+'>'+f1(x)+'</td>').join('')+'<td>'+f1(s.over33)+'%</td></tr>';}
  return h+'</table>';}
function render(r){
  if(editing)return;
  const keep=openState(MR,'details');
  if(!r){MR.innerHTML='<div class="result"><div class="sum">아직 녹화 결과가 없습니다.</div></div>';return;}
  const vid=r.video?'16비트 원본 frames.raw.gz · '+r.video.frames+'장'+(r.video.dropped?' · 저장 큐에서 놓침 '+r.video.dropped+'장':''):'영상 저장 안 함';
  MR.innerHTML='<div class="result"><div class="row" style="justify-content:space-between"><div class="rtitle">'+r.n+'회차 · '+(r.name?esc(r.name):'<span style="color:var(--mut)">이름 없음</span>')+'</div></div>'+
    (r.memo?'<div class="rmemo">'+esc(r.memo)+'</div>':'')+
    '<div class="sum">'+r.frames+'프레임 · '+r.seconds.toFixed(1)+'초</div>'+
    '<details class="fold" data-k="file"><summary>저장 파일</summary><div class="file">Orange Pi의 measurement.output_root/recordings/MEASURE_…<br>'+vid+'<br>프레임 번호·시각 frames.csv, 추적 결과 tracks.csv, 형식 recording.txt</div></details></div>';
  applyOpen(MR,'details',keep);}
function history(m){
  const hs=m.history||[];if(!hs.length){MH.innerHTML='';return;}
  const keep=openState(MH,'details');
  let h='<details class="fold hist" data-k="hist"><summary>지난 측정 '+hs.length+'회 · 입력→출력 p50 / p95</summary><div>';
  for(const x of hs.slice().reverse()){const on=(sel==null?(m.last&&m.last.n===x.n):sel===x.n);
    h+='<button data-n="'+x.n+'"'+(on?' class="sel"':'')+'><span class="hn">'+x.n+'회차 · '+(x.name?esc(x.name):'이름 없음')+'</span><span class="hv">'+f1(x.delay_p50)+' / '+f1(x.delay_p95)+' ms</span></button>';}
  MH.innerHTML=h+'</div></details>';applyOpen(MH,'details',keep);
  for(const b of MH.querySelectorAll('button'))b.onclick=()=>{const n=+b.dataset.n;sel=(m.last&&m.last.n===n)?null:n;shownKey='';tick();};}
function mshow(m){if(m.available===false){$('mst').disabled=$('msp').disabled=true;MI.textContent=m.text||'측정 제어를 사용할 수 없습니다';return;}
  lastState=m;$('mst').disabled=m.on;$('msp').disabled=!m.on&&!m.failed;$('mexport').disabled=m.on||exporting;
  if(m.failed){MI.textContent='원본 영상 저장 오류가 발생했습니다. 종료를 눌러 저장 스레드를 정리하세요.';return;}
  if(m.on){chip('mdot','mtext','live','측정 중');MI.textContent=m.cur.n+'회차 측정 중'+(m.cur.name?' · '+m.cur.name:'')+' · '+m.frames+'프레임 · '+m.seconds.toFixed(0)+'초'+(m.video_frames!=null?' · 영상 '+m.video_frames+'장'+(m.video_dropped?' (놓침 '+m.video_dropped+')':''):'');}
  else{chip('mdot','mtext',m.last?'good':'',m.last?'측정 '+m.last.n+'회차 완료':'측정 대기');if(Date.now()>miHold)MI.textContent=m.last?'측정 끝 · 시작을 누르면 새 측정이 시작됩니다':'시작을 누르면 그 순간부터 프레임마다 기록합니다.';}
  history(m);
  if(sel!=null){const key='h'+sel;if(shownKey!==key){shownKey=key;fetch('/measure/'+sel).then(x=>x.json()).then(render).catch(()=>{});}return;}
  const key=m.last?('l'+m.last.n+'|'+m.last.name+'|'+m.last.memo):'none';if(shownKey!==key){shownKey=key;render(m.last);}}
function tick(){if(document.hidden)return Promise.resolve();return fetch('/measure',{cache:'no-store'}).then(r=>{lastOk=Date.now();return r.json();}).then(mshow).catch(()=>{});}
function mpost(u,body){return fetch(u,{method:'POST',headers:{'X-Gimbal':'1','Content-Type':'application/json'},body:JSON.stringify(body||{})}).then(r=>r.json()).then(m=>{sel=null;shownKey='';mshow(m);}).catch(()=>{note('서버 연결 끊김');});}
$('mst').addEventListener('click',()=>mpost('/measure/start',{name:MN.value,memo:MM.value,video:$('mvid').checked}));
$('msp').addEventListener('click',()=>mpost('/measure/stop',{memo:MM.value}).then(()=>{MN.value='';MM.value='';MM.style.height='';}));
$('mexport').addEventListener('click',()=>{
  exporting=true;$('mexport').disabled=true;MI.textContent='PC /workspace/recordings로 가져오는 중…';
  fetch('/measure/export',{method:'POST'}).then(r=>r.json()).then(r=>{
    note(r.ok?'PC 저장 완료: '+r.path:(r.text||'내보내기 실패'));
  }).catch(()=>note('내보내기 실패')).finally(()=>{exporting=false;$('mexport').disabled=!!(lastState&&lastState.on);});
});
