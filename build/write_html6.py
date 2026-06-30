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
    --lime-2: #e8ff4d;
    --gray: #c4c6c9;
    --gray-light: #e5e6e3;
    --text: #111111;
    --text-2: #555555;
    --text-3: #999999;
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

  .snap {
    height: 100vh;
    overflow-y: scroll;
    scroll-snap-type: y mandatory;
    scroll-behavior: smooth;
  }

  .module {
    min-height: 100vh;
    scroll-snap-align: start;
    scroll-snap-stop: always;
    position: relative;
    display: flex;
    flex-direction: column;
    justify-content: center;
    padding: 40px 24px;
    overflow: hidden;
  }

  /* technical grid overlay */
  .grid-tech {
    position: absolute;
    inset: 0;
    pointer-events: none;
    background-image:
      linear-gradient(var(--gray-light) 1px, transparent 1px),
      linear-gradient(90deg, var(--gray-light) 1px, transparent 1px);
    background-size: 60px 60px;
    opacity: 0.35;
  }

  .corner-mark {
    position: absolute;
    width: 60px;
    height: 60px;
    pointer-events: none;
  }
  .corner-mark::before, .corner-mark::after {
    content: "";
    position: absolute;
    background: var(--text-3);
    opacity: 0.4;
  }
  .corner-mark.tl { top: 32px; left: 32px; }
  .corner-mark.tl::before { width: 32px; height: 1px; top: 0; left: 0; }
  .corner-mark.tl::after { width: 1px; height: 32px; top: 0; left: 0; }
  .corner-mark.br { bottom: 32px; right: 32px; transform: rotate(180deg); }
  .corner-mark.br::before { width: 32px; height: 1px; top: 0; left: 0; }
  .corner-mark.br::after { width: 1px; height: 32px; top: 0; left: 0; }

  /* chrome blob decorations */
  .chrome-blob {
    position: absolute;
    border-radius: 40% 60% 70% 30% / 40% 50% 60% 50%;
    background: var(--chrome);
    box-shadow: inset -20px -20px 40px rgba(0,0,0,0.08), inset 20px 20px 40px rgba(255,255,255,0.8);
    filter: blur(0.5px);
    pointer-events: none;
    z-index: 0;
  }
  .chrome-blob.b1 {
    width: 360px; height: 280px;
    top: 10%; right: -80px;
    opacity: 0.75;
    animation: morph 12s ease-in-out infinite alternate;
  }
  .chrome-blob.b2 {
    width: 200px; height: 200px;
    bottom: 12%; left: -40px;
    opacity: 0.55;
    animation: morph2 14s ease-in-out infinite alternate;
  }
  @keyframes morph {
    0% { border-radius: 40% 60% 70% 30% / 40% 50% 60% 50%; transform: rotate(0deg); }
    100% { border-radius: 60% 40% 30% 70% / 50% 60% 40% 50%; transform: rotate(15deg); }
  }
  @keyframes morph2 {
    0% { border-radius: 30% 70% 50% 50% / 50% 30% 70% 50%; transform: rotate(0deg); }
    100% { border-radius: 70% 30% 40% 60% / 30% 60% 40% 70%; transform: rotate(-10deg); }
  }

  /* yellow accent shapes */
  .yellow-bar {
    position: absolute;
    background: var(--lime);
    pointer-events: none;
    z-index: 0;
  }
  .yellow-bar.y1 {
    width: 120px; height: 14px;
    top: 28%; left: 8%;
    transform: rotate(-12deg);
    opacity: 0.9;
  }
  .yellow-bar.y2 {
    width: 80px; height: 8px;
    bottom: 24%; right: 12%;
    transform: rotate(8deg);
    opacity: 0.7;
  }

  .wrap {
    max-width: 1000px;
    margin: 0 auto;
    width: 100%;
    position: relative;
    z-index: 1;
  }

  /* header */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 40px;
  }
  .logo {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    font-weight: 500;
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
    font-weight: 700;
  }
  .status-dot {
    width: 10px; height: 10px;
    border-radius: 50%;
    transition: background 0.3s;
  }
  .status-dot.on { background: var(--green); box-shadow: 0 0 0 4px rgba(10,155,125,0.15); }
  .status-dot.off { background: var(--red); }

  /* hero typography */
  .hero {
    position: relative;
    margin-bottom: 48px;
  }
  .hero-label {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    letter-spacing: 3px;
    text-transform: uppercase;
    color: var(--text-3);
    margin-bottom: 16px;
  }
  .hero-title {
    font-size: 92px;
    font-weight: 900;
    line-height: 0.9;
    letter-spacing: -5px;
    text-transform: uppercase;
    margin-bottom: 20px;
    position: relative;
    display: inline-block;
  }
  .hero-title .lime-fill {
    position: relative;
    color: var(--black);
  }
  .hero-title .lime-fill::before {
    content: "";
    position: absolute;
    left: -4px; right: -4px; top: 28%; bottom: 18%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-4deg);
  }
  .hero-sub {
    font-size: 24px;
    font-weight: 600;
    letter-spacing: -0.5px;
    color: var(--text-2);
    margin-left: 6px;
  }

  .status-row {
    display: flex;
    align-items: center;
    gap: 16px;
    margin-top: 36px;
    flex-wrap: wrap;
  }
  .status-pill {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 22px;
    border-radius: 100px;
    font-size: 14px;
    font-weight: 600;
    border: 1px solid var(--gray);
    background: var(--white);
    transition: all 0.3s;
  }
  .status-pill.on {
    border-color: var(--green);
    color: var(--green);
  }
  .status-pill.off {
    border-color: var(--red);
    color: var(--red);
  }
  .status-pill .dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: currentColor;
  }
  .status-pill.on .dot {
    animation: pulse 1.5s ease-in-out infinite;
  }
  .meta {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    color: var(--text-3);
    text-transform: uppercase;
    letter-spacing: 1px;
  }

  /* metrics */
  .metrics {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 14px;
    margin-bottom: 40px;
  }
  .metric {
    background: var(--white);
    border: 1px solid var(--gray);
    padding: 24px 20px;
    position: relative;
  }
  .metric::after {
    content: "";
    position: absolute;
    top: 0; left: 0;
    width: 100%; height: 4px;
    background: var(--lime);
    transform: scaleX(0);
    transform-origin: left;
    transition: transform 0.4s ease;
  }
  .metric:hover::after { transform: scaleX(1); }
  .metric .num {
    font-family: "JetBrains Mono", monospace;
    font-size: 48px;
    font-weight: 500;
    line-height: 1;
    letter-spacing: -3px;
    margin-bottom: 6px;
  }
  .metric .lbl {
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 1.5px;
    color: var(--text-2);
  }

  .scroll-hint {
    display: flex;
    align-items: center;
    gap: 10px;
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-3);
  }
  .scroll-hint .arrow {
    animation: bob 2s ease-in-out infinite;
  }
  @keyframes bob { 0%,100%{transform:translateY(0)} 50%{transform:translateY(5px)} }

  /* module 2 events */
  .module.events-mod {
    background: var(--white);
  }
  .events-mod .grid-tech { opacity: 0.18; }

  .events-head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-bottom: 32px;
  }
  .events-head h2 {
    font-size: 52px;
    font-weight: 900;
    letter-spacing: -2px;
    text-transform: uppercase;
    line-height: 1;
  }
  .events-head h2 .lime-fill {
    position: relative;
  }
  .events-head h2 .lime-fill::before {
    content: "";
    position: absolute;
    left: -6px; right: -6px; top: 40%; bottom: 10%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-3deg);
  }
  .events-head .refresh {
    font-family: "JetBrains Mono", monospace;
    font-size: 13px;
    color: var(--text-3);
  }
  .events-head .refresh span {
    color: var(--text);
    font-weight: 700;
  }

  .feed {
    border-top: 2px solid var(--black);
    max-height: 52vh;
    overflow-y: auto;
  }
  .feed .row {
    display: grid;
    grid-template-columns: 130px 1fr 140px;
    gap: 16px;
    align-items: center;
    padding: 14px 4px;
    border-bottom: 1px solid var(--gray-light);
    transition: background 0.15s;
    cursor: pointer;
  }
  .feed .row:hover { background: rgba(223,255,0,0.05); }
  .feed .row .tag {
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 1px;
    padding: 4px 0;
    border-bottom: 2px solid;
    display: inline-block;
    width: fit-content;
  }
  .feed .row .tag.face { color: var(--black); border-color: var(--lime); }
  .feed .row .tag.voice { color: var(--black); border-color: #a78bfa; }
  .feed .row .tag.heart { color: var(--black); border-color: var(--green); }
  .feed .row .detail {
    font-size: 14px;
    color: var(--text-2);
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
  .feed .row .time {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    color: var(--text-3);
    text-align: right;
  }
  .empty-feed {
    padding: 60px 0;
    text-align: center;
    color: var(--text-3);
    font-size: 14px;
  }

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
    top: -8px; left: 8px; right: -8px; bottom: 8px;
    background: var(--lime);
    z-index: -1;
  }
  .modal-close {
    position: absolute;
    top: 10px;
    right: 14px;
    background: none;
    border: none;
    font-size: 22px;
    cursor: pointer;
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
  .modal-grid .k { color: var(--text-3); font-weight: 600; text-transform: uppercase; font-size: 12px; letter-spacing: 0.5px; }
  .modal-grid .v { color: var(--text); word-break: break-all; }

  @media (max-width: 700px) {
    .hero-title { font-size: 52px; letter-spacing: -3px; }
    .hero-sub { font-size: 18px; }
    .metrics { grid-template-columns: 1fr; }
    .metric .num { font-size: 36px; }
    .events-head h2 { font-size: 34px; }
    .feed .row { grid-template-columns: 90px 1fr; gap: 6px 12px; }
    .feed .row .time { grid-column: 1 / -1; text-align: left; }
  }
</style>
</head>
<body>
<div class="snap">

<!-- Module 1 -->
<div class="module">
  <div class="grid-tech"></div>
  <div class="corner-mark tl"></div>
  <div class="corner-mark br"></div>
  <div class="chrome-blob b1"></div>
  <div class="chrome-blob b2"></div>
  <div class="yellow-bar y1"></div>
  <div class="yellow-bar y2"></div>

  <div class="wrap">
    <div class="header">
      <div class="logo"><span>ESP</span>Neural Monitor</div>
      <div class="status-dot" id="hdot"></div>
    </div>

    <div class="hero">
      <div class="hero-label">Device Status</div>
      <h1 class="hero-title"><span class="lime-fill">ESP32-P4</span></h1>
      <div class="hero-sub">NEURAL MONITOR / 实时监控</div>
      <div class="status-row">
        <div class="status-pill" id="hero-status">
          <span class="dot"></span>
          <span id="hero-status-text">离线</span>
        </div>
        <div class="meta" id="last-seen">LAST HEARTBEAT --</div>
      </div>
    </div>

    <div class="metrics">
      <div class="metric">
        <div class="num" id="cnt-face">0</div>
        <div class="lbl">Face Recog.</div>
      </div>
      <div class="metric">
        <div class="num" id="cnt-voice">0</div>
        <div class="lbl">Voice Cmd.</div>
      </div>
      <div class="metric">
        <div class="num" id="cnt-heart">0</div>
        <div class="lbl">Heartbeat</div>
      </div>
    </div>

    <div class="scroll-hint">
      Scroll for events <span class="arrow">&#x25BC;</span>
    </div>
  </div>
</div>

<!-- Module 2 -->
<div class="module events-mod">
  <div class="grid-tech"></div>
  <div class="chrome-blob b2" style="width:300px;height:240px;opacity:0.35;bottom:auto;top:8%;right:-60px;left:auto"></div>
  <div class="yellow-bar y1" style="top:auto;bottom:18%;left:10%;width:90px;height:10px;transform:rotate(6deg)"></div>

  <div class="wrap">
    <div class="events-head">
      <h2>Event <span class="lime-fill">Stream</span></h2>
      <div class="refresh"><span id="countdown">3</span>s refresh</div>
    </div>
    <div class="feed" id="feed">
      <div class="empty-feed">暂无事件，等待设备上报</div>
    </div>
    <div class="scroll-hint" style="margin-top:20px">
      Back to top <span class="arrow">&#x25B2;</span>
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
var tCls={face_recognized:"face",face_recognize:"face",voice_command:"voice",heartbeat:"heart"};
var kL={id:"用户ID",user:"用户",action:"动作",cmd:"指令",command:"命令",uptime:"运行时长",wifi_rssi:"WiFi信号",rssi:"信号强度",time:"时间",name:"姓名",confidence:"置信度"};

async function fStatus(){
  try{
    var r=await fetch(API_STATUS),d=await r.json();
    var on=d.online;
    document.getElementById("hdot").className="status-dot "+(on?"on":"off");
    var hs=document.getElementById("hero-status");
    var ht=document.getElementById("hero-status-text");
    hs.className="status-pill "+(on?"on":"off");
    ht.textContent=on?"设备在线":"设备离线";
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
    var tc=tCls[e.event_type]||"face";
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
