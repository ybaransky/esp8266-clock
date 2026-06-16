#include "html.h"

// ── Index ─────────────────────────────────────────────────────────────────────

const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock</title><style>
body{font-family:sans-serif;text-align:center;padding:20px;background:#111;color:#eee}
h1{font-size:2em;margin:0 0 8px}
#t{font-size:3.5em;font-weight:bold;margin:16px 0;letter-spacing:4px}
.btn{display:block;max-width:280px;margin:10px auto;padding:13px;font-size:1em;
  background:#3a9;color:#fff;border-radius:8px;text-decoration:none;box-sizing:border-box}
.btn:active{background:#2a8}
</style></head>
<body>
<h1 id="apn">Clock</h1>
<div id="t">--:--:--</div>
<a class="btn" href="/config">Configure</a>
<a class="btn" href="/demo">Demo</a>
<a class="btn" href="/wifi">WiFi Settings</a>
<a class="btn" href="/utility">Utility</a>
<script>
function f(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('t').textContent=d.time;}).catch(()=>{});}
f();setInterval(f,1000);
fetch('/api/wifi').then(r=>r.json()).then(d=>{var n=d.ssid||'Clock';document.getElementById('apn').textContent=n;document.title=n;}).catch(()=>{});
</script>
</body></html>
)rawliteral";

// ── Config ────────────────────────────────────────────────────────────────────
// Loads format lists from /api/formats and current settings from /api/config.
// Saves via POST /api/config with JSON — no server-side template tokens needed.

