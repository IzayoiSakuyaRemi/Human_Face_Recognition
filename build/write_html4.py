import pathlib
html = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-P4 · 监控面板</title>
<style>
  @import url("https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;600&display=swap");

  :root {
    --bg: #f0f1f4;
    --surface: #ffffff;
    --border: #e2e4e9;
    --text: #12141a;
    --text-secondary: #5e6378;
    --text-muted: #9499ad;
    --accent: #1e3a5f;
    --accent-light: rgba(30,58,95,0.04);
    --accent-glow: rgba(30,58,95,0.08);
    --gold: #b8860b;
    --gold-light: rgba(184,134,11,0.06);
    --gold-border: rgba(184,134,11,0.15);
    --green: #0d9488;
    --green-light: rgba(13,148,136,0.07);
    --purple: #7c3aed;
    --purple-light: rgba(124,58,237,0.06);
    --red: #dc2626;
    --shadow-sm: 0 1px 3px rgba(0,0,0,0.04);
    --shadow-md: 0 4px 16px rgba(0,0,0,0.05);
    --shadow-lg: 0 12px 40px rgba(0,0,0,0.07);
    --radius: 16px;
  }
  *{margin:0;padding:0;box-sizing:border-box}
  body {
    font-family: "Inter", -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .app {
    max-width: 860px;
    margin: 0 auto;
    padding: 36px 20px 80px;
  }

  /* ── Header ── */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 32px;
  }
  .header-left {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .brand-icon {
    width: 36px;
    height: 36px;
    border-radius: 10px;
    background: var(--accent);
    display: flex;
    align-items: center;
    justify-content: center;
    color: white;
  }
  .brand-icon svg { width: 18px; height: 18px; }
  .brand-name {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: -0.3px;
  }
  .brand-name .sub {
    font-size: 12px;
    font-weight: 400;
    color: var(--text-secondary);
    margin-left: 8px;
  }
  .header-status {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 13px;
    font-weight: 500;
    color: var(--text-secondary);
  }
  .header-status .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    transition: background 0.3s;
  }
  .header-status .dot.on { background: var(--green); box-shadow: 0 0 8px rgba(13,148,136,0.3); }
  .header-status .dot.off { background: var(--red); }

  /* ── Hero Card ── */
  .hero {
    background: var(--surface);
    border-radius: var(--radius);
    border: 1px solid var(--border);
    box-shadow: var(--shadow-sm);
    padding: 36px 40px;
    margin-bottom: 28px;
    position: relative;
    overflow: hidden;
  }
  .hero::before {
    content: "";
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    height: 3px;
    background: linear-gradient(90deg, var(--accent), var(--gold), transparent 60%);
  }
  .hero-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 24px;
  }
  .hero-device {
    display: flex;
    align-items: center;
    gap: 14px;
  }
  .hero-device .icon-wrap {
    width: 48px;
    height: 48px;
    border-radius: 14px;
    background: var(--accent-light);
    border: 1px solid var(--accent-glow);
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--accent);
  }
  .hero-device .icon-wrap svg { width: 24px; height: 24px; }
  .hero-device .device-info .device-name {
    font-size: 22px;
    font-weight: 800;
    letter-spacing: -0.5px;
    line-height: 1.2;
  }
  .hero-device .device-info .device-tag {
    font-size: 12px;
    color: var(--text-muted);
    margin-top: 2px;
  }
  .hero-status {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 10px 20px;
    border-radius: 100px;
    font-size: 14px;
    font-weight: 600;
    transition: all 0.3s;
    border: 1px solid var(--border);
  }
  .hero-status.on {
    background: var(--green-light);
    border-color: rgba(13,148,136,0.15);
    color: var(--green);
  }
  .hero-status.off {
    background: rgba(220,38,38,0.05);
    border-color: rgba(220,38,38,0.12);
    color: var(--red);
  }
  .hero-status .big-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: currentColor;
  }
  .hero-status.on .big-dot {
    animation: hp 1.5s ease-in-out infinite;
  }
  @keyframes hp {
    0%,100%{opacity:1;box-shadow:0 0 0 0 currentColor}
    50%{opacity:0.6;box-shadow:0 0 0 6px transparent}
  }

  .hero-stats {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 12px;
  }
  .hero-stat {
    background: var(--bg);
    border-radius: 12px;
    padding: 20px 20px 16px;
    text-align: center;
    transition: all 0.2s;
    border: 1px solid transparent;
  }
  .hero-stat:hover {
    border-color: var(--border);
    background: var(--surface);
    box-shadow: var(--shadow-sm);
  }
  .hero-stat .hs-num {
    font-family: "JetBrains Mono", monospace;
    font-size: 38px;
    font-weight: 600;
    letter-spacing: -1.5px;
    line-height: 1;
    margin-bottom: 6px;
  }
  .hero-stat .hs-label {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1.2px;
    color: var(--text-muted);
  }
  .hero-stat:nth-child(1) .hs-num { color: var(--accent); }
  .hero-stat:nth-child(2) .hs-num { color: var(--purple); }
  .hero-stat:nth-child(3) .hs-num { color: var(--green); }

  .hero-footer {
    margin-top: 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-top: 16px;
    border-top: 1px solid var(--border);
  }
  .hero-footer .last-seen {
    font-size: 13px;
    color: var(--text-muted);
  }
  .hero-footer .last-seen span {
    color: var(--text-secondary);
    font-weight: 500;
  }

  /* ── Events Section ── */
  .events-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
    padding: 0 4px;
  }
  .events-head h2 {
    font-size: 15px;
    font-weight: 700;
    color: var(--text);
  }
  .events-head .refresh {
    font-size: 13px;
    color: var(--text-muted);
    font-family: "JetBrains Mono", monospace;
    font-variant-numeric: tabular-nums;
  }
  .events-head .refresh span {
    color: var(--accent);
    font-weight: 600;
  }

  .feed {
    background: var(--surface);
    border-radius: var(--radius);
    border: 1px solid var(--border);
    box-shadow: var(--shadow-sm);
    overflow: hidden;
  }

  /* Timeline event items */
  .ev-item {
    display: flex;
    align-items: stretch;
    border-bottom: 1px solid var(--border);
    cursor: pointer;
    transition: background 0.12s ease;
  }
  .ev-item:last-child { border-bottom: none; }
  .ev-item:hover { background: var(--accent-light); }

  .ev-line {
    width: 44px;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 14px 0;
    flex-shrink: 0;
    position: relative;
  }
  .ev-line .dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    flex-shrink: 0;
    margin-top: 5px;
    border: 2px solid transparent;
  }
  .ev-line .dot.face {
    background: var(--accent);
    border-color: var(--accent-glow);
  }
  .ev-line .dot.voice {
    background: var(--purple);
    border-color: rgba(124,58,237,0.15);
  }
  .ev-line .dot.heart {
    background: var(--green);
    border-color: rgba(13,148,136,0.15);
  }
  .ev-line .bar {
    flex: 1;
    width: 1px;
    background: var(--border);
    margin: 5px 0;
  }
  .ev-item:last-child .ev-line .bar { display: none; }

  .ev-body {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 20px 14px 0;
    min-width: 0;
    gap: 16px;
  }
  .ev-info {
    min-width: 0;
    flex: 1;
  }
  .ev-info .ev-type {
    font-size: 13px;
    font-weight: 600;
    margin-bottom: 2px;
  }
  .ev-info .ev-type.f { color: var(--accent); }
  .ev-info .ev-type.v { color: var(--purple); }
  .ev-info .ev-type.h { color: var(--green); }
  .ev-info .ev-detail {
    font-size: 13px;
    color: var(--text-secondary);
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
  .ev-time {
    font-family: "JetBrains Mono", monospace;
    font-size: 13px;
    color: var(--text-muted);
    white-space: nowrap;
    flex-shrink: 0;
    font-variant-numeric: tabular-nums;
  }

  .empty-feed {
    text-align: center;
    padding: 50px 20px;
  }
  .empty-feed .empty-icon {
    font-size: 24px;
    display: block;
    margin-bottom: 8px;
    opacity: 0.25;
  }
  .empty-feed .empty-text {
    font-size: 14px;
    color: var(--text-muted);
  }

  /* ── Modal ── */
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
    background: var(--surface);
    border-radius: 18px;
    padding: 28px 32px;
    max-width: 460px;
    width: 90vw;
    box-shadow: var(--shadow-lg);
    border: 1px solid var(--border);
    animation: mi 0.18s ease;
    position: relative;
  }
  @keyframes mi {
    from{opacity:0;transform:scale(0.97) translateY(6px)}
    to{opacity:1;transform:scale(1) translateY(0)}
  }
  .modal-close {
    position: absolute;
    top: 16px;
    right: 18px;
    background: none;
    border: none;
    color: var(--text-muted);
    font-size: 20px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 8px;
    transition: all 0.15s;
    line-height: 1;
  }
  .modal-close:hover {
    color: var(--text);
    background: var(--accent-light);
  }
  .modal h3 {
    font-size: 16px;
    font-weight: 700;
    margin-bottom: 20px;
    letter-spacing: -0.3px;
  }
  .modal-grid {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 10px 24px;
    font-size: 14px;
  }
  .modal-grid .k {
    color: var(--text-muted);
    font-weight: 500;
  }
  .modal-grid .v {
    color: var(--text);
    word-break: break-all;
  }

  @media (max-width: 700px) {
    .app { padding: 24px 14px 60px; }
    .hero { padding: 24px; }
    .hero-device .device-info .device-name { font-size: 18px; }
    .hero-status { font-size: 12px; padding: 8px 14px; }
    .hero-stats { grid-template-columns: repeat(3, 1fr); gap: 8px; }
    .hero-stat { padding: 14px 10px; }
    .hero-stat .hs-num { font-size: 28px; }
    .ev-body { padding: 12px 12px 12px 0; }
    .modal { padding: 20px; }
    .header-status .status-text { display: none; }
  }
