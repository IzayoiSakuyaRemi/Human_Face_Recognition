import pathlib
html = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-P4 · 监控面板</title>
<style>
  @import url("https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;500&display=swap");

  :root {
    --bg: #0b0d14;
    --surface: #11131c;
    --surface-hover: #181b28;
    --border: rgba(255,255,255,0.05);
    --border-accent: rgba(255,255,255,0.08);
    --text: #ecedf0;
    --text-secondary: #7e8299;
    --text-muted: #4e5268;
    --gold: #e8b341;
    --gold-dim: rgba(232,179,65,0.10);
    --gold-glow: rgba(232,179,65,0.15);
    --cyan: #5bc0de;
    --cyan-dim: rgba(91,192,222,0.08);
    --purple: #a78bfa;
    --purple-dim: rgba(167,139,250,0.08);
    --green: #34d399;
    --green-dim: rgba(52,211,153,0.08);
    --radius: 16px;
    --radius-sm: 10px;
  }
  *{margin:0;padding:0;box-sizing:border-box}
  body {
    font-family: "Plus Jakarta Sans", -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .app {
    max-width: 900px;
    margin: 0 auto;
    padding: 48px 24px 80px;
  }

  /* ── Header ── */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 56px;
    position: relative;
  }
  .header::after {
    content: "";
    position: absolute;
    bottom: -28px;
    left: 0;
    right: 0;
    height: 1px;
    background: linear-gradient(90deg, var(--border), var(--border-accent), transparent);
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 14px;
  }
  .brand-mark {
    width: 36px;
    height: 36px;
    border-radius: 10px;
    background: var(--gold-dim);
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--gold);
    border: 1px solid rgba(232,179,65,0.15);
  }
  .brand-mark svg { width: 18px; height: 18px; }
  .brand-text h1 {
    font-size: 17px;
    font-weight: 700;
    letter-spacing: -0.3px;
  }
  .brand-text .sub {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 1px;
    letter-spacing: 0.3px;
  }
  .status-badge {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 7px 16px;
    border-radius: 100px;
    font-size: 13px;
    font-weight: 500;
    border: 1px solid var(--border);
    background: var(--surface);
    transition: all 0.3s ease;
    position: relative;
  }
  .status-badge.online {
    border-color: rgba(52,211,153,0.2);
    color: var(--green);
  }
  .status-badge.offline {
    border-color: rgba(248,113,113,0.2);
    color: #f87171;
  }
  .status-badge .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: currentColor;
    flex-shrink: 0;
  }
  .status-badge.online .dot {
    animation: stPulse 2s ease-in-out infinite;
  }
  @keyframes stPulse {
    0%,100%{opacity:1;box-shadow:0 0 0 0 currentColor}
    50%{opacity:0.5;box-shadow:0 0 0 4px transparent}
  }

  /* ── Hero Status ── */
  .hero {
    margin-bottom: 48px;
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 40px;
  }
  .hero-main {
    flex: 1;
  }
  .hero-label {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-muted);
    margin-bottom: 12px;
  }
  .hero-title {
    font-size: 42px;
    font-weight: 800;
    letter-spacing: -1px;
    line-height: 1.1;
    margin-bottom: 12px;
  }
  .hero-title .accent {
    background: linear-gradient(135deg, var(--gold), #f0c96a);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
  }
  .hero-meta {
    display: flex;
    align-items: center;
    gap: 20px;
    font-size: 14px;
    color: var(--text-secondary);
  }
  .hero-meta .sep {
    width: 3px;
    height: 3px;
    border-radius: 50%;
    background: var(--text-muted);
  }

  .hero-ring {
    flex-shrink: 0;
    width: 96px;
    height: 96px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    position: relative;
  }
  .hero-ring svg {
    width: 100%;
    height: 100%;
    transform: rotate(-90deg);
  }
  .hero-ring .bg-circle {
    fill: none;
    stroke: var(--border);
    stroke-width: 2;
  }
  .hero-ring .fg-circle {
    fill: none;
    stroke: var(--green);
    stroke-width: 2.5;
    stroke-linecap: round;
    stroke-dasharray: 219;
    transition: stroke-dashoffset 1s ease, stroke 0.3s ease;
  }
  .hero-ring .fg-circle.offline {
    stroke: #f87171;
  }
  .hero-ring-inner {
    position: absolute;
    text-align: center;
  }
  .hero-ring-inner .status-text {
    font-size: 11px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1px;
    color: var(--green);
    transition: color 0.3s;
  }
  .hero-ring-inner .status-text.offline {
    color: #f87171;
  }

  /* ── Metrics Row ── */
  .metrics {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 12px;
    margin-bottom: 48px;
  }
  .metric-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 24px;
    position: relative;
    overflow: hidden;
    transition: all 0.2s ease;
  }
  .metric-card:hover {
    border-color: var(--border-accent);
    background: var(--surface-hover);
  }
  .metric-card::before {
    content: "";
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    height: 2.5px;
    opacity: 0.65;
    transition: opacity 0.2s;
  }
  .metric-card:nth-child(1)::before { background: linear-gradient(90deg, var(--gold), transparent); }
  .metric-card:nth-child(2)::before { background: linear-gradient(90deg, var(--purple), transparent); }
  .metric-card:nth-child(3)::before { background: linear-gradient(90deg, var(--cyan), transparent); }
  .metric-card:hover::before { opacity: 1; }
  .metric-card .m-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 12px;
  }
  .metric-card .m-label {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1.5px;
    color: var(--text-muted);
  }
  .metric-card .m-icon {
    width: 28px;
    height: 28px;
    border-radius: 8px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 14px;
  }
  .metric-card:nth-child(1) .m-icon { background: var(--gold-dim); color: var(--gold); }
  .metric-card:nth-child(2) .m-icon { background: var(--purple-dim); color: var(--purple); }
  .metric-card:nth-child(3) .m-icon { background: var(--cyan-dim); color: var(--cyan); }
  .metric-card .m-value {
    font-family: "JetBrains Mono", monospace;
    font-size: 34px;
    font-weight: 500;
    letter-spacing: -1px;
    line-height: 1;
  }
  .metric-card:nth-child(1) .m-value { color: var(--gold); }
  .metric-card:nth-child(2) .m-value { color: var(--purple); }
  .metric-card:nth-child(3) .m-value { color: var(--cyan); }

  /* ── Events Section ── */
  .events-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 20px;
  }
  .events-header h2 {
    font-size: 13px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-secondary);
  }
  .events-header .refresh {
    font-size: 12px;
    color: var(--text-muted);
    font-family: "JetBrains Mono", monospace;
    font-variant-numeric: tabular-nums;
  }
  .events-header .refresh span {
    color: var(--gold);
    font-weight: 600;
  }

  .event-feed {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
  }

  .event-item {
    display: flex;
    align-items: stretch;
    padding: 0;
    border-bottom: 1px solid var(--border);
    transition: background 0.15s ease;
    cursor: pointer;
  }
  .event-item:last-child { border-bottom: none; }
  .event-item:hover { background: var(--surface-hover); }

  .event-timeline {
    width: 40px;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 16px 0;
    flex-shrink: 0;
    position: relative;
  }
  .event-timeline .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
    margin-top: 4px;
  }
  .event-timeline .dot.face { background: var(--gold); box-shadow: 0 0 8px var(--gold-glow); }
  .event-timeline .dot.voice { background: var(--purple); box-shadow: 0 0 8px rgba(167,139,250,0.3); }
  .event-timeline .dot.heart { background: var(--green); box-shadow: 0 0 8px rgba(52,211,153,0.3); }
  .event-timeline .line {
    flex: 1;
    width: 1px;
    background: var(--border);
    margin: 6px 0;
  }
  .event-item:last-child .event-timeline .line { display: none; }

  .event-body {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 20px 14px 0;
    min-width: 0;
    gap: 16px;
  }
  .event-info {
    min-width: 0;
    flex: 1;
  }
  .event-info .event-type {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.8px;
    margin-bottom: 2px;
  }
  .event-info .event-type.face { color: var(--gold); }
  .event-info .event-type.voice { color: var(--purple); }
  .event-info .event-type.heart { color: var(--green); }
  .event-info .event-detail {
    font-size: 13px;
    color: var(--text-secondary);
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
  .event-time {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    color: var(--text-muted);
    white-space: nowrap;
    flex-shrink: 0;
  }

  .empty-feed {
    text-align: center;
    padding: 60px 20px;
  }
  .empty-feed .empty-icon {
    font-size: 32px;
    display: block;
    margin-bottom: 10px;
    opacity: 0.2;
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
    background: rgba(0,0,0,0.55);
    backdrop-filter: blur(10px);
    align-items: center;
    justify-content: center;
  }
  .modal-overlay.active { display: flex; }
  .modal {
    background: #131624;
    border: 1px solid var(--border-accent);
    border-radius: 18px;
    padding: 32px 36px;
    max-width: 460px;
    width: 90vw;
    box-shadow: 0 24px 80px rgba(0,0,0,0.4);
    animation: mi 0.2s ease;
    position: relative;
  }
  @keyframes mi {
    from{opacity:0;transform:scale(0.96) translateY(8px)}
    to{opacity:1;transform:scale(1) translateY(0)}
  }
  .modal-close {
    position: absolute;
    top: 16px;
    right: 18px;
    background: none;
    border: none;
    color: var(--text-muted);
    font-size: 22px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 8px;
    transition: all 0.15s;
    line-height: 1;
  }
  .modal-close:hover {
    color: var(--text);
    background: rgba(255,255,255,0.04);
  }
  .modal h3 {
    font-size: 17px;
    font-weight: 700;
    margin-bottom: 24px;
    letter-spacing: -0.3px;
  }
  .modal-grid {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 12px 24px;
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
    .app { padding: 28px 16px 60px; }
    .hero { flex-direction: column; align-items: flex-start; gap: 24px; }
    .hero-title { font-size: 30px; }
    .metrics { grid-template-columns: 1fr; gap: 10px; }
    .metric-card .m-value { font-size: 28px; }
    .header { margin-bottom: 40px; }
    .header::after { bottom: -20px; }
    .event-body { padding: 12px 12px 12px 0; flex-wrap: wrap; }
    .event-time { width: 100%; padding-left: 0; }
    .modal { padding: 24px; }
  }
</style>
</head>
<body>
<div class="app">
  <!-- Header -->
  <div class="header">
    <div class="brand">
      <div class="brand-mark">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="8"/>
          <path d="M12 8v4l3 3"/>
          <circle cx="12" cy="12" r="2"/>
        </svg>
      </div>
      <div class="brand-text">
        <h1>ESP32-P4 Monitor</h1>
        <div class="sub">人脸识别系统 · 实时监控</div>
      </div>
    </div>
    <div class="status-badge" id="status">
      <span class="dot"></span>
      <span id="status-text">检测中...</span>
    </div>
  </div>

  <!-- Hero -->
  <div class="hero">
    <div class="hero-main">
      <div class="hero-label">设备状态</div>
      <div class="hero-title">
        <span class="accent">ESP32-P4</span><br>Neural Monitor
      </div>
      <div class="hero-meta">
        <span id="last-seen">等待心跳...</span>
        <span class="sep"></span>
        <span>v1.0 · site</span>
      </div>
    </div>
    <div class="hero-ring">
      <svg viewBox="0 0 72 72">
        <circle class="bg-circle" cx="36" cy="36" r="35"/>
        <circle class="fg-circle" id="ring-circle" cx="36" cy="36" r="35"/>
      </svg>
      <div class="hero-ring-inner">
        <div class="status-text" id="ring-label">离线</div>
      </div>
    </div>
  </div>

  <!-- Metrics -->
  <div class="metrics">
    <div class="metric-card">
      <div class="m-top">
        <span class="m-label">人脸识别</span>
        <span class="m-icon">&#x25CF;</span>
      </div>
      <div class="m-value" id="cnt-face">0</div>
    </div>
    <div class="metric-card">
      <div class="m-top">
        <span class="m-label">语音命令</span>
        <span class="m-icon">&#x25B6;</span>
      </div>
      <div class="m-value" id="cnt-voice">0</div>
    </div>
    <div class="metric-card">
      <div class="m-top">
        <span class="m-label">心跳</span>
        <span class="m-icon">&#x2661;</span>
      </div>
      <div class="m-value" id="cnt-heart">0</div>
    </div>
  </div>

  <!-- Events -->
  <div class="events-header">
    <h2>事件记录</h2>
    <span class="refresh"><span id="countdown">3</span>s</span>
  </div>
  <div class="event-feed" id="feed">
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
  var now=new Date();
  var diff=Math.round((now-d)/1000);
  if(diff<60)return diff+"秒前";
  if(diff<3600)return Math.round(diff/60)+"分钟前";
  if(diff<86400)return Math.round(diff/3600)+"小时前";
  return isNaN(d.getTime())?t:d.toLocaleString("zh-CN",{month:"short",day:"numeric",hour:"2-digit",minute:"2-digit"});
}