const char CONFIG_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configure</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em;margin-bottom:4px}
h2{font-size:1em;color:#8af;margin:14px 0 4px}
label{display:block;margin-top:10px;font-size:.85em;color:#aaa}
input,select{width:100%;box-sizing:border-box;padding:7px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:.95em;margin-top:3px;font-family:monospace}
input[type=range]{padding:0}
input[type=checkbox]{width:auto;margin-right:6px}
hr{border:none;border-top:1px solid #2a2a2a;margin:14px 0}
button{margin-top:16px;padding:10px 24px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Configure</h1><a href="/">&#8592; Home</a>

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

<hr><h2>Messages</h2>
<label>Splash message (shown at boot for 5 s — leave blank to skip)
<input type="text" id="splash" maxlength="63"></label>
<label>Final message (shown at end of demo overlay)
<input type="text" id="msg" maxlength="63"></label>

<hr><h2>Display</h2>
<label>Justification<select id="sel-ju"></select></label>
<label>Brightness: <span id="briteVal">4</span>
<input type="range" id="brite" min="0" max="7"
  oninput="document.getElementById('briteVal').textContent=this.value"></label>

<button onclick="save()">Save</button>
<div id="st"></div>

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
Promise.all([
  fetch('/api/formats').then(function(r){return r.json();}),
  fetch('/api/config').then(function(r){return r.json();})
]).then(function(res){
  var f=res[0],d=res[1];
  fill('sel-cd',f.countdown,d.countdownFmt||0);
  fill('sel-cu',f.countup,d.countupFmt||0);
  fill('sel-ck',f.clock,d.clockFmt||1);
  fill('sel-ju',f.justification,d.justification||0);
  document.getElementById('mode').value=d.mode||0;
  document.getElementById('target').value=dtl(d.countdownDatetime);
  var isNow=(d.countupDatetime==='now'||!d.countupDatetime);
  document.getElementById('startNow').checked=isNow;
  document.getElementById('start').value=isNow?'':dtl(d.countupDatetime);
  document.getElementById('start').disabled=isNow;
  document.getElementById('splash').value=d.splashMessage||'';
  document.getElementById('msg').value=d.finalMessage||'';
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
    justification:parseInt(document.getElementById('sel-ju').value),
    brightness:parseInt(document.getElementById('brite').value),
    countdownDatetime:fdt(document.getElementById('target').value),
    countupDatetime:isNow?'now':fdt(document.getElementById('start').value),
    splashMessage:document.getElementById('splash').value,
    finalMessage:document.getElementById('msg').value
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
// Allows changing the AP name and password. Device reboots to apply.

const char WIFI_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Settings</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em}
label{display:block;margin-top:10px;font-size:.85em;color:#aaa}
input{width:100%;box-sizing:border-box;padding:8px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:.95em;margin-top:3px}
button{margin-top:12px;padding:10px 24px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>WiFi Settings</h1><a href="/">&#8592; Home</a><br><br>
<p style="font-size:.85em;color:#888">These are the Access Point credentials for this device. Changes take effect after reboot.</p>
<label>Access Point Name<input type="text" id="ssid"></label>
<label>Password<input type="password" id="pw" placeholder="leave blank to keep current"></label>
<button onclick="save()">Save &amp; Reboot</button>
<div id="st"></div>
<script>
fetch('/api/wifi').then(function(r){return r.json();}).then(function(d){
  document.getElementById('ssid').value=d.ssid||'';
}).catch(function(){});
function save(){
  var pw=document.getElementById('pw').value;
  var body={ssid:document.getElementById('ssid').value};
  if(pw)body.password=pw;
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    document.getElementById('st').textContent=d.message||d.error;
  }).catch(function(){document.getElementById('st').textContent='Save failed';});
}
</script>
</body></html>
)rawliteral";

// ── Utility ───────────────────────────────────────────────────────────────────

const char UTILITY_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Utility</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em}h2{font-size:1em;color:#8af;margin:14px 0 4px}
pre{background:#1a1a1a;padding:12px;border-radius:6px;overflow-x:auto;font-size:.8em;color:#8f8;white-space:pre-wrap}
button{margin-top:10px;padding:10px 24px;font-size:1em;border:none;border-radius:8px;cursor:pointer;color:#fff}
.del{background:#a33}.del:active{background:#822}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Utility</h1><a href="/">&#8592; Home</a>
<h2>config.json</h2>
<pre id="cfg">Loading&#8230;</pre>
<button class="del" onclick="del()">Delete config.json</button>
<div id="st"></div>
<script>
fetch('/api/config/raw').then(function(r){return r.text();}).then(function(t){
  try{document.getElementById('cfg').textContent=JSON.stringify(JSON.parse(t),null,2);}
  catch(e){document.getElementById('cfg').textContent=t;}
}).catch(function(){document.getElementById('cfg').textContent='Not found';});
function del(){
  if(!confirm('Delete config.json? Defaults load on next boot.'))return;
  fetch('/api/config/delete',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    document.getElementById('st').textContent=d.message||d.error;
    if(d.message)document.getElementById('cfg').textContent='(deleted)';
  }).catch(function(){document.getElementById('st').textContent='Failed';});
}
</script>
</body></html>
)rawliteral";

// ── Demo ──────────────────────────────────────────────────────────────────────
// Test fires demo mode live on the hardware without saving.
// Save persists the message and keeps demo mode active.

const char DEMO_HTML[] = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Demo</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em;margin-bottom:4px}
label{display:block;margin-top:10px;font-size:.85em;color:#aaa}
.row{display:flex;gap:8px}
.row input{flex:1;min-width:0;box-sizing:border-box;padding:8px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:1.2em;margin-top:3px;font-family:monospace;text-align:center}
button{margin-top:16px;padding:10px 24px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer;margin-right:8px}
button:active{background:#2a8}
#saveBtn{display:none}
#st{margin-top:10px;font-size:.85em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Demo</h1><a href="/">&#8592; Home</a>
<label>Message (3 panels &times; 4 chars)</label>
<div class="row">
  <input id="r1" maxlength="4" placeholder="R1" oninput="this.value=this.value.slice(0,4);if(this.value.length>=4)document.getElementById('r2').focus()">
  <input id="r2" maxlength="4" placeholder="R2" oninput="this.value=this.value.slice(0,4);if(this.value.length>=4)document.getElementById('r3').focus()">
  <input id="r3" maxlength="4" placeholder="R3" oninput="this.value=this.value.slice(0,4)">
</div>
<button onclick="test()">Test on display</button>
<button id="saveBtn" onclick="save()">Save</button>
<div id="st"></div>
<script>
var saveTimer=null;
function msg(){return(document.getElementById('r1').value||'').slice(0,4)+(document.getElementById('r2').value||'').slice(0,4)+(document.getElementById('r3').value||'').slice(0,4);}
function splitMsg(s){s=s||'';return[s.slice(0,4),s.slice(4,8),s.slice(8,12)];}
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  var p=splitMsg(d.finalMessage||'');
  document.getElementById('r1').value=p[0];
  document.getElementById('r2').value=p[1];
  document.getElementById('r3').value=p[2];
}).catch(function(){});
function test(){
  document.getElementById('saveBtn').style.display='none';
  document.getElementById('st').textContent='Running demo\u2026';
  if(saveTimer)clearTimeout(saveTimer);
  fetch('/api/demo/test',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({finalMessage:msg()})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.error){document.getElementById('st').textContent=d.error;return;}
    var ms=d.preview_ms||10000;
    saveTimer=setTimeout(function(){
      document.getElementById('saveBtn').style.display='inline-block';
      document.getElementById('st').textContent='Demo finished. Save to keep this message.';
    },ms);
  }).catch(function(){document.getElementById('st').textContent='Test failed';});
}
function save(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({finalMessage:msg()})
  }).then(function(r){return r.json();}).then(function(d){
    document.getElementById('st').textContent=d.message||d.error;
    document.getElementById('saveBtn').style.display='none';
  }).catch(function(){document.getElementById('st').textContent='Save failed';});
}
</script>
</body></html>
)rawliteral";


