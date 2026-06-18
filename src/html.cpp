#include "html.h"

// Index

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>__DEVICE_NAME__</title><style>
body{font-family:sans-serif;text-align:center;padding:20px;background:#111;color:#eee;max-width:520px;margin:0 auto}
h1{font-size:2em;margin:0 0 8px}
.mode{display:block;width:100%;margin:10px auto;padding:18px;font-size:1.45em;
  background:#3a9;color:#fff;border:0;border-radius:8px;box-sizing:border-box;cursor:pointer}
.mode:active{background:#2a8}
.mode.current{animation:modeBlink 1s linear infinite}
@keyframes modeBlink{0%,49%{color:#fff}50%,99%{color:transparent}}
hr{border:0;border-top:1px solid #333;margin:18px 0}
.btn{display:block;padding:13px;font-size:1em;background:#357;color:#fff;border-radius:8px;
  text-decoration:none;box-sizing:border-box;border:0;cursor:pointer;margin:0 auto;max-width:240px}
.btn:active{background:#246}
#st{margin-top:12px;font-size:.85em;color:#8d8;min-height:1.2em}
</style></head>
<body>
<h1 id="apn">__DEVICE_NAME__</h1>
<hr>
<button id="mode-countdown" class="mode" onclick="setMode('countdown')">Countdown</button>
<button id="mode-clock" class="mode" onclick="setMode('clock')">Clock</button>
<button id="mode-countup" class="mode" onclick="setMode('countup')">Countup</button>
<hr>
<button id="mode-demo" class="mode" onclick="runDemo()">Demo</button>
<hr>
<a class="btn" href="/settings">Settings</a>
<div id="st"></div>
<script>
function $(id){return document.getElementById(id);}
var configuredMode='__INITIAL_MODE__';
var demoTimer=null;
function showMode(mode){
  ['countdown','clock','countup','demo'].forEach(function(m){
    $('mode-'+m).classList.toggle('current',m===mode);
  });
}
function showConfiguredMode(){
  showMode(configuredMode);
}
showConfiguredMode();
function setStatus(s){$('st').textContent=s||'';}
function setMode(mode){
  if(demoTimer){clearTimeout(demoTimer);demoTimer=null;}
  fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:mode})})
    .then(r=>r.json()).then(d=>{setStatus(d.message||d.error);if(!d.error){configuredMode=mode;showMode(mode);}}).catch(()=>setStatus('Mode change failed'));
}
function runDemo(){
  if(demoTimer){clearTimeout(demoTimer);}
  setStatus('Running demo...');
  showMode('demo');
  fetch('/api/demo/test',{method:'POST'})
    .then(r=>r.json()).then(d=>{
      if(d.error){setStatus(d.error);showConfiguredMode();return;}
      demoTimer=setTimeout(function(){demoTimer=null;setStatus('Demo finished.');showConfiguredMode();},d.preview_ms||10000);
    }).catch(()=>{setStatus('Demo failed');showConfiguredMode();});
}
</script>
</body></html>
)rawliteral";

// Settings
const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title><style>
body{font-family:sans-serif;text-align:center;padding:20px;background:#111;color:#eee;max-width:520px;margin:0 auto}
h1{font-size:2em;margin:0 0 18px}
.btn{display:block;padding:13px;font-size:1em;background:#357;color:#fff;border-radius:8px;
  text-decoration:none;box-sizing:border-box;border:0;cursor:pointer;margin:10px auto;max-width:260px}
.btn:active{background:#246}
.home{display:inline-block;margin-top:18px;color:#6af;font-size:1rem}
</style></head>
<body>
<h1>Settings</h1>
<a class="btn" href="/format">Formats</a>
<a class="btn" href="/messages">Messages</a>
<a class="btn" href="/wifi">Networks</a>
<a class="btn" href="/location">Location</a>
<a class="btn" href="/time-sync">Time Sync</a>
<a class="btn" href="/config">Directory</a>
<a class="home" href="/">&#8592; Home</a>
</body></html>
)rawliteral";

// Formats page: loads format lists and saves display settings as JSON.

const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Format</title><style>
body{font-family:sans-serif;padding:18px;background:#111;color:#eee;max-width:640px;margin:0 auto}
h1{font-size:2rem;margin:0 0 6px;text-align:center}
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
.actions{display:flex;justify-content:center;gap:10px}
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
<label>Target date &amp; time<input type="datetime-local" id="target" step="1"></label>
</div>

<div id="sec-countup">
<hr><h2>Count Up</h2>
<label>Format<select id="sel-cu"></select></label>
<label><input type="checkbox" id="startNow" onchange="toggleStart(this)"> Use current time at boot</label>
<input type="datetime-local" id="start" step="1">
</div>

<div id="sec-clock">
<hr><h2>Clock</h2>
<label>Format<select id="sel-ck"></select></label>
</div>

<hr><h2>Display</h2>
<label>Brightness: <span id="briteVal">4</span>
<input type="range" id="brite" min="0" max="7"
  oninput="previewBrightness(this.value)"></label>

<div class="actions"><button onclick="save()">Save</button></div>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>

<script>
function $(id){return document.getElementById(id);}
function reportFieldMismatch(page,field,configValue,acceptedValue,reason){
  fetch('/api/field-mismatch',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({page:page,field:field,configValue:String(configValue),acceptedValue:String(acceptedValue),reason:reason})
  }).catch(function(){});
}
function canonicalFieldValue(el,value){
  var text=value===undefined||value===null?'':String(value);
  if(el.type==='datetime-local'){
    var m=text.match(/^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})(?::(\d{2}))?/);
    return m?m[1]+'T'+m[2]+':'+(m[3]||'00'):text;
  }
  return text;
}
function setFieldFromConfig(page,id,field,configValue,fieldValue){
  var el=$(id), wanted=fieldValue===undefined||fieldValue===null?'':String(fieldValue);
  el.value=wanted;
  var rejected=canonicalFieldValue(el,el.value)!==canonicalFieldValue(el,wanted);
  var invalid=el.checkValidity&&!el.checkValidity();
  var conversionLost=configValue!==undefined&&configValue!==null&&String(configValue)!==''&&wanted==='';
  if(rejected||invalid||conversionLost){
    reportFieldMismatch(page,field,configValue,el.value,invalid?(el.validationMessage||'invalid value'):(conversionLost?'conversion produced empty value':'browser rejected value'));
  }
}
function dtl(s){
  if(!s||s==='now')return '';
  var m=String(s).match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2})(?::(\d{2}))?/);
  return m?m[1]+'T'+m[2]+':'+(m[3]||'00'):'';
}
function fdt(s){
  if(!s)return 'now';
  var m=String(s).match(/^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})(?::(\d{2}))?/);
  return m?m[1]+' '+m[2]+':'+(m[3]||'00'):'now';
}
function fdtOrUndef(s){
  if(!s)return undefined;
  var m=String(s).match(/^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})(?::(\d{2}))?/);
  return m?m[1]+' '+m[2]+':'+(m[3]||'00'):undefined;
}
function fill(id,arr,sel,page,field){
  var el=$(id);el.innerHTML='';
  var valid=sel>=0&&sel<arr.length;
  arr.forEach(function(o,i){
    var op=document.createElement('option');
    op.value=i;op.textContent=o;
    if(i===sel)op.selected=true;
    el.appendChild(op);
  });
  if(!valid)reportFieldMismatch(page,field,sel,el.value,'select option not found');
}
function updateSections(){
  var m=parseInt($('mode').value);
  $('sec-countdown').style.display=m===0?'':'none';
  $('sec-countup').style.display=m===1?'':'none';
  $('sec-clock').style.display=m===2?'':'none';
}
function toggleStart(cb){
  $('start').disabled=cb.checked;
}
var briteTimer=null;
function previewBrightness(value){
  $('briteVal').textContent=value;
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
  fill('sel-cd',f.countdown,d.countdownFmt||0,'format','countdownFmt');
  fill('sel-cu',f.countup,d.countupFmt||0,'format','countupFmt');
  fill('sel-ck',f.clock,d.clockFmt||1,'format','clockFmt');
  setFieldFromConfig('format','mode','mode',d.mode,d.mode!==undefined?d.mode:0);
  setFieldFromConfig('format','target','countdownDatetime',d.countdownDatetime,dtl(d.countdownDatetime));
  var isNow=(d.countupDatetime==='now'||!d.countupDatetime);
  $('startNow').checked=isNow;
  if(!isNow)setFieldFromConfig('format','start','countupDatetime',d.countupDatetime,dtl(d.countupDatetime));
  else $('start').value='';
  $('start').disabled=isNow;
  var b=d.brightness!==undefined?d.brightness:4;
  setFieldFromConfig('format','brite','brightness',d.brightness,b);
  $('briteVal').textContent=b;
  updateSections();
}).catch(function(){$('st').textContent='Load failed';});