</style>
</head>
<body>
<div class="app">
  <!-- Header -->
  <div class="header">
    <div class="header-left">
      <div class="brand-icon">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="8"/>
          <path d="M12 8v4l2.5 2.5"/>
        </svg>
      </div>
      <div class="brand-name">
        ESP32-P4
        <span class="sub">Monitoring</span>
      </div>
    </div>
    <div class="header-status">
      <span class="dot" id="hdot"></span>
      <span class="status-text" id="hstatus">检测中</span>
    </div>
  </div>

  <!-- Hero -->
  <div class="hero">
    <div class="hero-top">
      <div class="hero-device">
        <div class="icon-wrap">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
            <rect x="2" y="2" width="20" height="8" rx="2"/>
            <rect x="2" y="14" width="20" height="8" rx="2"/>
            <circle cx="6" cy="6" r="1" fill="currentColor"/>
            <circle cx="6" cy="18" r="1" fill="currentColor"/>
          </svg>
        </div>
        <div class="device-info">
          <div class="device-name">Neural Monitor</div>
          <div class="device-tag">人脸识别 &middot; 语音 &middot; 实时监控</div>
        </div>
      </div>
      <div class="hero-status" id="hero-status">
        <span class="big-dot"></span>
        <span id="hero-status-text">离线</span>
      </div>
    </div>
    <div class="hero-stats">
      <div class="hero-stat">
        <div class="hs-num" id="cnt-face">0</div>
        <div class="hs-label">人脸识别</div>
      </div>
      <div class="hero-stat">
        <div class="hs-num" id="cnt-voice">0</div>
        <div class="hs-label">语音命令</div>
      </div>
      <div class="hero-stat">
        <div class="hs-num" id="cnt-heart">0</div>
        <div class="hs-label">心跳</div>
      </div>
    </div>
    <div class="hero-footer">
      <div class="last-seen">最后活跃: <span id="last-seen">-</span></div>
    </div>
  </div>

  <!-- Events -->
  <div class="events-head">
    <h2>事件记录</h2>
    <span class="refresh"><span id="countdown">3</span>s 刷新</span>
  </div>
  <div class="feed" id="feed">
    <div class="empty-feed">
      <span class="empty-icon">&#x25CB;</span>
      <span class="empty-text">暂无事件，等待设备上报</span>
    </div>
  </div>
