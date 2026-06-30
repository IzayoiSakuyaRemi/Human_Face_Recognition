import pathlib
html = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-P4 · NEURAL MONITOR</title>
<style>
  @import url("https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800;900&family=JetBrains+Mono:wght@400;500&display=swap");

  :root {
    --black: #0a0a0a;
    --white: #ffffff;
    --bg: #f4f4f0;
    --lime: #dfff00;
    --gray: #c4c6c9;
    --gray-light: #e5e6e3;
    --text: #111111;
    --text-2: #555555;
    --text-3: #888888;
    --green: #0a9b7d;
    --red: #d92626;
    --chrome: linear-gradient(135deg,#f0f0f0 0%,#d4d4d4 30%,#ffffff 50%,#c0c0c0 70%,#e8e8e8 100%);
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }
  html { scroll-behavior: smooth; }
  body {
    font-family: "Inter", -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    -webkit-font-smoothing: antialiased;
    overflow-x: hidden;
  }

  /* tech grid background */
  .grid-tech {
    position: fixed;
    inset: 0;
    pointer-events: none;
    background-image:
      linear-gradient(var(--gray-light) 1px, transparent 1px),
      linear-gradient(90deg, var(--gray-light) 1px, transparent 1px);
    background-size: 80px 80px;
    opacity: 0.3;
    z-index: 0;
  }

  .wrap {
    position: relative;
    z-index: 1;
    max-width: 1200px;
    margin: 0 auto;
    padding: 32px 24px 80px;
  }

  /* chrome blobs */
  .blob {
    position: fixed;
    border-radius: 40% 60% 70% 30% / 40% 50% 60% 50%;
    background: var(--chrome);
    box-shadow: inset -20px -20px 40px rgba(0,0,0,0.08), inset 20px 20px 40px rgba(255,255,255,0.8);
    pointer-events: none;
    z-index: 0;
    filter: blur(0.5px);
  }
  .blob.b1 { width: 340px; height: 260px; top: 5%; right: -60px; opacity: 0.6; animation: m1 14s ease-in-out infinite alternate; }
  .blob.b2 { width: 180px; height: 180px; top: 45%; left: -40px; opacity: 0.45; animation: m2 16s ease-in-out infinite alternate; }
  .blob.b3 { width: 260px; height: 200px; bottom: 5%; right: 8%; opacity: 0.35; animation: m3 18s ease-in-out infinite alternate; }
  @keyframes m1 { 0%{border-radius:40% 60% 70% 30% / 40% 50% 60% 50%; transform:rotate(0)} 100%{border-radius:60% 40% 30% 70% / 50% 60% 40% 50%; transform:rotate(18deg)} }
  @keyframes m2 { 0%{border-radius:30% 70% 50% 50% / 50% 30% 70% 50%; transform:rotate(0)} 100%{border-radius:70% 30% 40% 60% / 30% 60% 40% 70%; transform:rotate(-15deg)} }
  @keyframes m3 { 0%{border-radius:50% 50% 60% 40% / 60% 40% 50% 50%; transform:rotate(0)} 100%{border-radius:40% 60% 40% 60% / 50% 60% 30% 70%; transform:rotate(12deg)} }

  /* top bar */
  .topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 24px;
    padding-bottom: 20px;
    border-bottom: 1px solid var(--gray);
  }
  .logo {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--text-2);
  }
  .logo span {
    display: inline-block;
    background: var(--lime);
    color: var(--black);
    padding: 4px 8px;
    margin-right: 8px;
    font-weight: 800;
  }
  .top-status {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 13px;
    font-weight: 600;
    color: var(--text-2);
    text-transform: uppercase;
    letter-spacing: 1px;
  }
  .top-status .dot {
    width: 9px; height: 9px;
    border-radius: 50%;
    transition: background 0.3s;
  }
  .top-status .dot.on { background: var(--green); box-shadow: 0 0 0 4px rgba(10,155,125,0.15); }
  .top-status .dot.off { background: var(--red); }

  /* hero */
  .hero {
    margin-bottom: 48px;
    position: relative;
  }
  .hero-tag {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    color: var(--text-3);
    letter-spacing: 3px;
    text-transform: uppercase;
    margin-bottom: 16px;
  }
  .hero h1 {
    font-size: 110px;
    font-weight: 900;
    letter-spacing: -7px;
    line-height: 0.85;
    text-transform: uppercase;
    margin-bottom: 20px;
  }
  .hero h1 .lime-back {
    position: relative;
    display: inline-block;
  }
  .hero h1 .lime-back::before {
    content: "";
    position: absolute;
    left: -8px; right: -8px; top: 25%; bottom: 12%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-5deg) rotate(-1deg);
  }
  .hero-sub {
    display: flex;
    align-items: center;
    gap: 20px;
    flex-wrap: wrap;
  }
  .hero-sub .chip {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    padding: 8px 18px;
    border: 1px solid var(--black);
    background: var(--white);
    text-transform: uppercase;
    letter-spacing: 1px;
  }
  .hero-sub .meta {
    font-size: 14px;
    color: var(--text-3);
  }

  /* main layout: asymmetric grid */
  .main-grid {
    display: grid;
    grid-template-columns: 280px 1fr;
    gap: 24px;
    align-items: start;
  }

  /* left metrics column */
  .metrics-stack {
    display: flex;
    flex-direction: column;
    gap: 16px;
    position: sticky;
    top: 24px;
  }
  .m-card {
    background: var(--white);
    border: 1px solid var(--black);
    padding: 24px 20px;
    position: relative;
    transition: transform 0.2s, box-shadow 0.2s;
  }
  .m-card:hover {
    transform: translate(-3px, -3px);
    box-shadow: 6px 6px 0 var(--black);
  }
  .m-card .m-tag {
    font-family: "JetBrains Mono", monospace;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-3);
    margin-bottom: 12px;
  }
  .m-card .m-num {
    font-size: 44px;
    font-weight: 800;
    letter-spacing: -3px;
    line-height: 1;
    margin-bottom: 4px;
  }
  .m-card .m-label {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-2);
  }
  .m-card .m-accent {
    position: absolute;
    top: 0; left: 0;
    width: 6px; height: 100%;
    background: var(--lime);
  }
  .m-card:nth-child(2) .m-accent { background: #a78bfa; }
  .m-card:nth-child(3) .m-accent { background: var(--green); }

  /* right events panel */
  .events-panel {
    background: var(--white);
    border: 1px solid var(--black);
    position: relative;
  }
  .events-panel::before {
    content: "";
    position: absolute;
    top: 6px; left: 6px;
    right: -6px; bottom: -6px;
    background: var(--lime);
    z-index: -1;
  }
  .events-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 20px 24px;
    border-bottom: 1px solid var(--black);
  }
  .events-header h2 {
    font-size: 14px;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 2px;
  }
  .events-header .refresh {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    color: var(--text-3);
  }
  .events-header .refresh span { color: var(--text); font-weight: 700; }
  .feed {
    max-height: 520px;
    overflow-y: auto;
    padding: 8px 0;
  }
  .feed .row {
    display: grid;
    grid-template-columns: 110px 1fr 110px;
    gap: 16px;
    align-items: center;
    padding: 14px 24px;
    border-bottom: 1px solid var(--gray-light);
    transition: background 0.12s;
    cursor: pointer;
  }
  .feed .row:last-child { border-bottom: none; }
  .feed .row:hover { background: rgba(223,255,0,0.08); }
  .feed .row .tag {
    font-family: "JetBrains Mono", monospace;
    font-size: 10px;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 1px;
    padding: 4px 0;
    border-bottom: 2px solid;
    width: fit-content;
  }
  .feed .row .tag.f { border-color: var(--lime); }
  .feed .row .tag.v { border-color: #a78bfa; }
  .feed .row .tag.h { border-color: var(--green); }
  .feed .row .detail {
    font-size: 14px;
    color: var(--text-2);
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
  .feed .row .time {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    color: var(--text-3);
    text-align: right;
  }
  .empty-feed {
    padding: 60px 24px;
    text-align: center;
    color: var(--text-3);
    font-size: 14px;
  }

  /* yellow connector bars */
  .connector {
    position: fixed;
    background: var(--lime);
    pointer-events: none;
    z-index: 0;
  }
  .connector.c1 { width: 100px; height: 12px; top: 32%; left: 5%; transform: rotate(-8deg); opacity: 0.85; }
  .connector.c2 { width: 60px; height: 8px; top: 58%; right: 6%; transform: rotate(6deg); opacity: 0.7; }
  .connector.c3 { width: 80px; height: 10px; bottom: 15%; left: 8%; transform: rotate(-4deg); opacity: 0.6; }

  /* modal */
  .modal-overlay {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 100;
    background: rgba(0,0,0,0.25);
    backdrop-filter: blur(6px);
    align-items: center;
    justify-content: center;
  }
  .modal-overlay.active { display: flex; }
  .modal {
    background: var(--white);
    border: 2px solid var(--black);
    padding: 32px 36px;
    max-width: 460px;
    width: 90vw;
    position: relative;
    animation: pop 0.2s ease;
  }
  @keyframes pop { from{opacity:0;transform:scale(0.96)} to{opacity:1;transform:scale(1)} }
  .modal::before {
    content: "";
    position: absolute;
    top: -8px; left: 8px;
    right: -8px; bottom: 8px;
    background: var(--lime);
    z-index: -1;
  }
  .modal-close {
    position: absolute;
    top: 10px; right: 14px;
    background: none; border: none;
    font-size: 22px; cursor: pointer;
    color: var(--text);
  }
  .modal h3 {
    font-size: 20px;
    font-weight: 800;
    margin-bottom: 24px;
    text-transform: uppercase;
    letter-spacing: -0.5px;
  }
  .modal-grid {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 10px 28px;
    font-size: 14px;
  }
  .modal-grid .k { color: var(--text-3); font-weight: 700; text-transform: uppercase; font-size: 11px; letter-spacing: 0.5px; }
  .modal-grid .v { color: var(--text); word-break: break-all; }

  @media (max-width: 900px) {
    .main-grid { grid-template-columns: 1fr; }
    .metrics-stack { position: static; flex-direction: row; overflow-x: auto; }
    .m-card { min-width: 160px; flex: 1; }
    .hero h1 { font-size: 64px; letter-spacing: -4px; }
  }
  @media (max-width: 600px) {
    .hero h1 { font-size: 44px; letter-spacing: -2px; }
    .feed .row { grid-template-columns: 80px 1fr; gap: 6px 12px; padding: 12px 16px; }
    .feed .row .time { grid-column: 1 / -1; text-align: left; }
  }
</style>
</head>
<body>

<div class="grid-tech"></div>
<div class="blob b1"></div>
<div class="blob b2"></div>
<div class="blob b3"></div>
<div class="connector c1"></div>
<div class="connector c2"></div>
<div class="connector c3"></div>

<div class="wrap">
  <!-- top bar -->
  <div class="topbar">
    <div class="logo"><span>ESP</span>Neural Monitor</div>
    <div class="top-status">
      <span class="dot" id="hdot"></span>
      <span id="hstatus">检测中</span>
    </div>
  </div>

  <!-- hero -->
  <div class="hero">
    <div class="hero-tag">Device Status</div>
    <h1>ESP32<span class="lime-back">-P4</span></h1>
    <div class="hero-sub">
      <div class="chip" id="hero-status-text">离线</div>
      <div class="meta" id="last-seen">LAST HEARTBEAT --</div>
    </div>
  </div>

  <!-- asymmetric main -->
  <div class="main-grid">
    <!-- left: vertical metrics -->
    <div class="metrics-stack">
      <div class="m-card">
        <div class="m-accent"></div>
        <div class="m-tag">01 / Face</div>
        <div class="m-num" id="cnt-face">0</div>
        <div class="m-label">人脸识别</div>
      </div>
      <div class="m-card">
        <div class="m-accent"></div>
        <div class="m-tag">02 / Voice</div>
        <div class="m-num" id="cnt-voice">0</div>
        <div class="m-label">语音命令</div>
      </div>
      <div class="m-card">
        <div class="m-accent"></div>
        <div class="m-tag">03 / Heart</div>
        <div class="m-num" id="cnt-heart">0</div>
        <div class="m-label">心跳</div>
      </div>
    </div>

    <!-- right: events panel -->
    <div class="events-panel">
      <div class="events-header">
        <h2>Event Stream</h2>
        <div class="refresh"><span id="countdown">3</span>s refresh</div>
      </div>
      <div class="feed" id="feed">
        <div class="empty-feed">暂无事件，等待设备上报</div>
      </div>
    </div>
  </div>
</div>

<!-- Modal -->
<div class="modal-overlay" id="modal">
  <div class="modal">
    <button class="modal-close" id="modal-close">&times;</button>
    <h3 id="modal-title">Event Detail</h3>
    <div class="modal-grid" id="modal-body"></div>
  </div>
</div>

<script>
var API_EVENTS="/api/events?limit=20";
var API_STATUS="/api/status";
var REFRESH=3,countdown=REFRESH,lastId=0;

function fmt(t){
  if(!t)return"--";
  var d=new Date(t.replace(" ","T")+"Z");
  if(isNaN(d.getTime()))return t;
  var now=new Date(),diff=Math.round((now-d)/1000);
  if(diff<60)return diff+"s ago";
  if(diff<3600)return Math.round(diff/60)+"m ago";
  if(diff<86400)return Math.round(diff/3600)+"h ago";
  return d.toLocaleString("zh-CN",{month:"short",day:"numeric",hour:"2-digit",minute:"2-digit"});
}

var tL={face_recognized:"人脸识别",face_recognize:"人脸识别",voice_command:"语音命令",heartbeat:"心跳"};
var tCls={face_recognized:"f",face_recognize:"f",voice_command:"v",heartbeat:"h"};
var kL={id:"用户ID",user:"用户",action:"动作",cmd:"指令",command:"命令",uptime:"运行时长",wifi_rssi:"WiFi信号",rssi:"信号强度",time:"时间",name:"姓名",confidence:"置信度"};

async function fStatus(){
  try{
    var r=await fetch(API_STATUS),d=await r.json();
    var on=d.online;
    document.getElementById("hdot").className="dot "+(on?"on":"off");
    document.getElementById("hstatus").textContent=on?"在线":"离线";
    document.getElementById("hero-status-text").textContent=on?"设备在线":"设备离线";
    document.getElementById("last-seen").textContent=d.last_heartbeat?"LAST HEARTBEAT "+fmt(d.last_heartbeat):"LAST HEARTBEAT --";
  }catch(e){}
}

async function fEvents(){
  try{
    var r=await fetch(API_EVENTS),data=await r.json(),ev=data.events||[];
    renderT(ev);renderS(ev);
  }catch(e){}
  countdown=REFRESH;document.getElementById("countdown").textContent=countdown;
}

function renderS(ev){
  var f=0,v=0,h=0;
  for(var i=0;i<ev.length;i++){var e=ev[i];if(e.event_type==="face_recognized")f++;else if(e.event_type==="voice_command")v++;else if(e.event_type==="heartbeat")h++;}
  document.getElementById("cnt-face").textContent=f;
  document.getElementById("cnt-voice").textContent=v;
  document.getElementById("cnt-heart").textContent=h;
}

function esc(s){return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;");}

function renderT(ev){
  var fb=document.getElementById("feed");
  if(!ev.length){fb.innerHTML='<div class="empty-feed">暂无事件，等待设备上报</div>';return;}
  if(ev[0].id>lastId)lastId=ev[0].id;
  var h="";
  for(var i=0;i<ev.length;i++){
    var e=ev[i],dt="",ob={};
    try{
      var d=JSON.parse(e.detail||"{}");ob.parsed=d;
      var ps=[];for(var k in d)if(d.hasOwnProperty(k))ps.push((kL[k]||k)+"="+d[k]);
      dt=ps.join(", ");
    }catch(_){dt=e.detail||"-";}
    var tc=tCls[e.event_type]||"f";
    var tmf=fmt(e.created_at);
    var fullTime=e.created_at?new Date(e.created_at.replace(" ","T")+"Z").toLocaleString("zh-CN",{hour12:false}):"-";
    var ds=esc(JSON.stringify(ob.parsed||{}));
    h+='<div class="row" data-detail=\''+ds+'" data-type="'+esc(e.event_type)+'" data-time="'+esc(fullTime)+'">';
    h+='<div class="tag '+tc+'">'+(tL[e.event_type]||e.event_type)+"</div>";
    h+='<div class="detail" title="'+esc(dt)+'">'+esc(dt)+"</div>";
    h+='<div class="time">'+tmf+"</div></div>";
  }
  fb.innerHTML=h;
  var rs=fb.querySelectorAll(".row");
  for(var i=0;i<rs.length;i++){
    rs[i].addEventListener("click",function(ev2){
      var ds2=ev2.currentTarget.dataset.detail,et=ev2.currentTarget.dataset.type,etm=ev2.currentTarget.dataset.time;
      document.getElementById("modal-title").textContent=(tL[et]||et)+" Detail";
      var bd=document.getElementById("modal-body");
      var h2='<div class="k">类型</div><div class="v">'+(tL[et]||et)+"</div>";
      h2+='<div class="k">时间</div><div class="v">'+etm+"</div>";
      try{
        var d2=JSON.parse(ds2);
        for(var k in d2)if(d2.hasOwnProperty(k))h2+='<div class="k">'+esc(kL[k]||k)+'</div><div class="v">'+esc(String(d2[k]))+"</div>";
      }catch(_){}
      bd.innerHTML=h2;
      document.getElementById("modal").classList.add("active");
    });
  }
}

document.getElementById("modal-close").addEventListener("click",function(){document.getElementById("modal").classList.remove("active");});
document.getElementById("modal").addEventListener("click",function(e){if(e.target===e.currentTarget)document.getElementById("modal").classList.remove("active");});

fEvents();fStatus();
setInterval(function(){countdown--;document.getElementById("countdown").textContent=countdown;if(countdown<=0){fEvents();fStatus();}},1000);
setInterval(function(){fStatus();},15000);
</script>
</body>
</html>
"""
pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html").write_text(html, encoding="utf-8")
print("Written", len(html), "bytes")