var tL={face_recognized:"人脸识别",face_recognize:"人脸识别",voice_command:"语音命令",heartbeat:"心跳"};
var tC={face_recognized:"face",face_recognize:"face",voice_command:"voice",heartbeat:"heart"};
var kL={id:"用户ID",user:"用户",action:"动作",cmd:"指令",command:"命令",uptime:"运行时长",wifi_rssi:"WiFi信号",rssi:"信号强度",time:"时间",name:"姓名",confidence:"置信度"};

async function fStatus(){
  try{
    var r=await fetch(API_STATUS),d=await r.json();
    var el=document.getElementById("status"),tx=document.getElementById("status-text");
    el.className="status-badge "+(d.online?"online":"offline");
    tx.textContent=d.online?"设备在线":"设备离线";

    var ring=document.getElementById("ring-circle");
    var rl=document.getElementById("ring-label");
    if(d.online){
      ring.classList.remove("offline");
      rl.classList.remove("offline");
      rl.textContent="在线";
      ring.style.strokeDashoffset="55";
    }else{
      ring.classList.add("offline");
      rl.classList.add("offline");
      rl.textContent="离线";
      ring.style.strokeDashoffset="160";
    }
    document.getElementById("last-seen").textContent=d.last_heartbeat?"最后活跃: "+fmt(d.last_heartbeat):"等待心跳...";
  }catch(e){}
}