function save(){
  var isNow=$('startNow').checked;
  var body={
    mode:parseInt($('mode').value),
    countdownFmt:parseInt($('sel-cd').value),
    countupFmt:parseInt($('sel-cu').value),
    clockFmt:parseInt($('sel-ck').value),
    brightness:parseInt($('brite').value),
    countdownDatetime:fdtOrUndef($('target').value),
    countupDatetime:isNow?'now':fdt($('start').value)
  };
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    $('st').textContent=d.message||d.error;
  }).catch(function(){$('st').textContent='Save failed';});
}
</script>
</body></html>
)rawliteral";

// WiFi
// Shows current WiFi runtime state and saves station credentials for next boot.

const char WIFI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Settings</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:500px;margin:0 auto}
h1{font-size:1.5em;text-align:center}
h2{font-size:1.35em;color:#8af;margin:18px 0 8px}
label{display:block;margin-top:10px;font-size:.85em;color:#aaa}
.sectionHead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:16px}
.sectionHead h2{margin:0}
.field{display:grid;grid-template-columns:118px 1fr;gap:10px;align-items:center;margin-top:10px;color:#aaa;font-size:.85em}
.field span{text-align:right}
.field input{margin-top:0}
input,select{width:100%;box-sizing:border-box;padding:8px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:.95em;margin-top:3px}
hr{border:none;border-top:1px solid #333;margin:22px 0}
button{margin-top:12px;padding:10px 24px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
.actions{display:flex;justify-content:center;gap:10px}
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
<div class="sectionHead"><h2>Available Networks</h2><button id="scan" onclick="scan()">Scan</button></div>
<div id="networks"><div class="network muted">Scanning...</div></div>
<label class="field"><span>Network</span><input type="text" id="staSsid" placeholder="SSID"></label>
<label class="field"><span>Password</span><input type="password" id="staPw" placeholder="open networks can be blank"></label>
<div class="actions"><button onclick="connect()">Connect &amp; Reboot</button></div>
<hr>
<h2>Local Hotspot Mode</h2>
<label class="field"><span>AP Name</span><input type="text" id="apSsid"></label>
<label class="field"><span>AP Password</span><input type="password" id="apPw" placeholder="blank keeps current"></label>
<div class="actions"><button onclick="saveAp()">Save AP &amp; Reboot</button></div>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function $(id){return document.getElementById(id);}
function reportFieldMismatch(page,field,configValue,acceptedValue,reason){
  fetch('/api/field-mismatch',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({page:page,field:field,configValue:String(configValue),acceptedValue:String(acceptedValue),reason:reason})
  }).catch(function(){});
}
function setFieldFromConfig(page,id,field,configValue,fieldValue){
  var el=$(id), wanted=fieldValue===undefined||fieldValue===null?'':String(fieldValue);
  el.value=wanted;
  var rejected=el.value!==wanted;
  var invalid=el.checkValidity&&!el.checkValidity();
  if(rejected||invalid){
    reportFieldMismatch(page,field,configValue,el.value,invalid?(el.validationMessage||'invalid value'):'browser rejected value');
  }
}
function setStatus(s){$('st').textContent=s||'';}
function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    setFieldFromConfig('wifi','staSsid','staSsid',d.staSsid,d.staSsid||'');
    setFieldFromConfig('wifi','apSsid','apSsid',d.apSsid,d.apSsid||'');
  }).catch(function(){});
}
function signalLevel(rssi){
  if(rssi>=-55)return 4;
  if(rssi>=-67)return 3;
  if(rssi>=-75)return 2;
  return 1;
}
function selectNetwork(button,ssid){
  $('staSsid').value=ssid;
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
    var list=$('networks');
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
  var body={ssid:$('staSsid').value,password:$('staPw').value};
  fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(body)
  }).then(function(r){return r.json();}).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(){setStatus('Connect failed');});
}
function saveAp(){
  var body={apSsid:$('apSsid').value};
  var pw=$('apPw').value;
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

// Directory viewer

const char CONFIG_JSON_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Directory</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:640px;margin:0 auto}
h1{font-size:1.5em;text-align:center}h2{font-size:1em;color:#8af;margin:18px 0 6px}
table{width:100%;border-collapse:collapse;table-layout:fixed}
th,td{border-bottom:1px solid #333;padding:8px;text-align:left}
th{color:#8af;font-weight:600;background:#1a2a3a}
th.size{text-align:right}
td.size{text-align:right;font-family:monospace;color:#ccc;width:24%}
td.del{text-align:right;width:56px}
button.file{background:none;border:0;color:#6af;padding:0;font:inherit;text-align:left;cursor:pointer}
button.del{padding:6px 10px;font-size:.85rem;background:#733;color:#fff;border:0;border-radius:5px;cursor:pointer}
tfoot td{color:#aaa;font-size:.9em;background:#1a2a3a}
.tfoot-gap td{height:40px;border-bottom:none!important;padding:0;background:transparent}
#st{margin-top:10px;font-size:.9em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
.upload-row{margin-top:16px;display:flex;gap:8px;align-items:center}
input[type=file]{background:#222;color:#eee;border:1px solid #444;border-radius:5px;padding:5px;font-size:.85em;flex:1;min-width:0}
button.upload{padding:6px 14px;font-size:.85rem;background:#3a9;color:#fff;border:0;border-radius:5px;cursor:pointer;white-space:nowrap}
button.cancel{padding:6px 10px;font-size:.85rem;background:#444;color:#fff;border:0;border-radius:5px;cursor:pointer}
.upload-warn{margin-top:8px;background:#2a1a1a;border:1px solid #633;padding:8px 12px;border-radius:5px;font-size:.9em;color:#f88;display:flex;gap:8px;align-items:center;flex-wrap:wrap}
#upload-st{margin-top:6px;font-size:.9em;min-height:1em}
</style></head>
<body>
<h1>Directory</h1>
<table>
  <thead><tr><th>Filename</th><th class="size">Size</th><th class="del"></th></tr></thead>
  <tbody id="files"><tr><td colspan="3">Loading&#8230;</td></tr></tbody>
  <tfoot id="totals"></tfoot>
</table>
<div class="upload-row"><input type="file" id="upload-input"><button class="upload" onclick="uploadFile()">Upload</button></div>
<div id="upload-warning" style="display:none" class="upload-warn"><span id="upload-warn-msg"></span><button class="del" onclick="confirmOverwrite()">Overwrite</button><button class="cancel" onclick="cancelUpload()">Cancel</button></div>
<div id="upload-st"></div>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
var currentFiles=[];
function $(id){return document.getElementById(id);}
function setStatus(s){$('st').textContent=s||'';}
function setUploadSt(s,ok){var el=$('upload-st');el.textContent=s||'';el.style.color=ok===false?'#f88':'#8d8';}
function deleteFile(name){
  if(!confirm('Delete '+name+'?'))return;
  fetch('/api/file?name='+encodeURIComponent(name),{method:'DELETE'}).then(function(r){
    if(!r.ok)throw new Error('Delete failed');
    return r.json();
  }).then(function(){
    setStatus('Deleted '+name);
    loadDirectory();
  }).catch(function(){setStatus('Delete failed');});
}
function uploadFile(){
  var input=$('upload-input');
  if(!input.files.length){setUploadSt('No file selected',false);return;}
  var file=input.files[0];
  $('upload-warning').style.display='none';
  var exists=currentFiles.some(function(f){return f.name===file.name;});
  if(exists){
    $('upload-warn-msg').textContent='"'+file.name+'" already exists.';
    $('upload-warning').style.display='';
    return;
  }
  doUpload(file);
}
function confirmOverwrite(){
  var input=$('upload-input');
  $('upload-warning').style.display='none';
  if(input.files.length)doUpload(input.files[0]);
}
function cancelUpload(){
  $('upload-warning').style.display='none';
  $('upload-input').value='';
  setUploadSt('');
}
function doUpload(file){
  setUploadSt('Uploading '+file.name+'...',true);
  var fd=new FormData();
  fd.append('file',file,file.name);
  fetch('/api/file/upload',{method:'POST',body:fd}).then(function(r){
    return r.json();
  }).then(function(d){
    setUploadSt(d.message||d.error,!d.error);
    if(!d.error){$('upload-input').value='';loadDirectory();}
  }).catch(function(){setUploadSt('Upload failed',false);});
}
function loadDirectory(){
fetch('/api/files').then(function(r){return r.json();}).then(function(d){
  currentFiles=d.files||[];
  var body=$('files');body.innerHTML='';
  (d.files||[]).forEach(function(file){
    var tr=document.createElement('tr');
    var name=document.createElement('td');
    var button=document.createElement('button');
    button.className='file';
    button.type='button';
    button.textContent=file.name;
    button.onclick=function(){window.location.href='/view?name='+encodeURIComponent(file.name);};
    name.appendChild(button);
    var size=document.createElement('td');
    size.className='size';
    size.textContent=file.size.toLocaleString();
    var del=document.createElement('td');
    del.className='del';
    var delButton=document.createElement('button');
    delButton.className='del';
    delButton.type='button';
    delButton.textContent='del';
    delButton.onclick=function(){deleteFile(file.name);};
    del.appendChild(delButton);
    tr.appendChild(name);
    tr.appendChild(size);
    tr.appendChild(del);
    body.appendChild(tr);
  });
  if(!body.children.length)body.innerHTML='<tr><td colspan="3">No files found</td></tr>';
  var totals=$('totals');totals.innerHTML='';
  if(d.total){
    var free=d.total-d.used;
    totals.innerHTML=
      '<tr class="tfoot-gap"><td colspan="3"></td></tr>'+
      '<tr><td>Used</td><td class="size">'+d.used.toLocaleString()+'</td><td></td></tr>'+
      '<tr><td>Available</td><td class="size">'+free.toLocaleString()+'</td><td></td></tr>';
  }
}).catch(function(){setStatus('Directory load failed');});
}
loadDirectory();
</script>
</body></html>
)rawliteral";

const char TIME_SYNC_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Time Sync</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:560px;margin:0 auto}
h1{font-size:2rem;margin:0 0 6px;text-align:center}
p{font-size:1.05rem;line-height:1.5;color:#ccc}
.now{margin:16px 0;font-size:1.05rem;line-height:1.5}
.timetable{width:100%;border-collapse:collapse;table-layout:fixed}
.timetable th,.timetable td{border-bottom:1px solid #333;padding:6px 8px}
.timetable tr:last-child td{border-bottom:0}
.timetable th{color:#8af;font-size:.9rem;font-weight:normal;text-align:center}
.timetable .label{width:18%;color:#aaa;text-align:right;white-space:nowrap}
.timetable .value{font-family:monospace;font-size:1.05rem;text-align:center;white-space:nowrap}
.timetable input{width:100%;box-sizing:border-box;padding:7px;background:#202020;color:#eee;
  border:1px solid #555;border-radius:5px;font-family:monospace;font-size:1rem;text-align:center}
.timetable-gap td{height:.8em;border-bottom:none!important;padding:0}
.timetable .narrow{width:80%;display:block;margin:0 auto}
button{margin-top:10px;padding:14px 28px;font-size:1.1rem;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button:active{background:#2a8}
.actions{display:flex;justify-content:center;gap:10px}
#st{margin-top:12px;font-size:1rem;color:#8d8;min-height:1.4em}
a{color:#6af;font-size:1rem}
</style></head>
<body>
<h1>Time Sync</h1>
<p>This sets the real-time clock to the current date and time reported by this browser.</p>
<div class="now">
  <table class="timetable">
    <tr><th class="label"></th><th>Device</th><th>Browser</th></tr>
    <tr><td class="label">Time</td><td class="value" id="deviceTime">--</td><td class="value" id="browserTime">--</td></tr>
    <tr><td class="label">Date</td><td class="value" id="deviceDate">--</td><td class="value" id="browserDate">--</td></tr>
    <tr class="timetable-gap"><td colspan="3"></td></tr>
    <tr><td class="label"></td><th colspan="2">Device</th></tr>
    <tr><td class="label">Timezone</td><td colspan="2"><input type="text" id="deviceTimezone" maxlength="39" class="narrow"></td></tr>
    <tr><td class="label">UTC offset</td><td colspan="2"><input type="number" id="deviceOffset" min="-840" max="840" step="1" class="narrow"></td></tr>
  </table>
</div>
<div class="actions"><button onclick="syncTime()">Synchronize Clocks</button></div>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function $(id){return document.getElementById(id);}
function reportFieldMismatch(page,field,configValue,acceptedValue,reason){
  fetch('/api/field-mismatch',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({page:page,field:field,configValue:String(configValue),acceptedValue:String(acceptedValue),reason:reason})
  }).catch(function(){});
}
function setFieldFromConfig(page,id,field,configValue,fieldValue){
  var el=$(id), wanted=fieldValue===undefined||fieldValue===null?'':String(fieldValue);
  el.value=wanted;
  var rejected=el.value!==wanted;
  var invalid=el.checkValidity&&!el.checkValidity();
  if(rejected||invalid){
    reportFieldMismatch(page,field,configValue,el.value,invalid?(el.validationMessage||'invalid value'):'browser rejected value');
  }
}
function browserTimezone(){
  try{return Intl.DateTimeFormat().resolvedOptions().timeZone||'UTC';}
  catch(e){return 'UTC';}
}
function browserOffsetMinutes(){return -new Date().getTimezoneOffset();}
function showNow(){
  var n=new Date();
  $('browserDate').textContent=formatDate(n);
  $('browserTime').textContent=formatTime(n);
}
function populateDeviceFieldsFromBrowser(n){
  $('browserDate').textContent=formatDate(n);
  $('browserTime').textContent=formatTime(n);
  $('deviceDate').textContent=formatDate(n);
  $('deviceTime').textContent=formatTime(n);
  $('deviceTimezone').value=browserTimezone();
  $('deviceOffset').value=browserOffsetMinutes();
}
function showDeviceNow(){
  fetch('/api/time').then(function(r){return r.json();}).then(function(d){
    $('deviceDate').textContent=d.date||'--';
    $('deviceTime').textContent=d.time||'--';
  }).catch(function(){
    $('deviceDate').textContent='unavailable';
    $('deviceTime').textContent='unavailable';
  });
}
function pad(n){return String(n).padStart(2,'0');}
function formatDate(n){return n.getFullYear()+'-'+pad(n.getMonth()+1)+'-'+pad(n.getDate());}
function formatTime(n){return pad(n.getHours())+':'+pad(n.getMinutes())+':'+pad(n.getSeconds());}
function loadTimeSettings(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    setFieldFromConfig('time-sync','deviceTimezone','timezone',d.timezone,d.timezone||browserTimezone());
    setFieldFromConfig('time-sync','deviceOffset','utcOffsetMinutes',d.utcOffsetMinutes,d.utcOffsetMinutes!==undefined?d.utcOffsetMinutes:browserOffsetMinutes());
  }).catch(function(){
    $('deviceTimezone').value=browserTimezone();
    $('deviceOffset').value=browserOffsetMinutes();
  });
}
showNow();showDeviceNow();loadTimeSettings();
setInterval(showNow,1000);
setInterval(showDeviceNow,1000);
function setStatus(s){$('st').textContent=s||'';}
function saveTimeSettings(){
  var off=parseInt($('deviceOffset').value,10);
  if(!isFinite(off)||off<-840||off>840){setStatus('Offset must be -840 to 840');return Promise.reject('offset');}
  return fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({timezone:$('deviceTimezone').value||browserTimezone(),utcOffsetMinutes:off})
  }).then(function(r){return r.json();});
}
function syncTime(){
  var n=new Date();
  populateDeviceFieldsFromBrowser(n);
  var body={year:n.getFullYear(),month:n.getMonth()+1,day:n.getDate(),hour:n.getHours(),minute:n.getMinutes(),second:n.getSeconds()};
  fetch('/api/time/sync',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(function(r){return r.json();}).then(function(d){
      if(d.error){setStatus(d.error);return;}
      return saveTimeSettings().then(function(t){setStatus(t.message||d.message);showDeviceNow();});
    })
    .catch(function(e){if(e!=='offset')setStatus('Time sync failed');});
}
</script>
</body></html>
)rawliteral";

// Messages

const char MESSAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Messages</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:560px;margin:0 auto}
h1{font-size:1.7em;margin-bottom:4px;text-align:center}
h2{font-size:1.1em;color:#8af;margin:22px 0 8px}
p{font-size:.95em;line-height:1.4;color:#bbb}
label{display:block;margin-top:10px;font-size:.9em;color:#aaa}
hr{border:none;border-top:1px solid #333;margin:22px 0}
.row{display:flex;gap:8px}
.row input{flex:1;min-width:0;box-sizing:border-box;padding:10px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:1.3em;margin-top:3px;font-family:monospace;text-align:center}
button{margin-top:14px;padding:11px 22px;font-size:1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer;margin-right:8px}
button.secondary{background:#357}
button:active{background:#2a8}
button.secondary:active{background:#246}
.actions{display:flex;justify-content:center;gap:10px}
.saveActions{margin-top:10px}
.actions button{margin-right:0}
#st{margin-top:12px;font-size:.9em;color:#8d8;min-height:1.2em}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Messages</h1>
<p>Each message is split across the three display panels. Test shows the message on the device for 5 seconds without saving.</p>

<h2>Splash Screen Text</h2>
<label>Shown at boot for 5 seconds</label>
<div class="row">
  <input id="s1" maxlength="4" placeholder="R1" oninput="nextPanel(this,'s2')">
  <input id="s2" maxlength="4" placeholder="R2" oninput="nextPanel(this,'s3')">
  <input id="s3" maxlength="4" placeholder="R3" oninput="trimPanel(this)">
</div>
<div class="actions"><button class="secondary" onclick="testMsg('s')">Test Splash</button></div>

<hr>
<h2>Countdown Text</h2>
<label>Shown when countdown or demo finishes</label>
<div class="row">
  <input id="f1" maxlength="4" placeholder="R1" oninput="nextPanel(this,'f2')">
  <input id="f2" maxlength="4" placeholder="R2" oninput="nextPanel(this,'f3')">
  <input id="f3" maxlength="4" placeholder="R3" oninput="trimPanel(this)">
</div>
<div class="actions">
  <button class="secondary" onclick="testMsg('f')">Test Final</button>
  <button class="secondary" onclick="runDemo()">Run Demo</button>
</div>

<div class="actions saveActions"><button onclick="save()">Save Messages</button></div>
<div id="st"></div>
<p><a href="/">&#8592; Home</a></p>
<script>
function $(id){return document.getElementById(id);}
function reportFieldMismatch(page,field,configValue,acceptedValue,reason){
  fetch('/api/field-mismatch',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({page:page,field:field,configValue:String(configValue),acceptedValue:String(acceptedValue),reason:reason})
  }).catch(function(){});
}
function cleanMessage(value){return String(value||'').replace(/[^\x20-\x7E]/g,'').slice(0,12);}
function trimPanel(el){el.value=cleanMessage(el.value).slice(0,4);}
function nextPanel(el,nextId){trimPanel(el);if(el.value.length>=4)$(nextId).focus();}
function msg(prefix){return cleanMessage(($(prefix+'1').value||'').slice(0,4)+($(prefix+'2').value||'').slice(0,4)+($(prefix+'3').value||'').slice(0,4));}
function split(prefix,value,field){
  var original=value||'';
  var cleaned=cleanMessage(original);
  if(String(original)!==cleaned){
    reportFieldMismatch('messages',field,original,cleaned,'message must be 12 printable ASCII chars');
  }
  $(prefix+'1').value=cleaned.slice(0,4);
  $(prefix+'2').value=cleaned.slice(4,8);
  $(prefix+'3').value=cleaned.slice(8,12);
}
function setStatus(s){$('st').textContent=s||'';}
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  split('s',d.splashMessage||'','splashMessage');
  split('f',d.finalMessage||'','finalMessage');
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

// View file

const char VIEW_FILE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>View File</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:720px;margin:0 auto}
h1{font-size:1.3em;text-align:center;word-break:break-all}
pre{background:#1a1a1a;padding:14px;border-radius:6px;overflow-x:auto;font-size:.88rem;line-height:1.5;color:#8f8;white-space:pre-wrap}
iframe{width:100%;height:80vh;border:none;border-radius:6px}
img{max-width:100%;border-radius:6px;display:block;margin:0 auto}
#st{margin-top:10px;font-size:.9em;color:#f88;min-height:1.2em;text-align:center}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1 id="title">&#8230;</h1>
<div id="viewer"></div>
<div id="st"></div>
<p><a href="/config">&#8592; Directory</a></p>
<script>
function $(id){return document.getElementById(id);}
function sorted(v){
  if(Array.isArray(v))return v.map(sorted);
  if(v&&typeof v==='object')return Object.keys(v).sort().reduce(function(o,k){o[k]=sorted(v[k]);return o;},{});
  return v;
}
function extOf(n){var d=n.lastIndexOf('.');return d>=0?n.substring(d+1).toLowerCase():'';}
var params=new URLSearchParams(window.location.search);
var name=params.get('name')||'';
document.title=name||'View File';
$('title').textContent=name||'(unknown)';
var url='/api/file?name='+encodeURIComponent(name);
var ext=extOf(name);
var viewer=$('viewer');
if(!name){
  $('st').textContent='No file specified.';
}else if(ext==='pdf'){
  var fr=document.createElement('iframe');
  fr.src=url;fr.title=name;
  viewer.appendChild(fr);
}else if(/^(png|jpg|jpeg|gif|svg)$/.test(ext)){
  var im=document.createElement('img');
  im.src=url;im.alt=name;
  viewer.appendChild(im);
}else{
  var pre=document.createElement('pre');
  pre.textContent='Loading...';
  viewer.appendChild(pre);
  fetch(url).then(function(r){
    if(!r.ok)throw new Error(r.status);
    return r.text();
  }).then(function(text){
    if(ext==='json'){try{text=JSON.stringify(sorted(JSON.parse(text)),null,2);}catch(e){}}
    pre.textContent=text;
  }).catch(function(e){pre.textContent='';$('st').textContent='Load failed: '+e.message;});
}
</script>
</body></html>
)rawliteral";

// Location

const char LOCATION_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Location</title><style>
body{font-family:sans-serif;padding:20px;background:#111;color:#eee;max-width:560px;margin:0 auto}
h1{font-size:2em;margin-bottom:14px;text-align:center}
.field{display:grid;grid-template-columns:110px 1fr;gap:10px;align-items:center;margin-top:14px;font-size:1.1em;color:#aaa}
.field span{text-align:right}
input{width:100%;min-width:0;box-sizing:border-box;padding:10px;background:#222;color:#eee;
  border:1px solid #444;border-radius:5px;font-size:1.15em;margin-top:5px}
button{margin-top:14px;padding:12px 24px;font-size:1.1em;background:#3a9;border:none;
  color:#fff;border-radius:8px;cursor:pointer}
button.secondary{background:#357}
button:active{background:#2a8}
button.secondary:active{background:#246}
.actions{display:flex;justify-content:center;gap:10px;flex-wrap:wrap}
#st{margin-top:12px;font-size:.9em;color:#8d8;min-height:1.2em;text-align:center}
.substatus{font-size:.9em;color:#f88;min-height:1em;text-align:center;margin-top:6px}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0}}.blinking{animation:blink 2s step-end infinite}
a{color:#6af;font-size:.9em}
</style></head>
<body>
<h1>Location</h1>
<label class="field"><span>Latitude</span><input type="text" id="lat" inputmode="decimal" pattern="-?[0-9]+(\.[0-9]+)?" placeholder="40.7128"></label>
<label class="field"><span>Longitude</span><input type="text" id="lon" inputmode="decimal" pattern="-?[0-9]+(\.[0-9]+)?" placeholder="-74.0060"></label>
<div class="actions">
  <button class="secondary" onclick="useBrowser()">Get Coordinates from Browser</button>
</div>
<div id="st-browser" class="substatus"></div>
<hr style="border-color:#333;margin:20px 0">
<label class="field"><span>ZIP code</span><input type="text" id="zip" inputmode="numeric" maxlength="5" pattern="[0-9]{5}" placeholder="10001"></label>
<div class="actions">
  <button class="secondary" onclick="computeZip()">Get Coordinates from Zipcode</button>
</div>
<div id="st-zip" class="substatus"></div>
<hr style="border-color:#333;margin:20px 0">
<div class="actions">
  <button onclick="save()">Save Location</button>
</div>
<div id="st"></div>
<p><a href="/settings">&#8592; Settings</a></p>
<script>
function $(id){return document.getElementById(id);}
function reportFieldMismatch(page,field,configValue,acceptedValue,reason){
  fetch('/api/field-mismatch',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({page:page,field:field,configValue:String(configValue),acceptedValue:String(acceptedValue),reason:reason})
  }).catch(function(){});
}
function setFieldFromConfig(page,id,field,configValue,fieldValue){
  var el=$(id), wanted=fieldValue===undefined||fieldValue===null?'':String(fieldValue);
  el.value=wanted;
  var rejected=el.value!==wanted;
  var invalid=el.checkValidity&&!el.checkValidity();
  if(rejected||invalid){
    reportFieldMismatch(page,field,configValue,el.value,invalid?(el.validationMessage||'invalid value'):'browser rejected value');
  }
}
function setStatus(s){$('st').textContent=s||'';}
var _subT={};
function showSubstatus(id,s,err){
  var el=$(id);clearTimeout(_subT[id]);
  el.textContent=s||'';el.className='substatus'+(err&&s?' blinking':'');
  if(err&&s)_subT[id]=setTimeout(function(){el.textContent='';el.className='substatus';},5000);
}
function setBrowserStatus(s,err){showSubstatus('st-browser',s,err);}
function setZipStatus(s,err){showSubstatus('st-zip',s,err);}
function jsonFetch(url,options){
  return fetch(url,options).then(function(r){
    return r.json().then(function(d){
      d._httpStatus=r.status;
      if(!r.ok&&!d.error)d.error='HTTP '+r.status;
      return d;
    });
  });
}
fetch('/api/config').then(function(r){
  if(!r.ok)throw new Error('HTTP '+r.status);
  return r.json();
}).then(function(d){
  setFieldFromConfig('location','zip','zipcode',d.zipcode,d.zipcode||'');
  setFieldFromConfig('location','lat','latitude',d.latitude,d.latitude!==undefined?d.latitude:'');
  setFieldFromConfig('location','lon','longitude',d.longitude,d.longitude!==undefined?d.longitude:'');
}).catch(function(e){setStatus('Load failed: '+e.message);});
function useBrowser(){
  if(!navigator.geolocation){setBrowserStatus('Browser location unavailable',true);return;}
  setBrowserStatus('Requesting location...');
  navigator.geolocation.getCurrentPosition(function(pos){
    $('lat').value=pos.coords.latitude.toFixed(6);
    $('lon').value=pos.coords.longitude.toFixed(6);
    setBrowserStatus('');
  },function(){setBrowserStatus('Location blocked or unavailable',true);},
  {enableHighAccuracy:false,timeout:10000,maximumAge:600000});
}
function validNumber(value,min,max){
  if(!/^-?[0-9]+(\.[0-9]+)?$/.test(value))return false;
  var n=parseFloat(value);
  return isFinite(n)&&n>=min&&n<=max;
}
function validZip(value){return /^[0-9]{5}$/.test(value);}
function computeZip(){
  var zip=$('zip').value;
  if(!validZip(zip)){setZipStatus('ZIP code must be 5 digits',true);return;}
  setZipStatus('Looking up...');
  jsonFetch('/api/zipcode/lookup?zip='+encodeURIComponent(zip)).then(function(d){
    if(d.error){setZipStatus(d.error+' - coordinates left unchanged',true);return;}
    $('lat').value=Number(d.latitude).toFixed(6);
    $('lon').value=Number(d.longitude).toFixed(6);
    setZipStatus('');
  }).catch(function(e){setZipStatus('ZIP lookup failed: '+e.message+' - coordinates left unchanged',true);});
}
function save(){
  var zip=$('zip').value;
  if(zip&&!validZip(zip)){setStatus('ZIP code must be 5 digits');return;}
  if(!validNumber($('lat').value,-90,90)){setStatus('Latitude must be -90 to 90');return;}
  if(!validNumber($('lon').value,-180,180)){setStatus('Longitude must be -180 to 180');return;}
  jsonFetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      zipcode:zip,
      latitude:parseFloat($('lat').value),
      longitude:parseFloat($('lon').value)
    })
  }).then(function(d){
    setStatus(d.message||d.error);
  }).catch(function(e){setStatus('Save failed: '+e.message);});
}
</script>
</body></html>
)rawliteral";

