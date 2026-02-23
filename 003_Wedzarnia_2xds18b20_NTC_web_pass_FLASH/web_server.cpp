// web_server.cpp - FIXED
// Poprawki vs oryginał GitHub:
// 1. +#include <esp_littlefs.h> + #define EXT_FLASH_PARTITION_LABEL
// 2. handleFlashInfo()    - esp_littlefs_info() zamiast LittleFS.totalBytes()=0
// 3. handleFlashFormat()  - esp_littlefs_format(etykieta) zamiast begin(true)
// 4. handleApiFilesList() - total/used jako LICZBY w JSON (nie stringi!)
// 5. handleSysInfoJson()  - esp_littlefs_info() zamiast LittleFS.totalBytes()=0
#include "web_server.h"
#include "config.h"
#include "state.h"
#include "storage.h"
#include "process.h"
#include "outputs.h"
#include "sensors.h"
#include <WiFi.h>
#include <Update.h>
#include "FS.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <esp_littlefs.h>  // <<< NOWE

#ifndef EXT_FLASH_PARTITION_LABEL
#define EXT_FLASH_PARTITION_LABEL "extfs"  // <<< NOWE
#endif

// =================================================================
// [NEW] AUTORYZACJA
// =================================================================
static bool requireAuth() {
    if (!server.authenticate(storage_get_auth_user(), storage_get_auth_pass())) {
        server.requestAuthentication(BASIC_AUTH, "Wedzarnia", "Wymagane logowanie");
        return false;
    }
    return true;
}
// =================================================================
// GŁÓWNA STRONA "/"
// =================================================================
static const char HTML_TEMPLATE_MAIN[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Wędzarnia</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{
font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;
background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);
color:#eee;
padding:15px;
min-height:100vh;
}
.container{max-width:800px;margin:0 auto;}
.header{
background:linear-gradient(135deg,#d32f2f 0%,#c62828 100%);
padding:20px;border-radius:15px;margin-bottom:20px;
box-shadow:0 8px 20px rgba(211,47,47,0.3);text-align:center;
}
.header h1{font-size:2em;margin:0;text-shadow:2px 2px 4px rgba(0,0,0,0.5);}
.header .version{font-size:0.8em;opacity:0.9;margin-top:5px;}
.status-card{
background:rgba(255,255,255,0.05);backdrop-filter:blur(10px);
border:1px solid rgba(255,255,255,0.1);padding:20px;
border-radius:15px;margin-bottom:15px;
box-shadow:0 8px 32px rgba(0,0,0,0.3);
}
.temp-display{display:flex;justify-content:space-around;flex-wrap:wrap;gap:15px;margin-bottom:15px;}
.temp-box{
flex:1;min-width:120px;background:rgba(0,0,0,0.3);padding:15px;
border-radius:12px;text-align:center;border:2px solid transparent;transition:all 0.3s;
}
.temp-box:hover{transform:translateY(-3px);border-color:rgba(255,255,255,0.3);}
.temp-box .label{font-size:0.9em;opacity:0.8;margin-bottom:8px;}
.temp-box .value{font-size:2.2em;font-weight:bold;font-family:'Courier New',monospace;}
.temp-chamber{border-color:#ff9800;}.temp-chamber .value{color:#ff9800;}
.temp-meat{border-color:#ffc107;}.temp-meat .value{color:#ffc107;}
.temp-target{border-color:#00bcd4;}.temp-target .value{color:#00bcd4;}
.status-info{
display:flex;justify-content:space-between;flex-wrap:wrap;gap:10px;
font-size:0.95em;padding:10px;background:rgba(0,0,0,0.2);border-radius:8px;
}
.status-badge{display:inline-block;padding:5px 12px;border-radius:20px;font-weight:600;font-size:0.9em;}
.status-idle{background:#666;}
.status-auto{background:#4caf50;}
.status-manual{background:#00bcd4;}
.status-pause{background:#ff9800;}
.status-error{background:#f44336;}
.timer-card{
background:rgba(76,175,80,0.1);border:2px solid #4caf50;padding:15px;
border-radius:12px;margin-bottom:15px;display:none;
}
.timer-card.active{display:block;animation:fadeIn 0.3s;}
@keyframes fadeIn{from{opacity:0;transform:translateY(-10px);}to{opacity:1;transform:translateY(0);}}
.timer-row{display:flex;justify-content:space-around;flex-wrap:wrap;gap:10px;margin-top:10px;}
.timer-item{text-align:center;}
.timer-item .label{font-size:0.85em;opacity:0.8;margin-bottom:5px;}
.timer-item .time{font-size:1.8em;font-weight:bold;font-family:'Courier New',monospace;}
.time-elapsed{color:#4caf50;}
.time-remaining{color:#ff9800;}
.section{
background:rgba(255,255,255,0.05);backdrop-filter:blur(10px);
border:1px solid rgba(255,255,255,0.1);padding:20px;
border-radius:12px;margin-bottom:15px;box-shadow:0 4px 16px rgba(0,0,0,0.2);
}
.section h3{color:#fff;margin-bottom:15px;padding-bottom:10px;border-bottom:2px solid rgba(255,255,255,0.2);font-size:1.3em;}
button{
padding:12px 20px;margin:5px;border:none;border-radius:8px;
font-size:1em;font-weight:600;cursor:pointer;transition:all 0.3s;
box-shadow:0 4px 12px rgba(0,0,0,0.3);
}
button:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.4);}
button:active{transform:translateY(0);}
.btn-start{background:linear-gradient(135deg,#4caf50,#45a049);color:white;}
.btn-stop{background:linear-gradient(135deg,#f44336,#e53935);color:white;}
.btn-action{background:linear-gradient(135deg,#2196f3,#1976d2);color:white;}
button:disabled{opacity:0.5;cursor:not-allowed;transform:none !important;}
input,select{
padding:10px;margin:5px;background:rgba(0,0,0,0.3);color:#fff;
border:2px solid rgba(255,255,255,0.2);border-radius:8px;font-size:1em;
}
input:focus,select:focus{outline:none;border-color:#2196f3;box-shadow:0 0 10px rgba(33,150,243,0.3);}
input[type="range"]{width:100%;max-width:200px;}
input[type="number"]{width:80px;}
.control-group{
display:flex;align-items:center;flex-wrap:wrap;gap:10px;margin:10px 0;
padding:12px;background:rgba(0,0,0,0.2);border-radius:8px;
}
.control-group label{font-weight:600;margin-right:10px;}
.footer{margin-top:20px;padding-top:20px;border-top:1px solid rgba(255,255,255,0.1);}
.footer-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px;}
.footer-link{
display:flex;align-items:center;justify-content:center;gap:7px;padding:12px 10px;
background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);
border-radius:10px;color:#90caf9;text-decoration:none;font-size:0.9em;
font-weight:500;transition:all 0.25s;white-space:nowrap;
}
.footer-link:hover{background:rgba(33,150,243,0.15);border-color:rgba(33,150,243,0.4);color:#fff;transform:translateY(-2px);box-shadow:0 4px 14px rgba(0,0,0,0.3);}
.footer-link .fi{font-size:1.1em;flex-shrink:0;}
.profile-info{margin-top:10px;padding:10px;background:rgba(33,150,243,0.1);border-left:4px solid #2196f3;border-radius:4px;font-size:0.9em;}
.readonly-banner{background:rgba(255,152,0,0.15);border:1px solid #ff9800;padding:10px 15px;border-radius:8px;margin-bottom:15px;font-size:0.9em;text-align:center;}
.readonly-banner a{color:#ff9800;font-weight:bold;}
@media(max-width:600px){
.header h1{font-size:1.5em;}.temp-box{min-width:100px;}.temp-box .value{font-size:1.8em;}
button{padding:10px 15px;font-size:0.9em;}.footer-grid{grid-template-columns:repeat(auto-fill,minmax(110px,1fr));}
}
</style>
</head>
<body>
<div class="container">
<div class="header">
<h1 id="fw-title">🔥 Wędzarnia IoT</h1>
<div class="version" id="fw-ver">wczytywanie...</div>
</div>
<div class="readonly-banner" id="readonlyBanner" style="display:none;">
👁️ Tryb podglądu – <a href="/auth/login">zaloguj się</a>aby sterować
</div>
<div class="status-card">
<div class="temp-display">
<div class="temp-box temp-chamber">
<div class="label">🌡️ Komora (śr.)</div>
<div class="value" id="temp-chamber">--°C</div>
<div style="font-size:0.7em;opacity:0.7;margin-top:5px;">
DS1: <span id="temp-ch1">--</span>°C | DS2: <span id="temp-ch2">--</span>°C
</div>
</div>
<div class="temp-box temp-meat">
<div class="label">🍖 Mięso (NTC)</div>
<div class="value" id="temp-meat">--°C</div>
</div>
<div class="temp-box temp-target">
<div class="label">🎯 Zadana</div>
<div class="value" id="temp-target">--°C</div>
</div>
</div>
<div class="status-info" id="status-info">
<div>Status:<span class="status-badge" id="status-badge">Ładowanie...</span></div>
<div>Moc:<span id="power-mode">-</span></div>
<div>Wentylator:<span id="fan-mode">-</span></div>
<div>💨 Dym:<span id="smoke-level">0%</span></div>
</div>
</div>
<div class="timer-card" id="timer-section">
<div style="text-align:center;margin-bottom:10px;"><strong id="step-name">-</strong></div>
<div class="timer-row">
<div class="timer-item">
<div class="label">🕒 Upłynęło</div>
<div class="time time-elapsed" id="timer-elapsed">00:00:00</div>
</div>
<div class="timer-item" id="countdown-section">
<div class="label">⏳ Pozostało</div>
<div class="time time-remaining" id="timer-remaining">00:00:00</div>
</div>
<div class="timer-item" id="process-total-section">
<div class="label">⏱️ Do końca</div>
<div class="time time-remaining" id="process-remaining">--:--:--</div>
</div>
</div>
<div style="text-align:center;margin-top:10px;">
<button class="btn-action" onclick="authAction('/timer/reset','Resetuj czas?')">↻ Resetuj Czas</button>
<button class="btn-action" id="nextStepBtn" style="display:none;" onclick="authAction('/auto/next_step','Pominąć krok?')">⏭️ Następny krok</button>
</div>
</div>
<div class="section">
<h3>⚡ Sterowanie</h3>
<div style="text-align:center;">
<button class="btn-action" onclick="authAction('/mode/manual')">🎮 Tryb Manualny</button>
<button class="btn-start" onclick="authAction('/auto/start')">▶️ Start AUTO</button>
<button class="btn-stop" onclick="authAction('/auto/stop','Zatrzymać proces?')">⏹️ Stop</button>
</div>
<div class="profile-info">Aktywny profil:<strong id="active-profile">Brak</strong></div>
</div>
<div class="section">
<h3>📋 Wybór Profilu</h3>
<div class="control-group">
<label>Źródło:</label>
<select id="profileSource" onchange="sourceChanged()">
<option value="flash" selected>💾 Flash</option>
<option value="github">☁️ GitHub</option>
</select>
</div>
<div class="control-group">
<select id="profileList" style="flex:1;"></select>
</div>
<div style="text-align:center;">
<button class="btn-action" onclick="selectProfile()">✅ Ustaw aktywny</button>
<button class="btn-action" onclick="editProfile()">✏️ Edytuj</button>
<button class="btn-action" id="reloadFlashBtn" onclick="reloadProfiles()">🔄 Odczytaj</button>
</div>
</div>
<div class="section">
<h3>🎛️ Ustawienia Manualne</h3>
<div class="control-group">
<label>Temperatura:</label>
<input id="tSet" type="number" value="70" min="20" max="130"><span>°C</span>
<button class="btn-action" onclick="setT()">✅ Ustaw</button>
</div>
<div class="control-group">
<label>Moc grzałek:</label>
<select id="power">
<option value="1">1 grzałka</option>
<option value="2" selected>2 grzałki</option>
<option value="3">3 grzałki</option>
</select>
<button class="btn-action" onclick="setP()">✅ Ustaw</button>
</div>
<div class="control-group">
<label>Dym PWM:</label>
<input id="smoke" type="range" min="0" max="255" value="0">
<span id="smokeVal" style="min-width:50px;font-weight:bold;">0</span>
<button class="btn-action" onclick="setS()">✅ Ustaw</button>
</div>
<div class="control-group">
<label>Termoobieg:</label>
<select id="fan">
<option value="0">OFF</option>
<option value="1" selected>ON</option>
<option value="2">CYKL</option>
</select>
<span style="margin-left:10px;">ON:</span><input id="fon" type="number" value="10" style="width:60px;"><span>s</span>
<span style="margin-left:10px;">OFF:</span><input id="foff" type="number" value="60" style="width:60px;"><span>s</span>
<button class="btn-action" onclick="setF()">✅ Ustaw</button>
</div>
</div>
<div class="footer">
<div class="footer-grid">
<a class="footer-link" href="/creator"><span class="fi">📝</span>Nowy Profil</a>
<a class="footer-link" href="/sensors"><span class="fi">🔧</span>Czujniki</a>
<a class="footer-link" href="/wifi"><span class="fi">📶</span>WiFi</a>
<a class="footer-link" href="/update"><span class="fi">📦</span>OTA Update</a>
<a class="footer-link" href="/auth/set"><span class="fi">🔑</span>Zmień hasło</a>
<a class="footer-link" href="/flash"><span class="fi">💾</span>Pamięć Flash</a>
<a class="footer-link" href="/files"><span class="fi">📂</span>Pliki</a>
<a class="footer-link" href="/sysinfo"><span class="fi">ℹ️</span>System</a>
</div>
</div>
</div>
<script>
let currentProfileSource = 'flash';
document.getElementById('smoke').oninput = function(){
document.getElementById('smokeVal').textContent = this.value;
};
function formatTime(seconds){
if(isNaN(seconds)||seconds<0)return "--:--:--";
const h=Math.floor(seconds/3600);
const m=Math.floor((seconds%3600)/60);
const s=seconds%60;
return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
}
function authAction(url,confirmMsg){
if(confirmMsg&&!confirm(confirmMsg))return;
fetch(url).then(r=>{
if(r.status===401){alert('Wymagane zalogowanie. Odśwież stronę i zaloguj się.');}
fetchStatus();
}).catch(e=>console.error(e));
}
function fetchStatus(){
fetch('/status')
.then(r=>r.json())
.then(data=>{
document.getElementById('temp-chamber').textContent=data.tChamber.toFixed(1)+'°C';
document.getElementById('temp-ch1').textContent=data.tChamber1.toFixed(1);
document.getElementById('temp-ch2').textContent=data.tChamber2.toFixed(1);
document.getElementById('temp-meat').textContent=data.tMeat.toFixed(1)+'°C';
document.getElementById('temp-target').textContent=data.tSet.toFixed(1)+'°C';
let statusClass='status-idle';
let statusText=data.mode;
if(data.mode.includes('PAUZA')||data.mode.includes('AWARIA')){statusClass='status-pause';statusText=data.mode;}
else if(data.mode.includes('ERROR')){statusClass='status-error';statusText='BŁĄD';}
else if(data.mode==='AUTO'){statusClass='status-auto';}
else if(data.mode==='MANUAL'){statusClass='status-manual';}
const badge=document.getElementById('status-badge');
badge.className='status-badge '+statusClass;
badge.textContent=statusText;
document.getElementById('power-mode').textContent=data.powerModeText;
document.getElementById('fan-mode').textContent=data.fanModeText;
document.getElementById('smoke-level').textContent=Math.round((data.smokePwm/255)*100)+'%';
const timerSection=document.getElementById('timer-section');
if(data.mode==='AUTO'||data.mode==='MANUAL'){
timerSection.classList.add('active');
document.getElementById('timer-elapsed').textContent=formatTime(data.elapsedTimeSec);
if(data.mode==='AUTO'){
document.getElementById('step-name').textContent='Krok:'+data.stepName;
document.getElementById('countdown-section').style.display='block';
document.getElementById('nextStepBtn').style.display='inline-block';
document.getElementById('timer-remaining').textContent=formatTime(Math.max(0,data.stepTotalTimeSec-data.elapsedTimeSec));
const ps=document.getElementById('process-total-section');
if(data.remainingProcessTimeSec>0){
ps.style.display='block';
document.getElementById('process-remaining').textContent=formatTime(data.remainingProcessTimeSec);
}else{ps.style.display='none';}
}else{
document.getElementById('step-name').textContent='Tryb Manualny';
document.getElementById('countdown-section').style.display='none';
document.getElementById('nextStepBtn').style.display='none';
}
}else{
timerSection.classList.remove('active');
}
let profileName=data.activeProfile.replace('/profiles/','').replace('github:','[GitHub] ');
document.getElementById('active-profile').textContent=profileName;
})
.catch(e=>console.error('Status fetch error:',e));
}
function setT(){authAction('/manual/set?tSet='+document.getElementById('tSet').value);}
function setP(){authAction('/manual/power?val='+document.getElementById('power').value);}
function setS(){authAction('/manual/smoke?val='+document.getElementById('smoke').value);}
function setF(){
authAction('/manual/fan?mode='+document.getElementById('fan').value+
'&on='+document.getElementById('fon').value+
'&off='+document.getElementById('foff').value);
}
function sourceChanged(){
currentProfileSource=document.getElementById('profileSource').value;
document.getElementById('reloadFlashBtn').style.display=currentProfileSource==='flash'?'inline-block':'none';
loadProfiles();
}
function loadProfiles(){
const url=currentProfileSource==='flash'?'/api/profiles':'/api/github_profiles';
fetch(url).then(r=>r.json()).then(profiles=>{
const list=document.getElementById('profileList');
list.innerHTML='';
profiles.forEach(p=>{
const name=p.replace('/profiles/','');
const opt=document.createElement('option');
opt.value=name;opt.textContent=name;
list.appendChild(opt);
});
});
}
function selectProfile(){
const name=document.getElementById('profileList').value;
if(!name)return;
const source=currentProfileSource==='flash'?'sd':currentProfileSource;
fetch('/profile/select?name='+name+'&source='+source)
.then(r=>{
if(r.status===401){alert('Wymagane zalogowanie.');return;}
return r.text();
})
.then(msg=>{if(msg){alert(msg);fetchStatus();}});
}
function editProfile(){
const name=document.getElementById('profileList').value;
if(!name)return;
const source=currentProfileSource==='flash'?'sd':currentProfileSource;
window.location.href='/creator?edit='+name+'&source='+source;
}
function reloadProfiles(){
if(currentProfileSource==='flash'){
fetch('/profile/reload').then(r=>{if(!r.ok)alert('Błąd odczytu Flash!');loadProfiles();fetchStatus();});
}
}
loadProfiles();
fetchStatus();
setInterval(fetchStatus,1000);
fetch('/api/sysinfo').then(r=>r.json()).then(d=>{
const t=document.getElementById('fw-title');
const v=document.getElementById('fw-ver');
function updateHeader(){const time=new Date().toLocaleTimeString('pl-PL');if(t)t.textContent='🔥 '+d.fw_name+'\u00A0'+d.fw_version+'\u00A0\u00A0\u00A0🕒 '+time;}
updateHeader();setInterval(updateHeader,1000);
if(v)v.textContent='by '+d.fw_author;
}).catch(()=>{});
</script>
</body>
</html>)rawliteral";

// =================================================================
// KREATOR PROFILI
// =================================================================
static const char HTML_TEMPLATE_CREATOR[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Kreator/Edytor Profili</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header">
<h2 id="creator-title">📝 Kreator Profili</h2>
</div>
<div class="card">
<h3>Krok <span id="step-counter">1</span></h3>
<label>Nazwa kroku</label><input type="text" id="stepName" value="Krok 1">
<label>Temp. komory(°C)</label><input type="number" id="stepTSet" value="60">
<label>Temp. mięsa(°C)</label><input type="number" id="stepTMeat" value="0">
<label>Min. czas(minuty)</label><input type="number" id="stepMinTime" value="60">
<label>Tryb mocy(1–3)</label><input type="number" id="stepPowerMode" value="2" min="1" max="3">
<label>Moc dymu(0–255)</label><input type="number" id="stepSmoke" value="150" min="0" max="255">
<label>Tryb wentylatora</label>
<select id="stepFanMode">
<option value="0">OFF</option>
<option value="1" selected>ON</option>
<option value="2">CYKL</option>
</select>
<label>Czas ON wentylatora(s)</label><input type="number" id="stepFanOn" value="10">
<label>Czas OFF wentylatora(s)</label><input type="number" id="stepFanOff" value="60">
<div class="check-row">
<input type="checkbox" id="stepUseMeatTemp">
<span>Użyj temperatury mięsa</span>
</div>
<div class="btn-row">
<button id="addStepBtn" class="btn-add" onclick="addStep()">Dodaj krok</button>
</div>
</div>
<div class="card">
<h3>Zbudowany profil</h3>
<div id="steps-preview"></div>
<label>Nazwa pliku</label>
<input type="text" id="profileFilename" placeholder="np. boczek.prof">
<div class="btn-row">
<button class="btn-save" onclick="saveProfile()">💾 Zapisz na Flash</button>
<button class="btn-pc" onclick="saveProfileToPC()">💻 Zapisz na komputerze</button>
<button class="btn-clear" onclick="clearCreator()">🗑️ Wyczyść</button>
</div>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
let newProfileSteps=[];let stepCounter=1;let editIndex=-1;
document.addEventListener('DOMContentLoaded',function(){const params=new URLSearchParams(window.location.search);const profileToEdit=params.get('edit');const source=params.get('source')||'sd';if(profileToEdit){document.getElementById('creator-title').textContent='📝 Edytor Profilu:'+profileToEdit;document.getElementById('profileFilename').value=profileToEdit;document.getElementById('profileFilename').readOnly=true;fetch('/profile/get?name='+profileToEdit+'&source='+source).then(r=>r.json()).then(data=>{newProfileSteps=data;updatePreview();if(data.length>0){stepCounter=data.length+1;document.getElementById('step-counter').textContent=stepCounter;}})}});
function addStep(){const e={name:document.getElementById("stepName").value,tSet:document.getElementById("stepTSet").value,tMeat:document.getElementById("stepTMeat").value,minTime:document.getElementById("stepMinTime").value,powerMode:document.getElementById("stepPowerMode").value,smoke:document.getElementById("stepSmoke").value,fanMode:document.getElementById("stepFanMode").value,fanOn:document.getElementById("stepFanOn").value,fanOff:document.getElementById("stepFanOff").value,useMeatTemp:document.getElementById("stepUseMeatTemp").checked?1:0};if(editIndex===-1){newProfileSteps.push(e);stepCounter++}else{newProfileSteps[editIndex]=e;editIndex=-1}updatePreview();document.getElementById('step-counter').textContent=stepCounter;document.getElementById('stepName').value="Krok "+stepCounter;document.getElementById('addStepBtn').textContent='Dodaj krok';}
function updatePreview(){const e=document.getElementById("steps-preview");e.innerHTML="";newProfileSteps.forEach((t,n)=>{const o=document.createElement("div");o.className="step-preview";o.textContent=`Krok ${n+1}:${t.name};${t.tSet}°C;${t.minTime}min`;o.onclick=function(){loadStepForEdit(n)};e.appendChild(o)})}
function loadStepForEdit(e){const t=newProfileSteps[e];document.getElementById("stepName").value=t.name;document.getElementById("stepTSet").value=t.tSet;document.getElementById("stepTMeat").value=t.tMeat;document.getElementById("stepMinTime").value=t.minTime;document.getElementById("stepPowerMode").value=t.powerMode;document.getElementById("stepSmoke").value=t.smoke;document.getElementById("stepFanMode").value=t.fanMode;document.getElementById("stepFanOn").value=t.fanOn;document.getElementById("stepFanOff").value=t.fanOff;document.getElementById("stepUseMeatTemp").checked=1==t.useMeatTemp;editIndex=e;document.getElementById("step-counter").textContent=e+1;document.getElementById("addStepBtn").textContent="Aktualizuj krok";window.scrollTo(0,0)}
function clearCreator(){if(confirm("Wyczyścić kreator?")){newProfileSteps=[];stepCounter=1;editIndex=-1;document.getElementById("step-counter").textContent="1";document.getElementById("steps-preview").innerHTML="";document.getElementById("profileFilename").value="";document.getElementById("profileFilename").readOnly=false;document.getElementById("creator-title").textContent="📝 Kreator Profili"}}
function saveProfile(){const e=document.getElementById("profileFilename").value;if(!e)return alert("Wpisz nazwę pliku!");if(0===newProfileSteps.length)return alert("Dodaj przynajmniej jeden krok!");let t="# Profil\n";newProfileSteps.forEach(e=>{t+=`${e.name};${e.tSet};${e.tMeat};${e.minTime};${e.powerMode};${e.smoke};${e.fanMode};${e.fanOn};${e.fanOff};${e.useMeatTemp}\n`});const n=new URLSearchParams;n.append("filename",e);n.append("data",t);fetch("/profile/create",{method:"POST",body:n}).then(e=>e.text().then(t=>({ok:e.ok,text:t}))).then(({ok:e,text:t})=>{alert(t);e&&(window.location.href="/")})}
function saveProfileToPC(){const e=document.getElementById("profileFilename").value;if(!e)return alert("Wpisz nazwę pliku!");if(0===newProfileSteps.length)return alert("Dodaj przynajmniej jeden krok!");let t="# Profil\n";newProfileSteps.forEach(e=>{t+=`${e.name};${e.tSet};${e.tMeat};${e.minTime};${e.powerMode};${e.smoke};${e.fanMode};${e.fanOn};${e.fanOff};${e.useMeatTemp}\n`});const n=new Blob([t],{type:"text/plain;charset=utf-8"}),o=URL.createObjectURL(n),d=document.createElement("a");d.href=o;let l=e.endsWith(".prof")?e:e+".prof";d.download=l;document.body.appendChild(d);d.click();document.body.removeChild(d);URL.revokeObjectURL(o)}
</script>
</body>
</html>)rawliteral";

// =================================================================
// OTA UPDATE
// =================================================================
static const char HTML_TEMPLATE_OTA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Aktualizacja OTA</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>📦 Aktualizacja OTA</h2></div>
<div class="card">
<p>Wybierz plik <strong>.bin</strong> z nowym firmware i kliknij przycisk.<br>
Urządzenie uruchomi się ponownie po zakończeniu.</p>
<form id="upload_form" method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" id="file" accept=".bin">
<button type="submit">🚀 Rozpocznij aktualizację</button>
</form>
<progress id="progress" value="0" max="100"></progress>
<div id="pr"></div><div id="status"></div>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
var form=document.getElementById("upload_form");var file_input=document.getElementById("file");var pr=document.getElementById("pr");var progress_bar=document.getElementById("progress");var status_div=document.getElementById("status");
form.addEventListener("submit",function(event){event.preventDefault();var file=file_input.files[0];if(!file){status_div.innerHTML="Nie wybrano pliku!";return;}var xhr=new XMLHttpRequest();xhr.open("POST","/update");xhr.upload.addEventListener("progress",function(event){if(event.lengthComputable){var p=Math.round((event.loaded/event.total)*100);progress_bar.style.display='block';pr.style.display='block';progress_bar.value=p;pr.innerHTML=p+"%";}});xhr.onloadend=function(){if(progress_bar.value===100){status_div.innerHTML="<span style='color:#4caf50;'>✅ Aktualizacja zakończona!</span>Urządzenie uruchomi się ponownie...";setTimeout(function(){window.location.href='/';},5000);}else if(xhr.status!==200){status_div.innerHTML="<span style='color:#f44336;'>❌ Błąd:</span>"+xhr.responseText;}};var formData=new FormData();formData.append("update",file,"firmware.bin");status_div.innerHTML="⏳ Wysyłanie...";xhr.send(formData);});
</script>
</body>
</html>)rawliteral";

// =================================================================
// ZMIANA HASŁA
// =================================================================
static const char HTML_AUTH_SET[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Zmiana hasła</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>🔑 Zmiana danych logowania</h2></div>
<div class="card">
<form method="POST" action="/auth/save">
<label>Nowy login</label>
<input type="text" name="user" required minlength="1" maxlength="31" autocomplete="username">
<label>Nowe hasło</label>
<input type="password" name="pass" required minlength="4" maxlength="63" autocomplete="new-password">
<label>Powtórz hasło</label>
<input type="password" name="pass2" required minlength="4" maxlength="63" autocomplete="new-password">
<button type="submit">💾 Zapisz</button>
</form>
<p class="note">
⚠️ Po zapisaniu przeglądarka poprosi o ponowne zalogowanie.<br>
Aby zresetować hasło do domyślnego, przytrzymaj ENTER na ekranie IDLE przez 5 sekund.
</p>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
</body>
</html>)rawliteral";

// =================================================================
// PAMIĘĆ FLASH – strona
// =================================================================
static void handleFlashPage() {
    if (!requireAuth()) return;
static const char HTML_FLASH[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Pamięć Flash</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>💾 Pamięć Flash (W25Q128)</h2></div>
<div class="card" id="flashInfo">
<h3>Informacje o pamięci</h3>
<div class="row"><span class="lbl">Status</span><span class="val" id="flashStatus">Sprawdzanie...</span></div>
<div class="row"><span class="lbl">System plików</span><span class="val">LittleFS</span></div>
<div class="row"><span class="lbl">Pojemność</span><span class="val" id="flashSize">-</span></div>
<div class="row"><span class="lbl">Użyte</span><span class="val" id="flashUsed">-</span></div>
<div class="row"><span class="lbl">Wolne</span><span class="val" id="flashFree">-</span></div>
</div>
<div class="warn-box card">
<h3>⚠️ Formatowanie pamięci Flash</h3>
<p>Operacja <strong>nieodwracalnie usunie</strong> wszystkie dane w pamięci.</p>
<p>Profile, backupy i logi zostaną skasowane.</p>
<p>Wędzarnia musi być w stanie <strong>IDLE</strong>.</p>
</div>
<label class="check-label" id="confirmLabel">
<input type="checkbox" id="chk" onchange="toggleBtn()">
<span>Rozumiem – chcę sformatować pamięć Flash</span>
</label>
<button id="btnFormat" disabled onclick="startFormat()">🗑️ Formatuj Flash</button>
<div id="progress">
<div id="bar"><div id="fill"></div></div>
<div id="msg">Formatowanie...</div>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
fetch('/flash/info')
.then(r=>r.json())
.then(d=>{
document.getElementById('flashStatus').textContent=d.ok?'✅ OK':'❌ Błąd';
document.getElementById('flashSize').textContent=d.size||'-';
document.getElementById('flashUsed').textContent=d.used||'-';
document.getElementById('flashFree').textContent=d.free||'-';
if(d.ok&&d.idle){document.getElementById('confirmLabel').style.display='flex';}
else if(!d.idle){document.getElementById('btnFormat').textContent='⛔ Zatrzymaj wędzarnię przed formatowaniem';}
})
.catch(()=>{document.getElementById('flashStatus').textContent='❌ Błąd';});
function toggleBtn(){document.getElementById('btnFormat').disabled=!document.getElementById('chk').checked;}
function startFormat(){
if(!document.getElementById('chk').checked)return;
if(!confirm('OSTATNIA SZANSA: Naprawdę sformatować pamięć Flash?\nWszystkie dane zostaną utracone!'))return;
document.getElementById('btnFormat').disabled=true;
document.getElementById('confirmLabel').style.display='none';
document.getElementById('progress').style.display='block';
let pct=0;
const timer=setInterval(()=>{pct=Math.min(pct+2,90);document.getElementById('fill').style.width=pct+'%';},200);
fetch('/flash/format',{method:'POST'})
.then(r=>r.json())
.then(d=>{
clearInterval(timer);
document.getElementById('fill').style.width='100%';
if(d.ok){
document.getElementById('fill').style.background='#4caf50';
document.getElementById('msg').textContent='✅ '+d.message;
document.getElementById('msg').style.color='#4caf50';
}else{
document.getElementById('fill').style.background='#f44336';
document.getElementById('msg').textContent='❌ '+d.message;
document.getElementById('msg').style.color='#f44336';
document.getElementById('btnFormat').disabled=false;
}
})
.catch(()=>{clearInterval(timer);document.getElementById('msg').textContent='❌ Błąd połączenia';});
}
</script>
</body>
</html>)rawliteral";
    server.send_P(200, "text/html", HTML_FLASH);
}

// =================================================================
// MANAGER PLIKÓW – strona WWW
// =================================================================
static void handleFilesPage() {
    if (!requireAuth()) return;
static const char HTML_FILES[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Manager plików</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
<style>
.file-item{display:flex;justify-content:space-between;align-items:center;padding:10px;background:rgba(0,0,0,0.2);border-radius:8px;margin-bottom:6px;}
.file-name{font-family:'Courier New',monospace;font-size:0.9em;word-break:break-all;}
.file-size{font-size:0.8em;color:#888;margin-left:10px;}
.file-actions{display:flex;gap:5px;}
.file-actions button{padding:6px 10px;font-size:0.8em;margin:0;}
.dir-name{color:#64b5f6;cursor:pointer;}
.dir-name:hover{text-decoration:underline;}
.breadcrumb{margin-bottom:15px;font-size:0.9em;}
.breadcrumb a{color:#64b5f6;text-decoration:none;}
.breadcrumb a:hover{text-decoration:underline;}
textarea{width:100%;min-height:200px;background:rgba(0,0,0,0.3);color:#eee;border:1px solid rgba(255,255,255,0.15);border-radius:8px;padding:10px;font-family:'Courier New',monospace;font-size:0.9em;}
</style>
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>📂 Manager plików Flash</h2></div>
<div class="card">
<h3>Informacje</h3>
<div class="row"><span class="lbl">Zajęte / Całkowite</span><span class="val" id="spaceInfo">-</span></div>
</div>
<div class="card">
<h3>Przeglądaj pliki</h3>
<div class="breadcrumb" id="breadcrumb"></div>
<div id="fileList">Ładowanie...</div>
</div>
<div class="card">
<h3>➕ Dodaj nowy plik</h3>
<label>Ścieżka pliku (np. /profiles/nowy.prof)</label>
<input type="text" id="newFilePath" placeholder="/profiles/nowy.prof">
<label>Zawartość</label>
<textarea id="newFileContent" placeholder="Wpisz zawartość pliku..."></textarea>
<div class="btn-row">
<button class="btn-save" onclick="createFile()">💾 Zapisz plik</button>
</div>
</div>
<div class="card" id="editCard" style="display:none;">
<h3>✏️ Edycja pliku: <span id="editFileName"></span></h3>
<textarea id="editContent"></textarea>
<div class="btn-row">
<button class="btn-save" onclick="saveEdit()">💾 Zapisz zmiany</button>
<button class="btn-clear" onclick="cancelEdit()">❌ Anuluj</button>
</div>
</div>
<div class="card">
<h3>📤 Upload pliku</h3>
<label>Katalog docelowy</label>
<input type="text" id="uploadDir" value="/profiles/" placeholder="/profiles/">
<input type="file" id="uploadFile">
<div class="btn-row">
<button class="btn-add" onclick="uploadFile()">📤 Wyślij plik</button>
</div>
<div id="uploadStatus"></div>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
let currentDir='/';
function loadFiles(dir){
currentDir=dir||'/';
fetch('/api/files?path='+encodeURIComponent(currentDir))
.then(r=>r.json())
.then(d=>{
// POPRAWKA: d.total i d.used to teraz LICZBY (nie stringi)
const total=Number(d.total)||0;
const used=Number(d.used)||0;
const free=total-used;
document.getElementById('spaceInfo').textContent=
formatBytes(used)+' / '+formatBytes(total)+' (wolne: '+formatBytes(free)+')';
let bc='<a href="#" onclick="loadFiles(\'/\');return false;">root</a>';
if(currentDir!=='/'){
let parts=currentDir.split('/').filter(p=>p);
let path='';
parts.forEach(p=>{path+='/'+p;bc+=' / <a href="#" onclick="loadFiles(\''+path+'\');return false;">'+p+'</a>';});
}
document.getElementById('breadcrumb').innerHTML=bc;
let html='';
if(currentDir!=='/')html+='<div class="file-item"><span class="dir-name" onclick="loadFiles(\''+getParentDir(currentDir)+'\')">📁 ..</span></div>';
(d.dirs||[]).forEach(dir=>{
html+='<div class="file-item"><span class="dir-name" onclick="loadFiles(\''+dir.path+'\')">📁 '+dir.name+'</span></div>';
});
(d.files||[]).forEach(f=>{
html+='<div class="file-item"><div><span class="file-name">📄 '+f.name+'</span><span class="file-size">('+formatBytes(f.size)+')</span></div>';
html+='<div class="file-actions">';
html+='<button class="btn-action" onclick="editFile(\''+f.path+'\')">✏️</button>';
html+='<button class="btn-clear" onclick="deleteFile(\''+f.path+'\',\''+f.name+'\')">🗑️</button>';
html+='</div></div>';
});
if(!d.dirs?.length&&!d.files?.length)html+='<div style="text-align:center;padding:20px;opacity:0.5;">Katalog pusty</div>';
document.getElementById('fileList').innerHTML=html;
});
}
function getParentDir(path){
let parts=path.replace(/\/$/,'').split('/');
parts.pop();
return parts.join('/')||'/';
}
function formatBytes(b){
b=Number(b)||0;
if(b>=1048576)return(b/1048576).toFixed(1)+' MB';
if(b>=1024)return(b/1024).toFixed(1)+' KB';
return b+' B';
}
function editFile(path){
fetch('/api/files/read?path='+encodeURIComponent(path))
.then(r=>r.text())
.then(content=>{
document.getElementById('editCard').style.display='block';
document.getElementById('editFileName').textContent=path;
document.getElementById('editContent').value=content;
document.getElementById('editCard').scrollIntoView({behavior:'smooth'});
});
}
function saveEdit(){
const path=document.getElementById('editFileName').textContent;
const content=document.getElementById('editContent').value;
fetch('/api/files/edit',{method:'PUT',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'path='+encodeURIComponent(path)+'&content='+encodeURIComponent(content)})
.then(r=>r.json())
.then(d=>{alert(d.message||d.error);if(d.ok){cancelEdit();loadFiles(currentDir);}});
}
function cancelEdit(){document.getElementById('editCard').style.display='none';}
function deleteFile(path,name){
if(!confirm('Usunąć plik '+name+'?'))return;
fetch('/api/files/delete?path='+encodeURIComponent(path),{method:'DELETE'})
.then(r=>r.json())
.then(d=>{alert(d.message||d.error);loadFiles(currentDir);});
}
function createFile(){
const path=document.getElementById('newFilePath').value;
const content=document.getElementById('newFileContent').value;
if(!path){alert('Podaj ścieżkę pliku!');return;}
fetch('/api/files/edit',{method:'PUT',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'path='+encodeURIComponent(path)+'&content='+encodeURIComponent(content)})
.then(r=>r.json())
.then(d=>{alert(d.message||d.error);if(d.ok){document.getElementById('newFilePath').value='';document.getElementById('newFileContent').value='';loadFiles(currentDir);}});
}
function uploadFile(){
const fileInput=document.getElementById('uploadFile');
const dir=document.getElementById('uploadDir').value;
if(!fileInput.files[0]){alert('Wybierz plik!');return;}
const file=fileInput.files[0];
const path=dir+(dir.endsWith('/')?'':'/')+file.name;
const formData=new FormData();
formData.append('file',file);
formData.append('path',path);
document.getElementById('uploadStatus').textContent='⏳ Wysyłanie...';
fetch('/api/files/upload',{method:'POST',body:formData})
.then(r=>r.json())
.then(d=>{
document.getElementById('uploadStatus').textContent=d.ok?'✅ '+d.message:'❌ '+d.error;
if(d.ok)loadFiles(currentDir);
})
.catch(()=>{document.getElementById('uploadStatus').textContent='❌ Błąd połączenia';});
}
loadFiles('/');
</script>
</body>
</html>)rawliteral";
    server.send_P(200, "text/html", HTML_FILES);
}

// =================================================================
// WIFI
// =================================================================
static const char HTML_WIFI[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Konfiguracja WiFi</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>📶 Konfiguracja WiFi</h2></div>
<div class="card">
<form method="POST" action="/wifi/save">
<label>Nazwa sieci(SSID)</label>
<input type="text" name="ssid" value="SSID_PLACEHOLDER" autocomplete="off">
<label>Hasło</label>
<input type="password" name="pass" autocomplete="current-password">
<button type="submit">📶 Połącz</button>
</form>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
</body>
</html>)rawliteral";

// =================================================================
// CZUJNIKI
// =================================================================
static const char HTML_SENSORS[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Czujniki</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>🔧 Zarządzanie czujnikami</h2></div>
<div class="card">
<h3>Status czujników</h3>
<div class="row"><span class="lbl">DS18B20 (komora)</span><span class="val" id="totalSensors">-</span></div>
<div class="row"><span class="lbl">DS18B20 #1 (idx)</span><span class="val" id="chamber1Idx">-</span></div>
<div class="row"><span class="lbl">DS18B20 #2 (idx)</span><span class="val" id="chamber2Idx">-</span></div>
<div class="row"><span class="lbl">NTC 100k (mięso)</span><span class="val">GPIO 34</span></div>
<div class="row"><span class="lbl">Zidentyfikowane</span><span class="val" id="identified">-</span></div>
</div>
<div class="card">
<h3>Przypisz ręcznie</h3>
<label>Indeks DS18B20 #1 (komora)</label>
<input type="number" id="chamber1Input" min="0" value="0">
<label>Indeks DS18B20 #2 (komora)</label>
<input type="number" id="chamber2Input" min="0" value="1">
<div class="btn-row">
<button class="btn-primary" onclick="reassign()">✅ Przypisz</button>
<button class="btn-auto" onclick="autodetect()">🔍 Auto-wykryj</button>
</div>
<div id="msg"></div>
</div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
function loadInfo(){
fetch('/api/sensors').then(r=>r.json()).then(d=>{
document.getElementById('totalSensors').textContent=d.total_sensors;
document.getElementById('chamber1Idx').textContent=d.chamber1_index;
document.getElementById('chamber2Idx').textContent=d.chamber2_index;
document.getElementById('identified').textContent=d.identified?'✅ Tak':'❌ Nie';
});
}
function reassign(){
const c1=document.getElementById('chamber1Input').value;
const c2=document.getElementById('chamber2Input').value;
const body=new URLSearchParams({chamber1:c1,chamber2:c2});
fetch('/api/sensors/reassign',{method:'POST',body})
.then(r=>r.json())
.then(d=>{document.getElementById('msg').textContent=d.status==='ok'?'✅ Przypisano':'❌ Błąd';loadInfo();});
}
function autodetect(){
document.getElementById('msg').textContent='⏳ Wykrywanie...';
fetch('/api/sensors/autodetect',{method:'POST'})
.then(r=>r.json())
.then(d=>{document.getElementById('msg').textContent=d.message||d.error;loadInfo();});
}
loadInfo();
</script>
</body>
</html>)rawliteral";

// =================================================================
// FUNKCJE POMOCNICZE
// =================================================================
static const char* getStateString(ProcessState st) {
    switch (st) {
        case ProcessState::IDLE:               return "IDLE";
        case ProcessState::RUNNING_AUTO:       return "AUTO";
        case ProcessState::RUNNING_MANUAL:     return "MANUAL";
        case ProcessState::PAUSE_DOOR:         return "PAUZA: DRZWI";
        case ProcessState::PAUSE_SENSOR:       return "PAUZA: CZUJNIK";
        case ProcessState::PAUSE_OVERHEAT:     return "PAUZA: PRZEGRZANIE";
        case ProcessState::PAUSE_HEATER_FAULT: return "AWARIA: GRZALKA";
        case ProcessState::PAUSE_USER:         return "PAUZA UZYTK.";
        case ProcessState::ERROR_PROFILE:      return "ERROR_PROFILE";
        case ProcessState::SOFT_RESUME:        return "Wznawianie...";
        default:                               return "UNKNOWN";
    }
}

static const char* getStatusJSON() {
    static char jsonBuffer[700];
    double tc, tc1, tc2, tm, ts;
    int pm, fm, sm;
    ProcessState st;
    unsigned long elapsedSec=0, stepTotalSec=0, remainingProcessTimeSec=0;
    const char* stepName="";
    char activeProfile[64]="Brak";
    state_lock();
    st=g_currentState; tc=g_tChamber; tc1=g_tChamber1; tc2=g_tChamber2;
    tm=g_tMeat; ts=g_tSet; pm=g_powerMode; fm=g_fanMode; sm=g_manualSmokePwm;
    remainingProcessTimeSec=g_processStats.remainingProcessTimeSec;
    strncpy(activeProfile, storage_get_profile_path(), sizeof(activeProfile)-1);
    activeProfile[sizeof(activeProfile)-1]='\0';
    if (st==ProcessState::RUNNING_MANUAL) {
        elapsedSec=(millis()-g_processStartTime)/1000;
    } else if (st==ProcessState::RUNNING_AUTO) {
        elapsedSec=(millis()-g_stepStartTime)/1000;
        if (g_currentStep<g_stepCount) {
            stepName=g_profile[g_currentStep].name;
            stepTotalSec=g_profile[g_currentStep].minTimeMs/1000;
        }
    }
    state_unlock();
    const char* powerModeStr;
    switch(pm){case 1:powerModeStr="1-grzalka";break;case 2:powerModeStr="2-grzalki";break;case 3:powerModeStr="3-grzalki";break;default:powerModeStr="Brak";}
    const char* fanModeStr;
    switch(fm){case 0:fanModeStr="OFF";break;case 1:fanModeStr="ON";break;case 2:fanModeStr="Cyklicznie";break;default:fanModeStr="Brak";}
    char cleanProfileName[64];
    strncpy(cleanProfileName,activeProfile,sizeof(cleanProfileName));
    if(strstr(cleanProfileName,"/profiles/")!=NULL) strcpy(cleanProfileName,strstr(cleanProfileName,"/profiles/")+10);
    else if(strstr(cleanProfileName,"github:")!=NULL) memmove(cleanProfileName,cleanProfileName+7,strlen(cleanProfileName)-6);
    snprintf(jsonBuffer,sizeof(jsonBuffer),
        "{\"tChamber\":%.1f,\"tChamber1\":%.1f,\"tChamber2\":%.1f,"
        "\"tMeat\":%.1f,\"tSet\":%.1f,"
        "\"powerMode\":%d,\"fanMode\":%d,\"smokePwm\":%d,"
        "\"mode\":\"%s\",\"state\":%d,"
        "\"powerModeText\":\"%s\",\"fanModeText\":\"%s\","
        "\"elapsedTimeSec\":%lu,\"stepName\":\"%s\","
        "\"stepTotalTimeSec\":%lu,\"activeProfile\":\"%s\","
        "\"remainingProcessTimeSec\":%lu}",
        tc,tc1,tc2,tm,ts,pm,fm,sm,
        getStateString(st),(int)st,
        powerModeStr,fanModeStr,
        elapsedSec,stepName,stepTotalSec,cleanProfileName,
        remainingProcessTimeSec);
    return jsonBuffer;
}

// =================================================================
// INFORMACJE SYSTEMOWE
// =================================================================
static const char HTML_SYSINFO[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset='utf-8'>
<title>Informacje systemowe</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="page-wrap">
<div class="page-header"><h2>ℹ️ Informacje systemowe</h2></div>
<div class="card">
<h3>Pamięć RAM</h3>
<div class="row"><span class="lbl">🧠 Wolna heap</span><span class="val info" id="heap">...</span></div>
<div class="row"><span class="lbl">📉 Min. heap(od boot)</span><span class="val" id="heap_min">...</span></div>
<div class="row"><span class="lbl">📦 Całkowita PSRAM</span><span class="val" id="psram">...</span></div>
<div class="mem-bar-wrap" title="Użycie pamięci heap"><div class="mem-bar" id="mem_bar" style="width:0%;background:#4caf50;"></div></div>
</div>
<div class="card">
<h3>Procesor i czas pracy</h3>
<div class="row"><span class="lbl">⏱️ Uptime</span><span class="val info" id="uptime">...</span></div>
<div class="row"><span class="lbl">⚡ Częstotliwość CPU</span><span class="val" id="cpu_freq">...</span></div>
<div class="row"><span class="lbl">🌡️ Temp. procesora</span><span class="val" id="cpu_temp">...</span></div>
<div class="row"><span class="lbl">🔢 Reset reason</span><span class="val" id="reset_reason">...</span></div>
</div>
<div class="card">
<h3>Peryferia</h3>
<div class="row"><span class="lbl">💾 Pamięć Flash</span><span class="val" id="flash_status">...</span></div>
<div class="row"><span class="lbl">📏 Pojemność Flash</span><span class="val" id="flash_size_info">...</span></div>
<div class="row"><span class="lbl">🌡️ Czujniki DS18B20</span><span class="val" id="sensors">...</span></div>
<div class="row"><span class="lbl">✅ Zidentyfikowane</span><span class="val" id="sensors_id">...</span></div>
</div>
<div class="card">
<h3>Sieć WiFi</h3>
<div class="row"><span class="lbl">📶 Status STA</span><span class="val" id="wifi_status">...</span></div>
<div class="row"><span class="lbl">🔗 SSID</span><span class="val" id="wifi_ssid">...</span></div>
<div class="row"><span class="lbl">📡 IP(STA)</span><span class="val" id="wifi_ip">...</span></div>
<div class="row"><span class="lbl">📡 IP(AP)</span><span class="val" id="ap_ip">...</span></div>
<div class="row"><span class="lbl">📶 Siła sygnału</span><span class="val" id="wifi_rssi">...</span></div>
</div>
<div class="card">
<h3>O systemie</h3>
<div class="row"><span class="lbl">🔥 Wersja firmware</span><span class="val info" id="fw_version">...</span></div>
<div class="row"><span class="lbl">👤 Autor</span><span class="val" id="fw_author">...</span></div>
<div class="row"><span class="lbl">🏷️ Chip model</span><span class="val" id="chip_model">...</span></div>
<div class="row"><span class="lbl">🔢 MAC adres</span><span class="val" id="mac_addr">...</span></div>
<div class="row"><span class="lbl">💡 Flash size</span><span class="val" id="flash_size">...</span></div>
</div>
<button class="btn-refresh" onclick="loadInfo()">🔄 Odśwież dane</button>
<div class="updated" id="updated_at"></div>
<a class="back-link" href="/">⬅️ Wróć do strony głównej</a>
</div>
<script>
function fmtBytes(b){b=Number(b)||0;if(b>=1048576)return(b/1048576).toFixed(1)+' MB';if(b>=1024)return(b/1024).toFixed(1)+' KB';return b+' B';}
function fmtUptime(s){const d=Math.floor(s/86400);const h=Math.floor((s%86400)/3600);const m=Math.floor((s%3600)/60);const sec=s%60;let out='';if(d)out+=d+'d ';out+=String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');return out;}
function setVal(id,text,cls){const el=document.getElementById(id);if(!el)return;el.textContent=text;el.className='val'+(cls?' '+cls:'');}
function loadInfo(){
fetch('/api/sysinfo').then(r=>r.json()).then(d=>{
setVal('heap',fmtBytes(d.heap_free),'info');
setVal('heap_min',fmtBytes(d.heap_min));
setVal('psram',d.psram_total>0?fmtBytes(d.psram_total):'Brak');
const usedPct=d.heap_total>0?Math.round((1-d.heap_free/d.heap_total)*100):0;
const bar=document.getElementById('mem_bar');
bar.style.width=usedPct+'%';
bar.style.background=usedPct>80?'#f44336':usedPct>60?'#ff9800':'#4caf50';
document.getElementById('uptime').innerHTML='<span class="live-dot"></span>'+fmtUptime(d.uptime_sec);
document.getElementById('uptime').className='val info';
setVal('cpu_freq',d.cpu_freq+' MHz');
setVal('cpu_temp',d.cpu_temp>0?d.cpu_temp.toFixed(1)+' °C':'N/A',d.cpu_temp>70?'err':d.cpu_temp>55?'warn':'');
setVal('reset_reason',d.reset_reason);
const flashOk=d.flash_ok;
setVal('flash_status',flashOk?'✅ OK (LittleFS)':'❌ Błąd',flashOk?'ok':'err');
setVal('flash_size_info',flashOk?fmtBytes(d.flash_total)+' / wolne '+fmtBytes(d.flash_free):'-');
setVal('sensors',d.sensor_count+' szt.',d.sensor_count>0?'ok':'warn');
setVal('sensors_id',d.sensors_identified?'✅ Tak':'⚠️ Nie',d.sensors_identified?'ok':'warn');
const wOk=d.wifi_connected;
setVal('wifi_status',wOk?'✅ Połączono':'❌ Rozłączono',wOk?'ok':'err');
setVal('wifi_ssid',d.wifi_ssid||'-');
setVal('wifi_ip',d.wifi_ip||'-');
setVal('ap_ip',d.ap_ip||'-');
setVal('wifi_rssi',wOk?d.wifi_rssi+' dBm':'-',d.wifi_rssi>-60?'ok':d.wifi_rssi>-75?'warn':'err');
setVal('fw_version',d.fw_version,'info');
setVal('fw_author',d.fw_author);
setVal('chip_model',d.chip_model);
setVal('mac_addr',d.mac_addr);
setVal('flash_size',fmtBytes(d.flash_size));
document.getElementById('updated_at').textContent='Odświeżono: '+new Date().toLocaleTimeString('pl-PL');
}).catch(e=>{document.getElementById('updated_at').textContent='❌ Błąd pobierania danych';console.error(e);});
}
loadInfo();
setInterval(loadInfo,5000);
</script>
</body>
</html>)rawliteral";

static void handleSysInfoPage() {
    if (!requireAuth()) return;
    server.send_P(200, "text/html", HTML_SYSINFO);
}

// =================================================================
// SYSINFO JSON - POPRAWKA: esp_littlefs_info zamiast LittleFS.totalBytes()
// =================================================================
static void handleSysInfoJson() {
    if (!requireAuth()) return;
    uint32_t heapFree  = ESP.getFreeHeap();
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapMin   = ESP.getMinFreeHeap();
    uint32_t psramTotal= ESP.getPsramSize();
    unsigned long uptimeSec = millis()/1000;
    uint32_t cpuFreq = ESP.getCpuFreqMHz();
    float cpuTemp = temperatureRead();
    esp_reset_reason_t rr = esp_reset_reason();
    const char* resetReasonStr;
    switch(rr){
        case ESP_RST_POWERON:  resetReasonStr="Power-on";    break;
        case ESP_RST_EXT:      resetReasonStr="Reset ext.";  break;
        case ESP_RST_SW:       resetReasonStr="Reset SW";    break;
        case ESP_RST_PANIC:    resetReasonStr="Panic/Crash"; break;
        case ESP_RST_INT_WDT:  resetReasonStr="WDT Int.";    break;
        case ESP_RST_TASK_WDT: resetReasonStr="WDT Task";    break;
        case ESP_RST_WDT:      resetReasonStr="WDT";         break;
        case ESP_RST_DEEPSLEEP:resetReasonStr="Deep sleep";  break;
        case ESP_RST_BROWNOUT: resetReasonStr="Brownout";    break;
        default:               resetReasonStr="Inny";        break;
    }

    // POPRAWKA: esp_littlefs_info zamiast LittleFS.totalBytes()=0
    size_t flashTotal=0, flashUsed=0;
    esp_err_t fErr = esp_littlefs_info(EXT_FLASH_PARTITION_LABEL, &flashTotal, &flashUsed);
    bool flashOk = (fErr == ESP_OK && flashTotal > 0);
    size_t flashFree = (flashTotal > flashUsed) ? (flashTotal-flashUsed) : 0;

    int sensorCount   = sensors.getDeviceCount();
    bool sensorsIdent = areSensorsIdentified();
    bool wifiConn     = (WiFi.status()==WL_CONNECTED);
    String wifiSsid   = wifiConn ? WiFi.SSID() : "";
    String wifiIp     = wifiConn ? WiFi.localIP().toString() : "";
    String apIp       = WiFi.softAPIP().toString();
    int    wifiRssi   = wifiConn ? WiFi.RSSI() : 0;
    String chipModel  = ESP.getChipModel();
    uint32_t flashSize= ESP.getFlashChipSize();
    String macString  = WiFi.macAddress();

    static char json[900];
    snprintf(json, sizeof(json),
        "{\"heap_free\":%u,\"heap_total\":%u,\"heap_min\":%u,\"psram_total\":%u,"
        "\"uptime_sec\":%lu,\"cpu_freq\":%u,\"cpu_temp\":%.1f,\"reset_reason\":\"%s\","
        "\"flash_ok\":%s,\"flash_total\":%u,\"flash_used\":%u,\"flash_free\":%u,"
        "\"sensor_count\":%d,\"sensors_identified\":%s,"
        "\"wifi_connected\":%s,\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\","
        "\"ap_ip\":\"%s\",\"wifi_rssi\":%d,"
        "\"fw_name\":\"" FW_NAME "\",\"fw_version\":\"" FW_VERSION "\","
        "\"fw_author\":\"" FW_AUTHOR "\","
        "\"chip_model\":\"%s\",\"mac_addr\":\"%s\",\"flash_size\":%u}",
        heapFree,heapTotal,heapMin,psramTotal,
        uptimeSec,cpuFreq,cpuTemp,resetReasonStr,
        flashOk?"true":"false",(unsigned)flashTotal,(unsigned)flashUsed,(unsigned)flashFree,
        sensorCount,sensorsIdent?"true":"false",
        wifiConn?"true":"false",wifiSsid.c_str(),wifiIp.c_str(),
        apIp.c_str(),wifiRssi,
        chipModel.c_str(),macString.c_str(),flashSize);
    server.send(200, "application/json", json);
}

// =================================================================
// CZUJNIKI API
// =================================================================
static void handleSensorInfo() {
    if (!requireAuth()) return;
    char jsonBuf[128];
    snprintf(jsonBuf,sizeof(jsonBuf),
        "{\"total_sensors\":%d,\"chamber1_index\":%d,\"chamber2_index\":%d,"
        "\"identified\":%s,\"ntc_pin\":%d}",
        getTotalSensorCount(),getChamberSensor1Index(),getChamberSensor2Index(),
        areSensorsIdentified()?"true":"false",PIN_NTC);
    server.send(200,"application/json",jsonBuf);
}
static void handleSensorReassign() {
    if (!requireAuth()) return;
    if (server.hasArg("chamber1")&&server.hasArg("chamber2")) {
        int c1=server.arg("chamber1").toInt();
        int c2=server.arg("chamber2").toInt();
        if (c1>=0&&c2>=0&&c1!=c2) {
            chamberSensor1Index=c1; chamberSensor2Index=c2; sensorsIdentified=true;
            server.send(200,"application/json","{\"status\":\"ok\"}");
        } else server.send(400,"application/json","{\"error\":\"Invalid indices\"}");
    } else server.send(400,"application/json","{\"error\":\"Missing parameters\"}");
}
static void handleSensorAutoDetect() {
    if (!requireAuth()) return;
    identifyAndAssignSensors();
    if (areSensorsIdentified()) server.send(200,"application/json","{\"message\":\"Sensors auto-detected and assigned\"}");
    else server.send(500,"application/json","{\"error\":\"Auto-detection failed\"}");
}
static void handleSensorsPage() {
    if (!requireAuth()) return;
    server.send_P(200,"text/html",HTML_SENSORS);
}

// =================================================================
// FLASH INFO - POPRAWKA: esp_littlefs_info zamiast LittleFS.totalBytes()
// =================================================================
static void handleFlashInfo() {
    if (!requireAuth()) return;
    bool isIdle=false;
    if (state_lock()) { isIdle=(g_currentState==ProcessState::IDLE); state_unlock(); }

    // POPRAWKA KLUCZOWA: LittleFS.totalBytes() zwraca 0 dla zewnętrznych chipów
    // Użyj esp_littlefs_info() bezpośrednio
    size_t totalBytes=0, usedBytes=0;
    esp_err_t err = esp_littlefs_info(EXT_FLASH_PARTITION_LABEL, &totalBytes, &usedBytes);
    bool flashMounted = (err==ESP_OK && totalBytes>0);
    size_t freeBytes = (totalBytes>usedBytes) ? (totalBytes-usedBytes) : 0;

    auto fmtSize=[](size_t bytes, char* buf, size_t len){
        if(bytes>=1024*1024) snprintf(buf,len,"%.1f MB",bytes/(1024.0*1024.0));
        else if(bytes>=1024)  snprintf(buf,len,"%.1f KB",bytes/1024.0);
        else                  snprintf(buf,len,"%u B",(unsigned)bytes);
    };
    char sizeStr[16],usedStr[16],freeStr[16];
    fmtSize(totalBytes,sizeStr,sizeof(sizeStr));
    fmtSize(usedBytes, usedStr,sizeof(usedStr));
    fmtSize(freeBytes, freeStr,sizeof(freeStr));

    char json[256];
    snprintf(json,sizeof(json),
        "{\"ok\":%s,\"idle\":%s,\"size\":\"%s\",\"used\":\"%s\",\"free\":\"%s\"}",
        flashMounted?"true":"false",
        isIdle?"true":"false",
        sizeStr,usedStr,freeStr);
    server.send(200,"application/json",json);
}

// =================================================================
// FLASH FORMAT - POPRAWKA: esp_littlefs_format + begin z etykietą
// =================================================================
static void handleFlashFormat() {
    if (!requireAuth()) return;
    bool isIdle=false;
    if (state_lock()) { isIdle=(g_currentState==ProcessState::IDLE); state_unlock(); }
    if (!isIdle) {
        server.send(200,"application/json",
            "{\"ok\":false,\"message\":\"Zatrzymaj proces przed formatowaniem!\"}");
        return;
    }
    LOG_FMT(LOG_LEVEL_WARN,"Flash FORMAT via HTTP");

    LittleFS.end();
    delay(300);

    // POPRAWKA KLUCZOWA: esp_littlefs_format z etykietą
    // Poprzedni kod: LittleFS.begin(true) bez etykiety -> montował wewnętrzny 896KB!
    esp_err_t fmt = esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
    if (fmt!=ESP_OK) {
        LittleFS.begin(false,"/littlefs",10,EXT_FLASH_PARTITION_LABEL);
        server.send(200,"application/json","{\"ok\":false,\"message\":\"Formatowanie nieudane.\"}");
        return;
    }
    delay(300);

    // Remontuj Z ETYKIETĄ
    bool ok = LittleFS.begin(false,"/littlefs",10,EXT_FLASH_PARTITION_LABEL);
    if (!ok) {
        server.send(200,"application/json",
            "{\"ok\":false,\"message\":\"Format OK ale remount nieudany.\"}");
        return;
    }

    // Odtwórz katalogi
    {File f=LittleFS.open("/profiles/.keep","w");if(f)f.close();}
    {File f=LittleFS.open("/logs/.keep","w");    if(f)f.close();}
    {File f=LittleFS.open("/backup/.keep","w");  if(f)f.close();}

    size_t tot=0,use=0;
    esp_littlefs_info(EXT_FLASH_PARTITION_LABEL,&tot,&use);

    char msg[128];
    snprintf(msg,sizeof(msg),"Sformatowano! %.1f MB dostepne.",tot/(1024.0*1024.0));
    char json[200];
    snprintf(json,sizeof(json),"{\"ok\":true,\"message\":\"%s\"}",msg);
    server.send(200,"application/json",json);
}

// =================================================================
// API PLIKI - POPRAWKA: total/used jako LICZBY (nie stringi)
// =================================================================
static void handleApiFilesList() {
    if (!requireAuth()) return;
    String path = server.hasArg("path") ? server.arg("path") : "/";
    if (path.isEmpty()) path="/";

    // POPRAWKA: esp_littlefs_info zamiast LittleFS.totalBytes()
    size_t totalBytes=0, usedBytes=0;
    esp_littlefs_info(EXT_FLASH_PARTITION_LABEL,&totalBytes,&usedBytes);

    File root = LittleFS.open(path);
    if (!root) {
        char err[128];
        // POPRAWKA: LICZBY bez cudzysłowów -> JS może je używać arytmetycznie
        snprintf(err,sizeof(err),
            "{\"total\":%u,\"used\":%u,\"dirs\":[],\"files\":[]}",
            (unsigned)totalBytes,(unsigned)usedBytes);
        server.send(200,"application/json",err);
        return;
    }

    String dirsJson="",filesJson="";
    bool firstDir=true,firstFile=true;

    File file=root.openNextFile();
    while(file){
        String fileName=file.name();
        if(fileName.endsWith("/.keep")){
            String dirPath=fileName.substring(0,fileName.length()-6);
            String dirName=dirPath;
            int ls=dirName.lastIndexOf('/');
            if(ls>=0)dirName=dirName.substring(ls+1);
            if(dirName.length()>0&&dirName!="."){
                if(!firstDir)dirsJson+=",";
                dirsJson+="{\"name\":\""+dirName+"\",\"path\":\""+dirPath+"\"}";
                firstDir=false;
            }
        } else if(file.isDirectory()){
            String dirName=fileName;
            int ls=dirName.lastIndexOf('/');
            if(ls>=0)dirName=dirName.substring(ls+1);
            if(dirName.length()>0){
                if(!firstDir)dirsJson+=",";
                dirsJson+="{\"name\":\""+dirName+"\",\"path\":\""+fileName+"\"}";
                firstDir=false;
            }
        } else {
            String name=fileName;
            int ls=name.lastIndexOf('/');
            if(ls>=0)name=name.substring(ls+1);
            if(name.length()>0&&name!=".keep"){
                if(!firstFile)filesJson+=",";
                filesJson+="{\"name\":\""+name+"\",\"path\":\""+fileName+"\",\"size\":"+String(file.size())+"}";
                firstFile=false;
            }
        }
        file=root.openNextFile();
    }
    root.close();

    // POPRAWKA KLUCZOWA: total i used jako LICZBY (bez cudzysłowów!)
    // Stary kod: "\"total\":\""+String(totalBytes)+"\"" -> d.total = "16777216" (string)
    // Nowy kod:  "\"total\":"+String(totalBytes)        -> d.total = 16777216 (number)
    String json = "{\"total\":"+String(totalBytes)+
                  ",\"used\":"+String(usedBytes)+
                  ",\"dirs\":["+dirsJson+"]"+
                  ",\"files\":["+filesJson+"]}";
    server.send(200,"application/json",json);
}

static void handleApiFileRead() {
    if (!requireAuth()) return;
    if (!server.hasArg("path")) { server.send(400,"text/plain","Missing path"); return; }
    String path=server.arg("path");
    if (!LittleFS.exists(path)) { server.send(404,"text/plain","File not found"); return; }
    File f=LittleFS.open(path,"r");
    if (!f) { server.send(500,"text/plain","Cannot open file"); return; }
    String content=f.readString(); f.close();
    server.send(200,"text/plain",content);
}

static void handleApiFileEdit() {
    if (!requireAuth()) return;
    if (!server.hasArg("path")) { server.send(400,"application/json","{\"ok\":false,\"error\":\"Missing path\"}"); return; }
    String path=server.arg("path");
    String content=server.hasArg("content")?server.arg("content"):"";
    File f=LittleFS.open(path,"w");
    if (!f) { server.send(500,"application/json","{\"ok\":false,\"error\":\"Cannot create file\"}"); return; }
    f.print(content); f.close();
    LOG_FMT(LOG_LEVEL_INFO,"File saved: %s (%d bytes)",path.c_str(),content.length());
    server.send(200,"application/json","{\"ok\":true,\"message\":\"Plik zapisany!\"}");
}

static void handleApiFileDelete() {
    if (!requireAuth()) return;
    if (!server.hasArg("path")) { server.send(400,"application/json","{\"ok\":false,\"error\":\"Missing path\"}"); return; }
    String path=server.arg("path");
    if (!LittleFS.exists(path)) { server.send(404,"application/json","{\"ok\":false,\"error\":\"File not found\"}"); return; }
    if (LittleFS.remove(path)) {
        LOG_FMT(LOG_LEVEL_INFO,"File deleted: %s",path.c_str());
        server.send(200,"application/json","{\"ok\":true,\"message\":\"Plik usunięty!\"}");
    } else {
        server.send(500,"application/json","{\"ok\":false,\"error\":\"Cannot delete file\"}");
    }
}

static String uploadFilePath="";
static File uploadFileHandle;

static void handleApiFileUploadEnd() {
    if (!requireAuth()) return;
    if (uploadFilePath.length()>0) server.send(200,"application/json","{\"ok\":true,\"message\":\"Plik przesłany!\"}");
    else server.send(500,"application/json","{\"ok\":false,\"error\":\"Upload failed\"}");
}

static void handleApiFileUploadData() {
    if (!server.authenticate(storage_get_auth_user(),storage_get_auth_pass())) return;
    HTTPUpload& upload=server.upload();
    if (upload.status==UPLOAD_FILE_START) {
        uploadFilePath=server.hasArg("path")?server.arg("path"):"/"+String(upload.filename);
        LOG_FMT(LOG_LEVEL_INFO,"Upload start: %s",uploadFilePath.c_str());
        uploadFileHandle=LittleFS.open(uploadFilePath,"w");
    } else if (upload.status==UPLOAD_FILE_WRITE) {
        if(uploadFileHandle) uploadFileHandle.write(upload.buf,upload.currentSize);
    } else if (upload.status==UPLOAD_FILE_END) {
        if(uploadFileHandle){uploadFileHandle.close();LOG_FMT(LOG_LEVEL_INFO,"Upload done: %s (%u bytes)",uploadFilePath.c_str(),upload.totalSize);}
    } else if (upload.status==UPLOAD_FILE_ABORTED) {
        if(uploadFileHandle){uploadFileHandle.close();LittleFS.remove(uploadFilePath);}
        uploadFilePath="";
    }
}

// =================================================================
// CSS WSPÓLNY
// =================================================================
static const char CSS_COMMON[] PROGMEM = "*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);color:#eee;padding:20px;min-height:100vh}.page-wrap{max-width:520px;margin:0 auto}.page-header{background:linear-gradient(135deg,#d32f2f 0%,#c62828 100%);padding:18px 22px;border-radius:14px;margin-bottom:22px;box-shadow:0 8px 20px rgba(211,47,47,.3)}.page-header h2{font-size:1.4em;margin:0;text-shadow:1px 1px 3px rgba(0,0,0,.4)}.card{background:rgba(255,255,255,.05);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.1);border-radius:14px;padding:20px;margin-bottom:16px;box-shadow:0 8px 32px rgba(0,0,0,.3)}.card h3{font-size:.8em;text-transform:uppercase;letter-spacing:1.5px;color:#aaa;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid rgba(255,255,255,.1)}.row{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid rgba(255,255,255,.06)}.row:last-child{border:none}.row .lbl{color:#888;font-size:.9em}.row .val{font-weight:600;font-family:'Courier New',monospace;font-size:.95em}.val.ok{color:#4caf50}.val.err{color:#f44336}.val.warn{color:#ff9800}.val.info{color:#00bcd4}label{display:block;margin-top:14px;margin-bottom:5px;font-size:.85em;color:#aaa;text-transform:uppercase;letter-spacing:.8px}input[type=text],input[type=password],input[type=number],input[type=file],select{width:100%;padding:11px 14px;background:rgba(0,0,0,.35);color:#eee;border:1px solid rgba(255,255,255,.15);border-radius:9px;font-size:1em;transition:border-color .2s,box-shadow .2s}input:focus,select:focus{outline:none;border-color:#2196f3;box-shadow:0 0 0 3px rgba(33,150,243,.2)}.btn{display:block;width:100%;padding:13px;margin-top:10px;border:none;border-radius:9px;font-size:1em;font-weight:700;cursor:pointer;transition:all .25s;box-shadow:0 4px 14px rgba(0,0,0,.3)}.btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,.4)}.btn-primary{background:linear-gradient(135deg,#1976d2,#1565c0);color:#fff}.btn-danger{background:linear-gradient(135deg,#c62828,#b71c1c);color:#fff}.btn:disabled{opacity:.4;cursor:not-allowed;transform:none!important}.warn-box{background:rgba(211,47,47,.12);border:1px solid rgba(211,47,47,.45);border-radius:12px;padding:16px;margin-bottom:16px}.warn-box h3{color:#ef9a9a}.warn-box p{margin-top:8px;font-size:.9em;color:#ccc;line-height:1.5}.check-label{display:flex;align-items:center;gap:10px;cursor:pointer;font-size:.95em;padding:12px;background:rgba(0,0,0,.2);border-radius:8px;margin:14px 0}.check-label input[type=checkbox]{width:18px;height:18px;flex-shrink:0}.note{font-size:.82em;color:#888;margin-top:14px;line-height:1.6;padding:12px;background:rgba(0,0,0,.2);border-radius:8px}.btn-row{display:flex;gap:8px;margin-top:14px}.btn-row button{flex:1;padding:12px;border:none;border-radius:9px;font-size:.95em;font-weight:600;cursor:pointer;transition:all .25s;box-shadow:0 4px 12px rgba(0,0,0,.3)}.btn-row button:hover{transform:translateY(-2px)}.btn-add{background:linear-gradient(135deg,#2196f3,#1565c0);color:#fff}.btn-save{background:linear-gradient(135deg,#4caf50,#388e3c);color:#fff}.btn-pc{background:linear-gradient(135deg,#607d8b,#455a64);color:#fff}.btn-clear{background:linear-gradient(135deg,#c62828,#b71c1c);color:#fff;margin-left:auto}.back-link{display:inline-block;margin-top:18px;color:#64b5f6;text-decoration:none;font-size:.95em;transition:color .2s}.back-link:hover{color:#90caf9}#progress{display:none;margin-top:14px}#bar{height:8px;background:rgba(0,0,0,.3);border-radius:4px;overflow:hidden;margin-bottom:8px}#fill{height:100%;width:0;background:#f44336;border-radius:4px;transition:width .3s}#msg{font-size:.9em;text-align:center;color:#aaa}";

static void handleCommonCss() {
    server.sendHeader("Cache-Control","public, max-age=86400");
    server.send_P(200,"text/css",CSS_COMMON);
}

// =================================================================
// INICJALIZACJA SERWERA
// =================================================================
void web_server_init() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(CFG_AP_SSID, CFG_AP_PASS);
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
    const char* ssid=storage_get_wifi_ssid();
    if (strlen(ssid)>0) { WiFi.begin(ssid,storage_get_wifi_pass()); Serial.printf("Connecting STA to %s...\n",ssid); }

    server.on("/style.css",HTTP_GET,handleCommonCss);
    server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",HTML_TEMPLATE_MAIN);});
    server.on("/status",HTTP_GET,[](){server.send(200,"application/json",getStatusJSON());});
    server.on("/api/profiles",HTTP_GET,[](){server.send(200,"application/json",storage_list_profiles_json());});
    server.on("/api/github_profiles",HTTP_GET,[](){server.send(200,"application/json",storage_list_github_profiles_json());});

    server.on("/flash",        HTTP_GET,  handleFlashPage);
    server.on("/flash/info",   HTTP_GET,  handleFlashInfo);
    server.on("/flash/format", HTTP_POST, handleFlashFormat);

    server.on("/files",            HTTP_GET,    handleFilesPage);
    server.on("/api/files",        HTTP_GET,    handleApiFilesList);
    server.on("/api/files/read",   HTTP_GET,    handleApiFileRead);
    server.on("/api/files/edit",   HTTP_PUT,    handleApiFileEdit);
    server.on("/api/files/delete", HTTP_DELETE, handleApiFileDelete);
    server.on("/api/files/upload", HTTP_POST,   handleApiFileUploadEnd, handleApiFileUploadData);

    server.on("/auth/login",HTTP_GET,[](){if(!requireAuth())return;server.sendHeader("Location","/");server.send(302);});
    server.on("/auth/set",HTTP_GET,[](){if(!requireAuth())return;server.send_P(200,"text/html",HTML_AUTH_SET);});
    server.on("/auth/save",HTTP_POST,[](){
        if(!requireAuth())return;
        if(!server.hasArg("user")||!server.hasArg("pass")||!server.hasArg("pass2")){server.send(400,"text/html","<html><body style='background:#111;color:#eee;padding:20px'>Brak pól. <a href='/auth/set'>Wróć</a></body></html>");return;}
        String nu=server.arg("user"),np=server.arg("pass"),np2=server.arg("pass2");
        if(nu.length()==0||nu.length()>31){server.send(400,"text/html","<html><body style='background:#1a1a2e;color:#eee;padding:20px'>Nieprawidłowy login. <a href='/auth/set'>Wróć</a></body></html>");return;}
        if(np.length()<4||np.length()>63){server.send(400,"text/html","<html><body style='background:#1a1a2e;color:#eee;padding:20px'>Hasło 4-63 znaki. <a href='/auth/set'>Wróć</a></body></html>");return;}
        if(np!=np2){server.send(400,"text/html","<html><body style='background:#1a1a2e;color:#eee;padding:20px'>Hasła różne. <a href='/auth/set'>Wróć</a></body></html>");return;}
        storage_save_auth_nvs(nu.c_str(),np.c_str());
        LOG_FMT(LOG_LEVEL_INFO,"Auth changed, user: %s",nu.c_str());
        server.requestAuthentication(BASIC_AUTH,"Wedzarnia","Haslo zmienione! Zaloguj sie ponownie.");
    });

    server.on("/creator",HTTP_GET,[](){if(!requireAuth())return;server.send_P(200,"text/html",HTML_TEMPLATE_CREATOR);});
    server.on("/update",HTTP_GET,[](){if(!requireAuth())return;server.send_P(200,"text/html",HTML_TEMPLATE_OTA);});

    server.on("/profile/get",HTTP_GET,[](){
        if(!requireAuth())return;
        if(!server.hasArg("name")||!server.hasArg("source")){server.send(400,"text/plain","Brak parametrów");return;}
        server.send(200,"application/json",storage_get_profile_as_json(server.arg("name").c_str()));
    });
    server.on("/profile/select",HTTP_GET,[](){
        if(!requireAuth())return;
        if(server.hasArg("name")&&server.hasArg("source")){
            String pn=server.arg("name"),src=server.arg("source");
            bool ok=false;
            if(src=="sd"||src=="flash"){String fp="/profiles/"+pn;storage_save_profile_path_nvs(fp.c_str());ok=storage_load_profile();}
            else if(src=="github"){String gp="github:"+pn;storage_save_profile_path_nvs(gp.c_str());ok=storage_load_github_profile(pn.c_str());}
            server.send(ok?200:500,"text/plain",ok?"OK, profil "+pn+" załadowany.":"Błąd ładowania profilu.");
        } else server.send(400,"text/plain","Brak parametrów");
    });
    server.on("/auto/next_step",HTTP_GET,[](){
        if(!requireAuth())return;
        state_lock();
        if(g_currentState==ProcessState::RUNNING_AUTO&&g_currentStep<g_stepCount) g_profile[g_currentStep].minTimeMs=0;
        state_unlock();
        server.send(200,"text/plain","OK");
    });
    server.on("/timer/reset",HTTP_GET,[](){
        if(!requireAuth())return;
        state_lock();
        if(g_currentState==ProcessState::RUNNING_MANUAL) g_processStartTime=millis();
        else if(g_currentState==ProcessState::RUNNING_AUTO) g_stepStartTime=millis();
        state_unlock();
        server.send(200,"text/plain","Timer zresetowany");
    });
    server.on("/mode/manual",HTTP_GET,[](){if(!requireAuth())return;process_start_manual();server.send(200,"text/plain","OK");});
    server.on("/auto/start",HTTP_GET,[](){
        if(!requireAuth())return;
        if(storage_load_profile()){process_start_auto();server.send(200,"text/plain","OK");}
        else server.send(500,"text/plain","Profile error");
    });
    server.on("/auto/stop",HTTP_GET,[](){
        if(!requireAuth())return;
        allOutputsOff();state_lock();g_currentState=ProcessState::IDLE;state_unlock();
        server.send(200,"text/plain","OK");
    });
    server.on("/profile/reload",HTTP_GET,[](){
        if(!requireAuth())return;
        if(storage_reinit_flash()){storage_load_profile();server.send(200,"text/plain","Flash odświeżony.");}
        else server.send(500,"text/plain","Błąd reinicjalizacji Flash!");
    });
    server.on("/profile/create",HTTP_POST,[](){
        if(!requireAuth())return;
        if(!server.hasArg("filename")||!server.hasArg("data")){server.send(400,"text/plain","Brak danych.");return;}
        String fn=server.arg("filename"),data=server.arg("data");
        if(fn.isEmpty()){server.send(400,"text/plain","Pusta nazwa.");return;}
        if(!fn.endsWith(".prof"))fn+=".prof";
        String path="/profiles/"+fn;
        File f=LittleFS.open(path,"w");
        if(!f){server.send(500,"text/plain","Nie można otworzyć pliku.");return;}
        f.print(data);f.close();
        server.send(200,"text/plain","Profil '"+fn+"' zapisany!");
    });

    server.on("/sysinfo",    HTTP_GET,handleSysInfoPage);
    server.on("/api/sysinfo",HTTP_GET,handleSysInfoJson);

    server.on("/api/sensors",           HTTP_GET, handleSensorInfo);
    server.on("/api/sensors/reassign",  HTTP_POST,handleSensorReassign);
    server.on("/api/sensors/autodetect",HTTP_POST,handleSensorAutoDetect);
    server.on("/sensors",               HTTP_GET, handleSensorsPage);

    server.on("/manual/set",HTTP_GET,[](){
        if(!requireAuth())return;
        if(server.hasArg("tSet")){double v=constrain(server.arg("tSet").toFloat(),CFG_T_MIN_SET,CFG_T_MAX_SET);state_lock();g_tSet=v;state_unlock();storage_save_manual_settings_nvs();}
        server.send(200,"text/plain","OK");
    });
    server.on("/manual/power",HTTP_GET,[](){
        if(!requireAuth())return;
        if(server.hasArg("val")){int v=constrain(server.arg("val").toInt(),CFG_POWERMODE_MIN,CFG_POWERMODE_MAX);state_lock();g_powerMode=v;state_unlock();storage_save_manual_settings_nvs();}
        server.send(200,"text/plain","OK");
    });
    server.on("/manual/smoke",HTTP_GET,[](){
        if(!requireAuth())return;
        if(server.hasArg("val")){int v=constrain(server.arg("val").toInt(),CFG_SMOKE_PWM_MIN,CFG_SMOKE_PWM_MAX);state_lock();g_manualSmokePwm=v;state_unlock();storage_save_manual_settings_nvs();}
        server.send(200,"text/plain","OK");
    });
    server.on("/manual/fan",HTTP_GET,[](){
        if(!requireAuth())return;
        if(server.hasArg("mode")){state_lock();g_fanMode=constrain(server.arg("mode").toInt(),0,2);state_unlock();}
        if(server.hasArg("on")){state_lock();g_fanOnTime=max(1000UL,(unsigned long)server.arg("on").toInt()*1000UL);state_unlock();}
        if(server.hasArg("off")){state_lock();g_fanOffTime=max(1000UL,(unsigned long)server.arg("off").toInt()*1000UL);state_unlock();}
        storage_save_manual_settings_nvs();
        server.send(200,"text/plain","OK");
    });

    server.on("/wifi",HTTP_GET,[](){
        if(!requireAuth())return;
        String html=String(HTML_WIFI);
        html.replace("SSID_PLACEHOLDER",String(storage_get_wifi_ssid()));
        server.send(200,"text/html",html);
    });
    server.on("/wifi/save",HTTP_POST,[](){
        if(!requireAuth())return;
        if(server.hasArg("ssid")&&server.hasArg("pass")){
            storage_save_wifi_nvs(server.arg("ssid").c_str(),server.arg("pass").c_str());
            WiFi.begin(storage_get_wifi_ssid(),storage_get_wifi_pass());
        }
        server.send(200,"text/html","<html><body style='background:#1a1a2e;color:#eee;padding:20px;font-family:sans-serif;'>⏳ Łączenie... <a href='/' style='color:#64b5f6;'>Wróć</a></body></html>");
    });

    // OTA
    server.on("/update",HTTP_POST,
        [](){
            if(!requireAuth())return;
            server.sendHeader("Connection","close");
            bool ok=!Update.hasError();
            server.send(200,"text/plain",ok?"OK":Update.errorString());
            if(ok){Serial.println("[OTA] OK – restarting");delay(500);ESP.restart();}
        },
        [](){
            if(!server.authenticate(storage_get_auth_user(),storage_get_auth_pass())){server.requestAuthentication(BASIC_AUTH,"Wedzarnia","Wymagane logowanie");return;}
            HTTPUpload& upload=server.upload();
            if(upload.status==UPLOAD_FILE_START){
                Serial.printf("[OTA] Start: %s\n",upload.filename.c_str());
                esp_task_wdt_config_t wdt_cfg={.timeout_ms=60000,.idle_core_mask=0,.trigger_panic=false};
                esp_task_wdt_reconfigure(&wdt_cfg);
                esp_task_wdt_reset();
                if(!Update.begin(UPDATE_SIZE_UNKNOWN)) Serial.printf("[OTA] begin error: %s\n",Update.errorString());
            } else if(upload.status==UPLOAD_FILE_WRITE){
                esp_task_wdt_reset();
                if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize) Serial.printf("[OTA] write error: %s\n",Update.errorString());
            } else if(upload.status==UPLOAD_FILE_END){
                esp_task_wdt_reset();
                if(Update.end(true)) Serial.printf("[OTA] End OK: %u bytes\n",upload.totalSize);
                else Serial.printf("[OTA] end error: %s\n",Update.errorString());
            } else if(upload.status==UPLOAD_FILE_ABORTED){
                Update.abort(); Serial.println("[OTA] Aborted");
            }
            yield();
        }
    );

    server.begin();
    Serial.println("Web server started");
}

void web_server_handle_client() {
    server.handleClient();
}