async function fEvents(){
  try{
    var r=await fetch(API_EVENTS),data=await r.json(),ev=data.events||[];
    renderT(ev);renderS(ev);
  }catch(e){}
  countdown=REFRESH;
  document.getElementById("countdown").textContent=countdown;
}

function renderS(ev){
  var f=0,v=0,h=0,i;
  for(i=0;i<ev.length;i++){var e=ev[i];if(e.event_type==="face_recognized")f++;else if(e.event_type==="voice_command")v++;else if(e.event_type==="heartbeat")h++;}
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
  var l=ev[0];if(l.id>lastId)lastId=l.id;
  var h="";
  for(var i=0;i<ev.length;i++){
    var e=ev[i],dt="",ob={};
    try{
      var d=JSON.parse(e.detail||"{}");ob.parsed=d;
      var ps=[];for(var k in d)if(d.hasOwnProperty(k))ps.push((kL[k]||k)+"="+d[k]);
      dt=ps.join(", ");
    }catch(_){dt=e.detail||"-";}
    var cls=tC[e.event_type]||"face";
    var tmf=fmt(e.created_at);
    var fullTime=e.created_at?new Date(e.created_at.replace(" ","T")+"Z").toLocaleString("zh-CN",{hour12:false}):"-";
    var ds=esc(JSON.stringify(ob.parsed||{}));
    h+='<div class="event-item" data-detail=\''+ds+'" data-type="'+esc(e.event_type)+'" data-time="'+esc(fullTime)+'">';
    h+='<div class="event-timeline"><div class="dot '+cls+'"></div><div class="line"></div></div>';
    h+='<div class="event-body"><div class="event-info">';
    h+='<div class="event-type '+cls+'">'+(tL[e.event_type]||e.event_type)+"</div>";
    h+='<div class="event-detail" title="'+esc(dt)+'">'+esc(dt)+"</div></div>";
    h+='<div class="event-time">'+tmf+"</div></div></div>";
  }
  fb.innerHTML=h;
  var items=fb.querySelectorAll(".event-item");
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