</div>

<!-- Modal -->
<div class="modal-overlay" id="modal">
  <div class="modal">
    <button class="modal-close" id="modal-close">&times;</button>
    <h3 id="modal-title">事件详情</h3>
    <div class="modal-grid" id="modal-body"></div>
  </div>
</div>

<script>
var API_EVENTS="/api/events?limit=20";
var API_STATUS="/api/status";
var REFRESH=3,countdown=REFRESH,lastId=0;

function fmt(t){
  if(!t)return"-";
  var d=new Date(t.replace(" ","T")+"Z");
  if(isNaN(d.getTime()))return t;
  var now=new Date(),diff=Math.round((now-d)/1000);
  if(diff<60)return diff+"秒前";
  if(diff<3600)return Math.round(diff/60)+"分钟前";
  if(diff<86400)return Math.round(diff/3600)+"小时前";
  return d.toLocaleString("zh-CN",{month:"short",day:"numeric",hour:"2-digit",minute:"2-digit"});
}

var tL={face_recognized:"人脸识别",face_recognize:"人脸识别",voice_command:"语音命令",heartbeat:"心跳"};
var tC={face_recognized:"face",face_recognize:"face",voice_command:"voice",heartbeat:"heart"};
var tCls={face_recognized:"f",face_recognize:"f",voice_command:"v",heartbeat:"h"};
var kL={id:"用户ID",user:"用户",action:"动作",cmd:"指令",command:"命令",uptime:"运行时长",wifi_rssi:"WiFi信号",rssi:"信号强度",time:"时间",name:"姓名",confidence:"置信度"};

