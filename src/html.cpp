#include "html.h"

// ── Index ─────────────────────────────────────────────────────────────────────

const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock</title><style>
body{font-family:sans-serif;text-align:center;padding:20px;background:#111;color:#eee;max-width:520px;margin:0 auto}
h1{font-size:2em;margin:0 0 8px}
#t{font-size:3.5em;font-weight:bold;margin:16px 0;letter-spacing:4px}
.mode{display:block;width:100%;margin:10px auto;padding:18px;font-size:1.45em;
  background:#3a9;color:#fff;border:0;border-radius:8px;box-sizing:border-box;cursor:pointer}
.mode:active{background:#2a8}
.mode.current{animation:modeBlink 1s linear infinite}
@keyframes modeBlink{0%,49%{color:#fff}50%,99%{color:transparent}}
hr{border:0;border-top:1px solid #333;margin:18px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.btn{display:block;padding:13px;font-size:1em;background:#357;color:#fff;border-radius:8px;
  text-decoration:none;box-sizing:border-box;border:0;cursor:pointer}
.grid button{width:100%;font-family:inherit}
.btn:active{background:#246}
#st{margin-top:12px;font-size:.85em;color:#8d8;min-height:1.2em}
</style></head>
<body>
<h1 id="apn">Clock</h1>
<div id="t">--:--:--</div>
<button id="mode-countdown" class="mode" onclick="setMode('countdown')">Countdown</button>
<button id="mode-clock" class="mode" onclick="setMode('clock')">Clock</button>
<button id="mode-countup" class="mode" onclick="setMode('countup')">Countup</button>
<hr>
<div class="grid">
  <a class="btn" href="/format">Format</a>
  <a class="btn" href="/wifi">WiFi</a>
  <a class="btn" href="/time-sync">Time Sync</a>
  <a class="btn" href="/messages">Messages</a>
  <button class="btn" onclick="runDemo()">Demo</button>
  <a class="btn" href="/config">Config</a>
</div>
<div id="st"></div>
<script>
function f(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('t').textContent=d.time;}).catch(()=>{});}
f();setInterval(f,1000);
fetch('/api/wifi/status').then(r=>r.json()).then(d=>{var n=d.ssid||d.apSsid||'Clock';document.getElementById('apn').textContent=n;document.title=n;}).catch(()=>{});
function showMode(mode){
  ['countdown','clock','countup'].forEach(function(m){
    document.getElementById('mode-'+m).classList.toggle('current',m===mode);
  });
}
fetch('/api/config').then(r=>r.json()).then(d=>{showMode(['countdown','countup','clock'][d.mode||0]);}).catch(()=>{});
function setStatus(s){document.getElementById('st').textContent=s||'';}
function setMode(mode){
  fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:mode})})
    .then(r=>r.json()).then(d=>{setStatus(d.message||d.error);if(!d.error)showMode(mode);}).catch(()=>setStatus('Mode change failed'));
}
function runDemo(){
  setStatus('Running demo...');
  fetch('/api/demo/test',{method:'POST'})
    .then(r=>r.json()).then(d=>{
      if(d.error){setStatus(d.error);return;}
      setTimeout(()=>setStatus('Demo finished.'),d.preview_ms||10000);
    }).catch(()=>setStatus('Demo failed'));
}
</script>
</body></html>
)rawliteral";

// ── Config ────────────────────────────────────────────────────────────────────
// Loads format lists from /api/formats and current settings from /api/config.
// Saves via POST /api/config with JSON — no server-side template tokens needed.

