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
    background: var(--black);
    color: var(--text);
    -webkit-font-smoothing: antialiased;
    overflow: hidden;
  }

  /* snap container */
  .snap {
    height: 100vh;
    overflow-y: scroll;
    scroll-snap-type: y mandatory;
    scroll-behavior: smooth;
  }

  .section {
    min-height: 100vh;
    scroll-snap-align: start;
    scroll-snap-stop: always;
    position: relative;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 40px 24px;
    overflow: hidden;
  }

  /* reveal animations */
  .reveal {
    opacity: 0;
    transform: translateY(40px);
    transition: all 0.8s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .section.active .reveal {
    opacity: 1;
    transform: translateY(0);
  }
  .reveal.d1 { transition-delay: 0.1s; }
  .reveal.d2 { transition-delay: 0.2s; }
  .reveal.d3 { transition-delay: 0.3s; }
  .reveal.d4 { transition-delay: 0.4s; }
  .reveal.d5 { transition-delay: 0.5s; }

  /* side nav dots */
  .side-nav {
    position: fixed;
    right: 28px;
    top: 50%;
    transform: translateY(-50%);
    z-index: 50;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .side-nav a {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: var(--gray);
    transition: all 0.3s;
    cursor: pointer;
    position: relative;
  }
  .side-nav a.active {
    background: var(--lime);
    transform: scale(1.4);
  }
  .side-nav a::before {
    content: attr(data-label);
    position: absolute;
    right: 18px;
    top: 50%;
    transform: translateY(-50%);
    font-size: 10px;
    letter-spacing: 1px;
    text-transform: uppercase;
    color: var(--text-3);
    white-space: nowrap;
    opacity: 0;
    transition: opacity 0.2s;
    font-family: "JetBrains Mono", monospace;
  }
  .side-nav a:hover::before { opacity: 1; }

  /* grid tech overlay */
  .grid-tech {
    position: absolute;
    inset: 0;
    pointer-events: none;
    background-image:
      linear-gradient(var(--gray-light) 1px, transparent 1px),
      linear-gradient(90deg, var(--gray-light) 1px, transparent 1px);
    background-size: 70px 70px;
    opacity: 0.25;
  }

  /* chrome blobs */
  .blob {
    position: absolute;
    background: var(--chrome);
    box-shadow: inset -20px -20px 40px rgba(0,0,0,0.08), inset 20px 20px 40px rgba(255,255,255,0.8);
    pointer-events: none;
    filter: blur(0.5px);
  }
  .blob.b1 { width: 360px; height: 280px; top: 8%; right: -80px; opacity: 0.65; border-radius: 40% 60% 70% 30% / 40% 50% 60% 50%; animation: mo1 14s ease-in-out infinite alternate; }
  .blob.b2 { width: 200px; height: 200px; bottom: 10%; left: -40px; opacity: 0.5; border-radius: 30% 70% 50% 50% / 50% 30% 70% 50%; animation: mo2 16s ease-in-out infinite alternate; }
  .blob.b3 { width: 280px; height: 220px; top: 50%; right: 10%; opacity: 0.35; border-radius: 50% 50% 60% 40% / 60% 40% 50% 50%; animation: mo3 18s ease-in-out infinite alternate; }
  @keyframes mo1 { 0%{transform:rotate(0)} 100%{transform:rotate(18deg)} }
  @keyframes mo2 { 0%{transform:rotate(0)} 100%{transform:rotate(-15deg)} }
  @keyframes mo3 { 0%{transform:rotate(0)} 100%{transform:rotate(12deg)} }

  /* yellow accent bars */
  .bar-y {
    position: absolute;
    background: var(--lime);
    pointer-events: none;
  }
  .bar-y.y1 { width: 120px; height: 14px; top: 22%; left: 6%; transform: rotate(-10deg); opacity: 0.85; }
  .bar-y.y2 { width: 80px; height: 8px; top: 48%; right: 8%; transform: rotate(7deg); opacity: 0.7; }
  .bar-y.y3 { width: 100px; height: 10px; bottom: 20%; left: 12%; transform: rotate(-5deg); opacity: 0.6; }

  /* section 1 - hero */
  .s1 { background: var(--bg); }
  .s1-wrap { max-width: 900px; width: 100%; }
  .s1 .top-line {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 60px;
  }
  .s1 .logo {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--text-2);
  }
  .s1 .logo span {
    display: inline-block;
    background: var(--lime);
    color: var(--black);
    padding: 4px 8px;
    margin-right: 8px;
    font-weight: 800;
  }
  .s1 .status-dot {
    width: 12px; height: 12px;
    border-radius: 50%;
    transition: background 0.3s;
  }
  .s1 .status-dot.on { background: var(--green); box-shadow: 0 0 0 5px rgba(10,155,125,0.15); }
  .s1 .status-dot.off { background: var(--red); }

  .s1 h1 {
    font-size: 120px;
    font-weight: 900;
    letter-spacing: -8px;
    line-height: 0.85;
    text-transform: uppercase;
    margin-bottom: 28px;
  }
  .s1 h1 .lime-back {
    position: relative;
    display: inline-block;
  }
  .s1 h1 .lime-back::before {
    content: "";
    position: absolute;
    left: -10px; right: -10px; top: 22%; bottom: 10%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-5deg) rotate(-1deg);
  }
  .s1 .sub-line {
    display: flex;
    align-items: center;
    gap: 24px;
    flex-wrap: wrap;
  }
  .s1 .chip {
    font-family: "JetBrains Mono", monospace;
    font-size: 13px;
    padding: 10px 22px;
    border: 2px solid var(--black);
    background: var(--white);
    text-transform: uppercase;
    letter-spacing: 1px;
    font-weight: 700;
  }
  .s1 .meta {
    font-size: 15px;
    color: var(--text-3);
  }

  .scroll-hint {
    position: absolute;
    bottom: 32px;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
    font-family: "JetBrains Mono", monospace;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-3);
  }
  .scroll-hint .arrow { animation: bob 2s ease-in-out infinite; font-size: 14px; }
  @keyframes bob { 0%,100%{transform:translateY(0)} 50%{transform:translateY(6px)} }

  /* section 2 - metrics */
  .s2 { background: var(--white); }
  .s2-wrap { max-width: 1000px; width: 100%; }
  .s2-label {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    letter-spacing: 3px;
    text-transform: uppercase;
    color: var(--text-3);
    margin-bottom: 16px;
  }
  .s2 h2 {
    font-size: 56px;
    font-weight: 900;
    letter-spacing: -2px;
    text-transform: uppercase;
    margin-bottom: 48px;
  }
  .s2 h2 .lime-back {
    position: relative;
    display: inline-block;
  }
  .s2 h2 .lime-back::before {
    content: "";
    position: absolute;
    left: -6px; right: -6px; top: 30%; bottom: 10%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-4deg);
  }

  .metrics-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 20px;
  }
  .m-card {
    background: var(--bg);
    border: 2px solid var(--black);
    padding: 32px 24px;
    position: relative;
    transition: transform 0.2s, box-shadow 0.2s;
  }
  .m-card:hover {
    transform: translate(-4px, -4px);
    box-shadow: 8px 8px 0 var(--black);
  }
  .m-card .m-tag {
    font-family: "JetBrains Mono", monospace;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: var(--text-3);
    margin-bottom: 16px;
  }
  .m-card .m-num {
    font-size: 64px;
    font-weight: 900;
    letter-spacing: -4px;
    line-height: 1;
    margin-bottom: 8px;
  }
  .m-card .m-label {
    font-size: 14px;
    font-weight: 700;
    color: var(--text-2);
    text-transform: uppercase;
    letter-spacing: 1px;
  }
  .m-card:nth-child(1) .m-num { color: var(--black); }
  .m-card:nth-child(2) .m-num { color: #7c3aed; }
  .m-card:nth-child(3) .m-num { color: var(--green); }

  /* section 3 - events */
  .s3 { background: var(--bg); }
  .s3-wrap { max-width: 900px; width: 100%; }
  .s3-head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-bottom: 32px;
  }
  .s3-head h2 {
    font-size: 56px;
    font-weight: 900;
    letter-spacing: -2px;
    text-transform: uppercase;
  }
  .s3-head h2 .lime-back {
    position: relative;
    display: inline-block;
  }
  .s3-head h2 .lime-back::before {
    content: "";
    position: absolute;
    left: -6px; right: -6px; top: 30%; bottom: 10%;
    background: var(--lime);
    z-index: -1;
    transform: skewX(-4deg);
  }
  .s3-head .refresh {
    font-family: "JetBrains Mono", monospace;
    font-size: 12px;
    color: var(--text-3);
  }
  .s3-head .refresh span { color: var(--text); font-weight: 700; }

  .feed {
    background: var(--white);
    border: 2px solid var(--black);
    max-height: 50vh;
    overflow-y: auto;
  }
  .feed .row {
    display: grid;
    grid-template-columns: 120px 1fr 130px;
    gap: 16px;
    align-items: center;
    padding: 16px 24px;
    border-bottom: 1px solid var(--gray-light);
    transition: background 0.12s;
    cursor: pointer;
  }
  .feed .row:last-child { border-bottom: none; }
  .feed .row:hover { background: rgba(223,255,0,0.08); }
  .feed .row .tag {
    font-family: "JetBrains Mono", monospace;
    font-size: 11px;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 1px;
    padding: 4px 0;
    border-bottom: 2px solid;
    width: fit-content;
  }
  .feed .row .tag.f { border-color: var(--lime); }
  .feed .row .tag.v { border-color: #7c3aed; }
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
    font-size: 12px;
    color: var(--text-3);
    text-align: right;
  }
  .empty-feed {
    padding: 60px 24px;
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
    background: rgba(0,0,0,0.35);
    backdrop-filter: blur(8px);
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

  @media (max-width: 700px) {
    .s1 h1 { font-size: 64px; letter-spacing: -4px; }
    .s2 h2, .s3-head h2 { font-size: 38px; }
    .metrics-grid { grid-template-columns: 1fr; }
    .m-card .m-num { font-size: 48px; }
    .feed .row { grid-template-columns: 90px 1fr; gap: 6px 12px; padding: 12px 16px; }
    .feed .row .time { grid-column: 1 / -1; text-align: left; }
    .side-nav { right: 14px; gap: 10px; }
    .side-nav a { width: 8px; height: 8px; }
  }
</style>
</head>
<body>

<!-- side navigation -->
<div class="side-nav">
  <a href="#s1" data-label="Home" class="active"></a>
  <a href="#s2" data-label="Metrics"></a>
  <a href="#s3" data-label="Events"></a>
</div>

<div class="snap">

<!-- Section 1: Hero -->
<div class="section s1 active" id="s1">
  <div class="grid-tech"></div>
  <div class="blob b1"></div>
  <div class="blob b2"></div>
  <div class="bar-y y1"></div>
  <div class="bar-y y2"></div>
  <div class="bar-y y3"></div>
  <div class="s1-wrap">
    <div class="top-line reveal d1">
      <div class="logo"><span>ESP</span>Neural Monitor</div>
      <div class="status-dot" id="hdot"></div>
    </div>
    <h1 class="reveal d2">ESP32<span class="lime-back">-P4</span></h1>
    <div class="sub-line reveal d3">
      <div class="chip" id="hero-status-text">离线</div>
      <div class="meta" id="last-seen">LAST HEARTBEAT --</div>
    </div>
  </div>
  <div class="scroll-hint reveal d4">
    滑动切换 <span class="arrow">&#x25BC;</span>
  </div>
</div>

<!-- Section 2: Metrics -->
<div class="section s2" id="s2">
  <div class="grid-tech"></div>
  <div class="blob b3"></div>
  <div class="bar-y y2" style="top:18%"></div>
  <div class="bar-y y3" style="bottom:15%;left:auto;right:12%;transform:rotate(8deg)"></div>
  <div class="s2-wrap">
    <div class="s2-label reveal d1">Real-time Data</div>
    <h2 class="reveal d2">System <span class="lime-back">Metrics</span></h2>
    <div class="metrics-grid">
      <div class="m-card reveal d3">
        <div class="m-tag">01 / Recognition</div>
        <div class="m-num" id="cnt-face">0</div>
        <div class="m-label">人脸识别</div>
      </div>
      <div class="m-card reveal d4">
        <div class="m-tag">02 / Command</div>
        <div class="m-num" id="cnt-voice">0</div>
        <div class="m-label">语音命令</div>
      </div>
      <div class="m-card reveal d5">
        <div class="m-tag">03 / Heartbeat</div>
        <div class="m-num" id="cnt-heart">0</div>
        <div class="m-label">心跳</div>
      </div>
    </div>
  </div>
  <div class="scroll-hint reveal d5">
    Continue <span class="arrow">&#x25BC;</span>
  </div>
</div>

<!-- Section 3: Events -->
<div class="section s3" id="s3">
  <div class="grid-tech"></div>
  <div class="blob b1" style="top:auto;bottom:5%;right:-60px;opacity:0.5"></div>
  <div class="blob b2" style="bottom:auto;top:10%;left:-30px;opacity:0.4"></div>
  <div class="bar-y y1" style="top:20%"></div>
  <div class="s3-wrap">
    <div class="s3-head reveal d1">
      <h2>Event <span class="lime-back">Stream</span></h2>
      <div class="refresh"><span id="countdown">3</span>s refresh</div>
    </div>
    <div class="feed reveal d2" id="feed">
      <div class="empty-feed">暂无事件，等待设备上报</div>
    </div>
  </div>
  <div class="scroll-hint reveal d3">
    Back to top <span class="arrow">&#x25B2;</span>
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
    document.getElementById("hdot").className="status-dot "+(on?"on":"off");
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

// scroll spy with intersection observer
var sections=document.querySelectorAll(".section");
var navDots=document.querySelectorAll(".side-nav a");
var observer=new IntersectionObserver(function(entries){
  entries.forEach(function(entry){
    if(entry.isIntersecting){
      entry.target.classList.add("active");
      var id=entry.target.id;
      navDots.forEach(function(dot){
        dot.classList.toggle("active", dot.getAttribute("href")==="#"+id);
      });
    }else{
      entry.target.classList.remove("active");
    }
  });
},{threshold:0.5});
sections.forEach(function(s){observer.observe(s);});

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