async function fStatus(){
  try{
    var r=await fetch(API_STATUS),d=await r.json();
    var on=d.online;
    // header dot
    var hd=document.getElementById("hdot");
    var hs=document.getElementById("hstatus");
    hd.className="dot "+(on?"on":"off");
    hs.textContent=on?"在线":"离线";
    // hero status
    var hs2=document.getElementById("hero-status");
    var hs3=document.getElementById("hero-status-text");
    hs2.className="hero-status "+(on?"on":"off");
    hs3.textContent=on?"设备在线":"设备离线";
    document.getElementById("last-seen").textContent=d.last_heartbeat?fmt(d.last_heartbeat):"-";
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
  if(!ev.length){
    fb.innerHTML='<div class="empty-feed"><span class="empty-icon">&#x25CB;</span><span class="empty-text">暂无事件，等待设备上报</span></div>';
    return;
  }
  if(ev[0].id>lastId)lastId=ev[0].id;
  var h="";
  for(var i=0;i<ev.length;i++){
    var e=ev[i],dt="",ob={};
    try{
      var d=JSON.parse(e.detail||"{}");ob.parsed=d;
      var ps=[];for(var k in d)if(d.hasOwnProperty(k))ps.push((kL[k]||k)+"="+d[k]);
      dt=ps.join(", ");
    }catch(_){dt=e.detail||"-";}
    var dc=tC[e.event_type]||"face";
    var tc=tCls[e.event_type]||"f";
    var tmf=fmt(e.created_at);
    var fullTime=e.created_at?new Date(e.created_at.replace(" ","T")+"Z").toLocaleString("zh-CN",{hour12:false}):"-";
    var ds=esc(JSON.stringify(ob.parsed||{}));
    h+='<div class="ev-item" data-detail=\''+ds+'" data-type="'+esc(e.event_type)+'" data-time="'+esc(fullTime)+'">';
    h+='<div class="ev-line"><div class="dot '+dc+'"></div><div class="bar"></div></div>';
    h+='<div class="ev-body"><div class="ev-info">';
    h+='<div class="ev-type '+tc+'">'+(tL[e.event_type]||e.event_type)+"</div>";
    h+='<div class="ev-detail" title="'+esc(dt)+'">'+esc(dt)+"</div></div>";
    h+='<div class="ev-time">'+tmf+"</div></div></div>";
  }
  fb.innerHTML=h;
  var items=fb.querySelectorAll(".ev-item");
  for(var i=0;i<items.length;i++){
    items[i].addEventListener("click",function(ev2){
      var ds2=ev2.currentTarget.dataset.detail,et=ev2.currentTarget.dataset.type,etm=ev2.currentTarget.dataset.time;
      document.getElementById("modal-title").textContent=(tL[et]||et)+" · 详情";
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