const char CONFIG_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Format</title><style>
body{font-family:sans-serif;padding:18px;background:#111;color:#eee;max-width:640px;margin:0 auto}
h1{font-size:2rem;margin:0 0 6px}
h2{font-size:1.25rem;color:#8af;margin:22px 0 8px}
label{display:block;margin-top:14px;font-size:1rem;color:#bbb}
input,select{width:100%;box-sizing:border-box;padding:12px;background:#202020;color:#eee;
  border:1px solid #555;border-radius:8px;font-size:1.1rem;margin-top:6px;font-family:monospace}
input[type=range]{padding:0;height:34px}
input[type=checkbox]{width:auto;margin-right:6px}
hr{border:none;border-top:1px solid #333;margin:22px 0}
button{margin-top:20px;padding:14px 28px;font-size:1.1rem;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
#st{margin-top:12px;font-size:1rem;color:#8d8;min-height:1.4em}
a{color:#6af;font-size:1rem}
</style></head>
<body>
<h1>Format</h1>

<label>Mode
<select id="mode" onchange="updateSections()">
  <option value="0">Countdown</option>
  <option value="1">Count Up</option>
  <option value="2">Clock</option>
</select></label>

<div id="sec-countdown">
<hr><h2>Countdown</h2>
<label>Format<select id="sel-cd"></select></label>
<label>Target date &amp; time<input type="datetime-local" id="target"></label>
</div>

<div id="sec-countup">
<hr><h2>Count Up</h2>
<label>Format<select id="sel-cu"></select></label>
<label><input type="checkbox" id="startNow" onchange="toggleStart(this)"> Use current time at boot</label>
<input type="datetime-local" id="start">
</div>

<div id="sec-clock">
<hr><h2>Clock</h2>
<label>Format<select id="sel-ck"></select></label>
</div>

<hr><h2>Display</h2>
<label>Brightness: <span id="briteVal">4</span>
<input type="range" id="brite" min="0" max="7"
  oninput="previewBrightness(this.value)"></label>

<button onclick="save()">Save</button>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>

<script>
function dtl(s){return(s&&s!='now')?s.replace(' ','T').slice(0,16):'';}
function fdt(s){return s?s.replace('T',' ')+':00':'now';}
function fill(id,arr,sel){
  var el=document.getElementById(id);el.innerHTML='';
  arr.forEach(function(o,i){
    var op=document.createElement('option');
    op.value=i;op.textContent=o;
    if(i===sel)op.selected=true;
    el.appendChild(op);
  });
}
function updateSections(){
  var m=parseInt(document.getElementById('mode').value);
  document.getElementById('sec-countdown').style.display=m===0?'':'none';
  document.getElementById('sec-countup').style.display=m===1?'':'none';
  document.getElementById('sec-clock').style.display=m===2?'':'none';
}
function toggleStart(cb){
  document.getElementById('start').disabled=cb.checked;
}
var briteTimer=null;
function previewBrightness(value){
  document.getElementById('briteVal').textContent=value;
  if(briteTimer)clearTimeout(briteTimer);
  briteTimer=setTimeout(function(){
    fetch('/api/brightness',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({brightness:parseInt(value)})
    }).catch(function(){});
  },120);
}
Promise.all([
  fetch('/api/formats').then(function(r){return r.json();}),
  fetch('/api/config').then(function(r){return r.json();})
]).then(function(res){
  var f=res[0],d=res[1];
  fill('sel-cd',f.countdown,d.countdownFmt||0);
  fill('sel-cu',f.countup,d.countupFmt||0);
  fill('sel-ck',f.clock,d.clockFmt||1);
  document.getElementById('mode').value=d.mode||0;
  document.getElementById('target').value=dtl(d.countdownDatetime);
  var isNow=(d.countupDatetime==='now'||!d.countupDatetime);
  document.getElementById('startNow').checked=isNow;
  document.getElementById('start').value=isNow?'':dtl(d.countupDatetime);
  document.getElementById('start').disabled=isNow;
  var b=d.brightness!==undefined?d.brightness:4;
  document.getElementById('brite').value=b;
  document.getElementById('briteVal').textContent=b;
  updateSections();
}).catch(function(){document.getElementById('st').textContent='Load failed';});

function save(){
  var isNow=document.getElementById('startNow').checked;
  var body={
    mode:parseInt(document.getElementById('mode').value),
    countdownFmt:parseInt(document.getElementById('sel-cd').value),
    countupFmt:parseInt(document.getElementById('sel-cu').value),
    clockFmt:parseInt(document.getElementById('sel-ck').value),
    brightness:parseInt(document.getElementById('brite').value),
    countdownDatetime:fdt(document.getElementById('target').value),
    countupDatetime:isNow?'now':fdt(document.getElementById('start').value)
  };
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    document.getElementById('st').textContent=d.message||d.error;
  }).catch(function(){document.getElementById('st').textContent='Save failed';});
}
</script>
</body></html>
)rawliteral";

// ── WiFi ──────────────────────────────────────────────────────────────────────
// Shows current WiFi runtime state and saves station credentials for next boot.

const char WIFI_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Settings</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em}
h2{font-size:1.35em;color:#8af;margin:18px 0 8px}
label{display:block;margin-top:10px;font-size:.85em;color:#aaa}
.sectionHead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:16px}
.sectionHead h2{margin:0}
.field{display:grid;grid-template-columns:118px 1fr;gap:10px;align-items:center;margin-top:10px;color:#aaa;font-size:.85em}
.field input{margin-top:0}
input,select{width:100%;box-sizing:border-box;padding:8px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:.95em;margin-top:3px}
button{margin-top:12px;padding:10px 24px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
#scan{background:#357;margin-top:0;padding:8px 18px}
#scan:active{background:#246}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
#networks{margin-top:8px;border:1px solid #333;border-radius:6px;overflow:hidden;background:#181818}
.network{display:grid;grid-template-columns:1fr auto auto;gap:10px;align-items:center;width:100%;
  padding:10px;border:0;border-bottom:1px solid #2b2b2b;background:#181818;color:#eee;text-align:left;
  border-radius:0;margin:0;box-sizing:border-box}
.network:last-child{border-bottom:0}
.network:active,.network.selected{background:#26384a}
.ssid{font-weight:bold;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.rssi{font-size:.85em;color:#bbb;font-family:monospace}
.bars{display:flex;align-items:flex-end;gap:2px;width:24px;height:18px}
.bars span{display:block;width:4px;background:#444;border-radius:1px}
.bars span:nth-child(1){height:5px}.bars span:nth-child(2){height:9px}.bars span:nth-child(3){height:13px}.bars span:nth-child(4){height:17px}
.bars.l1 span:nth-child(-n+1),.bars.l2 span:nth-child(-n+2),.bars.l3 span:nth-child(-n+3),.bars.l4 span:nth-child(-n+4){background:#3a9}
.muted{color:#aaa}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>WiFi Settings</h1>
<div class="sectionHead"><h2>Station Mode</h2><button id="scan" onclick="scan()">Scan</button></div>
<div id="networks"><div class="network muted">Scanning...</div></div>
<label class="field"><span>Network</span><input type="text" id="staSsid" placeholder="SSID"></label>
<label class="field"><span>Password</span><input type="password" id="staPw" placeholder="open networks can be blank"></label>
<button onclick="connect()">Connect &amp; Reboot</button>
<h2>Access Point Mode</h2>
<label class="field"><span>AP Name</span><input type="text" id="apSsid"></label>
<label class="field"><span>AP Password</span><input type="password" id="apPw" placeholder="blank keeps current"></label>
<button onclick="saveAp()">Save AP &amp; Reboot</button>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function setStatus(s){document.getElementById('st').textContent=s||'';}
function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.getElementById('staSsid').value=d.staSsid||'';
    document.getElementById('apSsid').value=d.apSsid||'';
  }).catch(function(){});
}
function signalLevel(rssi){
  if(rssi>=-55)return 4;
  if(rssi>=-67)return 3;
  if(rssi>=-75)return 2;
  return 1;
}
function selectNetwork(button,ssid){
  document.getElementById('staSsid').value=ssid;
  Array.prototype.forEach.call(document.querySelectorAll('.network'),function(n){n.classList.remove('selected');});
  button.classList.add('selected');
}
function networkRow(n){
  var button=document.createElement('button');
  var level=signalLevel(n.rssi);
  button.type='button';
  button.className='network';
  button.onclick=function(){selectNetwork(button,n.ssid||'');};
  var ssid=document.createElement('span');
  ssid.className='ssid';
  ssid.textContent=(n.ssid||'(hidden network)')+(n.secure?'':' open');
  var rssi=document.createElement('span');
  rssi.className='rssi';
  rssi.textContent=n.rssi+' dBm';
  var bars=document.createElement('span');
  bars.className='bars l'+level;
  for(var i=0;i<4;i++)bars.appendChild(document.createElement('span'));
  button.appendChild(ssid);
  button.appendChild(rssi);
  button.appendChild(bars);
  return button;
}
function scan(){
  setStatus('Scanning...');
  fetch('/api/wifi/scan').then(function(r){return r.json();}).then(function(d){
    var list=document.getElementById('networks');
    list.innerHTML='';
    (d.networks||[]).sort(function(a,b){return b.rssi-a.rssi;}).forEach(function(n){
      list.appendChild(networkRow(n));
    });
    if(!list.children.length){
      var empty=document.createElement('div');
      empty.className='network muted';
      empty.textContent='No networks found';
      list.appendChild(empty);
    }
    setStatus(list.children.length&&list.children[0].tagName==='BUTTON'?'':'No networks found');
  }).catch(function(){setStatus('Scan failed');});
}
function connect(){
  var body={ssid:document.getElementById('staSsid').value,password:document.getElementById('staPw').value};
  fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(){setStatus('Connect failed');});
}
function saveAp(){
  var body={apSsid:document.getElementById('apSsid').value};
  var pw=document.getElementById('apPw').value;
  if(pw)body.apPassword=pw;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(){setStatus('AP save failed');});
}
loadConfig();scan();
</script>
</body></html>
)rawliteral";

// ── Utility ───────────────────────────────────────────────────────────────────

const char CONFIG_JSON_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Config</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em}h2{font-size:1em;color:#8af;margin:14px 0 4px}
pre{background:#1a1a1a;padding:12px;border-radius:6px;overflow-x:auto;font-size:.8em;color:#8f8;white-space:pre-wrap}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Config</h1>
<h2>config.json</h2>
<pre id="cfg">Loading&#8230;</pre>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  document.getElementById('cfg').textContent=JSON.stringify(d,null,2);
}).catch(function(){document.getElementById('cfg').textContent='Not found';});
</script>
</body></html>
)rawliteral";

const char TIME_SYNC_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Time Sync</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:560px;margin:0 auto}
h1{font-size:2rem;margin:0 0 6px;text-align:center}
p{font-size:1.05rem;line-height:1.5;color:#ccc}
.now{background:#1b1b1b;border:1px solid #333;border-radius:8px;padding:12px;margin:16px 0;font-size:1.05rem;line-height:1.5}
.timegrid{display:grid;grid-template-columns:72px 1fr 1fr;gap:8px;align-items:center}
.timegrid div{padding:4px 0}
.head{color:#8af;font-size:.9rem}
.label{color:#aaa}
.value{font-family:monospace;font-size:1.05rem}
button{margin-top:10px;padding:14px 28px;font-size:1.1rem;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
#st{margin-top:12px;font-size:1rem;color:#8d8;min-height:1.4em}
a{color:#6af;font-size:1rem}
</style></head>
<body>
<h1>Time Sync</h1>
<p>This sets the real-time clock to the current date and time reported by this browser.</p>
<div class="now">
  <div class="timegrid">
    <div></div><div class="head">Time</div><div class="head">Date</div>
    <div class="label">Browser</div><div class="value" id="browserTime">--</div><div class="value" id="browserDate">--</div>
    <div class="label">Device</div><div class="value" id="deviceTime">--</div><div class="value" id="deviceDate">--</div>
  </div>
</div>
<button onclick="syncTime()">Sync RTC</button>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function showNow(){
  var n=new Date();
  document.getElementById('browserDate').textContent=formatDate(n);
  document.getElementById('browserTime').textContent=formatTime(n);
}
function showDeviceNow(){
  fetch('/api/time').then(function(r){return r.json();}).then(function(d){
    document.getElementById('deviceDate').textContent=d.date||'--';
    document.getElementById('deviceTime').textContent=d.time||'--';
  }).catch(function(){
    document.getElementById('deviceDate').textContent='unavailable';
    document.getElementById('deviceTime').textContent='unavailable';
  });
}
function pad(n){return String(n).padStart(2,'0');}
function formatDate(n){return n.getFullYear()+'-'+pad(n.getMonth()+1)+'-'+pad(n.getDate());}
function formatTime(n){return pad(n.getHours())+':'+pad(n.getMinutes())+':'+pad(n.getSeconds());}
showNow();showDeviceNow();
setInterval(showNow,1000);
setInterval(showDeviceNow,1000);
function setStatus(s){document.getElementById('st').textContent=s||'';}
function syncTime(){
  var n=new Date();
  var body={year:n.getFullYear(),month:n.getMonth()+1,day:n.getDate(),hour:n.getHours(),minute:n.getMinutes(),second:n.getSeconds()};
  fetch('/api/time/sync',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(function(r){return r.json();}).then(function(d){setStatus(d.message||d.error);showDeviceNow();})
    .catch(function(){setStatus('Time sync failed');});
}
</script>
</body></html>
)rawliteral";

// ── Messages ──────────────────────────────────────────────────────────────────

const char MESSAGE_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Messages</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:560px;margin:0 auto}
h1{font-size:1.7em;margin-bottom:4px}
h2{font-size:1.1em;color:#8af;margin:22px 0 8px}
p{font-size:.95em;line-height:1.4;color:#bbb}
label{display:block;margin-top:10px;font-size:.9em;color:#aaa}
.row{display:flex;gap:8px}
.row input{flex:1;min-width:0;box-sizing:border-box;padding:10px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:1.3em;margin-top:3px;font-family:monospace;text-align:center}
button{margin-top:14px;padding:11px 22px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer;margin-right:8px}
button.secondary{background:#357}
button:active{background:#2a8}
button.secondary:active{background:#246}
#st{margin-top:12px;font-size:.9em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Messages</h1>
<p>Each message is split across the three display panels. Test shows the message on the device for 5 seconds without saving.</p>

<h2>Splash</h2>
<label>Shown at boot for 5 seconds</label>
<div class="row">
  <input id="s1" maxlength="4" placeholder="R1" oninput="nextPanel(this,'s2')">
  <input id="s2" maxlength="4" placeholder="R2" oninput="nextPanel(this,'s3')">
  <input id="s3" maxlength="4" placeholder="R3" oninput="trimPanel(this)">
</div>
<button class="secondary" onclick="testMsg('s')">Test Splash</button>

<h2>Final</h2>
<label>Shown when countdown or demo finishes</label>
<div class="row">
  <input id="f1" maxlength="4" placeholder="R1" oninput="nextPanel(this,'f2')">
  <input id="f2" maxlength="4" placeholder="R2" oninput="nextPanel(this,'f3')">
  <input id="f3" maxlength="4" placeholder="R3" oninput="trimPanel(this)">
</div>
<button class="secondary" onclick="testMsg('f')">Test Final</button>
<button class="secondary" onclick="runDemo()">Run Demo</button>

<br><button onclick="save()">Save Messages</button>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function trimPanel(el){el.value=el.value.slice(0,4);}
function nextPanel(el,nextId){trimPanel(el);if(el.value.length>=4)document.getElementById(nextId).focus();}
function msg(prefix){return(document.getElementById(prefix+'1').value||'').slice(0,4)+(document.getElementById(prefix+'2').value||'').slice(0,4)+(document.getElementById(prefix+'3').value||'').slice(0,4);}
function split(prefix,value){
  value=value||'';
  document.getElementById(prefix+'1').value=value.slice(0,4);
  document.getElementById(prefix+'2').value=value.slice(4,8);
  document.getElementById(prefix+'3').value=value.slice(8,12);
}
function setStatus(s){document.getElementById('st').textContent=s||'';}
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  split('s',d.splashMessage||'');
  split('f',d.finalMessage||'');
}).catch(function(){setStatus('Load failed');});
function testMsg(prefix){
  setStatus('Previewing for 5 seconds...');
  fetch('/api/message/test',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({message:msg(prefix)})
  }).then(function(r){return r.json();}).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(){setStatus('Test failed');});
}
function runDemo(){
  setStatus('Running demo...');
  fetch('/api/demo/test',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({finalMessage:msg('f')})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.error){setStatus(d.error);return;}
    setTimeout(function(){setStatus('Demo finished.');},d.preview_ms||10000);
  }).catch(function(){setStatus('Demo failed');});
}
function save(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({splashMessage:msg('s'),finalMessage:msg('f')})
  }).then(function(r){return r.json();}).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(){setStatus('Save failed');});
}
</script>
</body></html>
)rawliteral";

// ── Demo ──────────────────────────────────────────────────────────────────────
