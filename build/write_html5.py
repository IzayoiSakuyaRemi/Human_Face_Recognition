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
    --green: #0d9488;
    --green-light: rgba(13,148,136,0.07);
    --purple: #7c3aed;
    --purple-light: rgba(124,58,237,0.06);
    --red: #dc2626;
  }
  *{margin:0;padding:0;box-sizing:border-box}
  html{scroll-behavior:smooth}
  body {
    font-family: "Inter",-apple-system,sans-serif;
    background: var(--bg);
    color: var(--text);
    -webkit-font-smoothing: antialiased;
  }
  /* each module fills the viewport */
  .module {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    justify-content: center;
    padding: 48px 24px;
  }
  .module-inner {
    max-width: 860px;
    margin: 0 auto;
    width: 100%;
  }

  /* scroll hint */
  .scroll-hint {
    text-align: center;
    padding: 16px 0 24px;
    color: var(--text-muted);
    font-size: 12px;
    letter-spacing: 2px;
    text-transform: uppercase;
  }
  .scroll-hint .arrow {
    display: block;
    font-size: 18px;
    margin-top: 6px;
    animation: bob 2s ease-in-out infinite;
  }
  @keyframes bob {
    0%,100%{transform:translateY(0)}
    50%{transform:translateY(6px)}
  }

  /* ── Module 1: Dashboard ── */
  .md-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 48px;
  }
  .md-brand {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .md-brand-icon {
    width: 34px;height:34px;
    border-radius: 10px;
    background: var(--accent);
    display: flex;align-items:center;justify-content:center;
    color:#fff;
  }
  .md-brand-icon svg{width:17px;height:17px}
  .md-brand-name{
    font-size:15px;font-weight:700;letter-spacing:-0.3px;
  }
  .md-brand-name .sub{
    font-weight:400;color:var(--text-secondary);font-size:12px;margin-left:6px
  }
  .md-dot {
    width:8px;height:8px;border-radius:50%;transition:background .3s;
  }
  .md-dot.on{background:var(--green);box-shadow:0 0 8px rgba(13,148,136,.3)}
  .md-dot.off{background:var(--red)}

  /* hero area - main visual */
  .md-hero {
    text-align: center;
    margin-bottom: 40px;
  }
  .md-hero .label {
    font-size:13px;font-weight:600;
    text-transform:uppercase;letter-spacing:3px;
    color:var(--text-muted);margin-bottom:12px;
  }
  .md-hero h1 {
    font-size:64px;font-weight:800;
    letter-spacing:-2px;line-height:1.05;
    margin-bottom:16px;
  }
  .md-hero h1 .accent {
    background:linear-gradient(135deg,var(--accent),#2d5a8e);
    -webkit-background-clip:text;
    -webkit-text-fill-color:transparent;
    background-clip:text;
  }
  .md-hero .status-line {
    display:flex;align-items:center;justify-content:center;gap:12px;
    font-size:16px;font-weight:500;
    color:var(--text-secondary);
  }
  .md-hero .status-line .badge {
    display:flex;align-items:center;gap:7px;
    padding:6px 18px;border-radius:100px;
    font-size:14px;font-weight:600;
    transition:all .3s;
  }
  .md-hero .status-line .badge.on {
    background:var(--green-light);
    color:var(--green);
  }
  .md-hero .status-line .badge.off {
    background:rgba(220,38,38,0.05);
    color:var(--red);
  }
  .md-hero .status-line .badge .bdot {
    width:9px;height:9px;border-radius:50%;background:currentColor;
  }
  .md-hero .status-line .badge.on .bdot {
    animation:bp 1.5s ease-in-out infinite;
  }
  @keyframes bp{0%,100%{opacity:1}50%{opacity:0.5}}

  /* metrics row in dashboard */
  .md-metrics {
    display:grid;
    grid-template-columns:repeat(3,1fr);
    gap:16px;
    margin-bottom:32px;
  }
  .md-metric {
    background:var(--surface);
    border-radius:16px;
    padding:28px 20px 24px;
    text-align:center;
    border:1px solid var(--border);
    box-shadow:0 1px 3px rgba(0,0,0,0.03);
    transition:all .2s;
  }
  .md-metric:hover {
    box-shadow:0 8px 24px rgba(0,0,0,0.06);
    transform:translateY(-2px);
  }
  .md-metric .num {
    font-family:"JetBrains Mono",monospace;
    font-size:48px;font-weight:600;
    letter-spacing:-2px;line-height:1;
    margin-bottom:6px;
  }
  .md-metric:nth-child(1) .num{color:var(--accent)}
  .md-metric:nth-child(2) .num{color:var(--purple)}
  .md-metric:nth-child(3) .num{color:var(--green)}
  .md-metric .lbl {
    font-size:12px;font-weight:600;
    text-transform:uppercase;letter-spacing:1.5px;
    color:var(--text-muted);
  }

  .md-footer {
    display:flex;align-items:center;justify-content:center;gap:24px;
    font-size:14px;color:var(--text-muted);
  }
  .md-footer span{color:var(--text-secondary);font-weight:500}

  /* ── Module 2: Events ── */
  .ev-title {
    display:flex;align-items:center;justify-content:space-between;
    margin-bottom:24px;
  }
  .ev-title h2 {
    font-size:13px;font-weight:600;
    text-transform:uppercase;letter-spacing:2px;
    color:var(--text-secondary);
  }
  .ev-title .refresh {
    font-size:13px;color:var(--text-muted);
    font-family:"JetBrains Mono",monospace;
    font-variant-numeric:tabular-nums;
  }
  .ev-title .refresh span{color:var(--accent);font-weight:600}

  .feed {
    background:var(--surface);
    border-radius:16px;
    border:1px solid var(--border);
    box-shadow:0 1px 3px rgba(0,0,0,0.03);
    overflow-y:auto;
    max-height:55vh;
  }
  /* timeline items */
  .ev-item {
    display:flex;align-items:stretch;
    border-bottom:1px solid var(--border);
    cursor:pointer;transition:background .12s;
  }
  .ev-item:last-child{border-bottom:none}
  .ev-item:hover{background:var(--accent-light)}
  .ev-line {
    width:44px;display:flex;flex-direction:column;
    align-items:center;padding:14px 0;flex-shrink:0;
  }
  .ev-line .dot {
    width:10px;height:10px;border-radius:50%;
    flex-shrink:0;margin-top:5px;border:2px solid transparent;
  }
  .ev-line .dot.face{background:var(--accent);border-color:rgba(30,58,95,0.12)}
  .ev-line .dot.voice{background:var(--purple);border-color:rgba(124,58,237,0.12)}
  .ev-line .dot.heart{background:var(--green);border-color:rgba(13,148,136,0.12)}
  .ev-line .bar{flex:1;width:1px;background:var(--border);margin:5px 0}
  .ev-item:last-child .ev-line .bar{display:none}
  .ev-body {
    flex:1;display:flex;align-items:center;
    justify-content:space-between;
    padding:14px 20px 14px 0;min-width:0;gap:16px;
  }
  .ev-info{min-width:0;flex:1}
  .ev-info .type{font-size:13px;font-weight:600;margin-bottom:2px}
  .ev-info .type.f{color:var(--accent)}
  .ev-info .type.v{color:var(--purple)}
  .ev-info .type.h{color:var(--green)}
  .ev-info .det{
    font-size:13px;color:var(--text-secondary);
    overflow:hidden;white-space:nowrap;text-overflow:ellipsis;
  }
  .ev-time{
    font-family:"JetBrains Mono",monospace;
    font-size:13px;color:var(--text-muted);
    white-space:nowrap;flex-shrink:0;
    font-variant-numeric:tabular-nums;
  }
  .empty-feed{text-align:center;padding:50px 20px}
  .empty-feed .ei{font-size:24px;display:block;margin-bottom:8px;opacity:.25}
  .empty-feed .et{font-size:14px;color:var(--text-muted)}

  /* ── Modal ── */
  .modal-overlay {
    display:none;position:fixed;inset:0;z-index:100;
    background:rgba(0,0,0,0.25);
    backdrop-filter:blur(6px);
    align-items:center;justify-content:center;
  }
  .modal-overlay.active{display:flex}
  .modal {
    background:var(--surface);border-radius:18px;
    padding:28px 32px;max-width:460px;width:90vw;
    box-shadow:0 12px 40px rgba(0,0,0,0.08);
    border:1px solid var(--border);
    animation:mi .18s ease;position:relative;
  }
  @keyframes mi{from{opacity:0;transform:scale(.97) translateY(6px)}to{opacity:1;transform:scale(1) translateY(0)}}
  .modal-close{
    position:absolute;top:16px;right:18px;
    background:none;border:none;color:var(--text-muted);
    font-size:20px;cursor:pointer;padding:4px 8px;
    border-radius:8px;transition:all .15s;line-height:1;
  }
  .modal-close:hover{color:var(--text);background:var(--accent-light)}
  .modal h3{font-size:16px;font-weight:700;margin-bottom:20px;letter-spacing:-.3px}
  .modal-grid{display:grid;grid-template-columns:auto 1fr;gap:10px 24px;font-size:14px}
  .modal-grid .k{color:var(--text-muted);font-weight:500}
  .modal-grid .v{color:var(--text);word-break:break-all}

  @media(max-width:700px){
    .module{padding:32px 16px}
    .md-hero h1{font-size:36px}
    .md-metrics{grid-template-columns:1fr;gap:10px}
    .md-metric .num{font-size:36px}
    .md-metric{padding:20px 16px}
    .ev-body{padding:12px 12px 12px 0}
    .modal{padding:20px}
    .feed{max-height:50vh}
  }
</style>
</head>
<body>

<!-- Module 1: Dashboard -->
<div class="module">
  <div class="module-inner">
    <div class="md-header">
      <div class="md-brand">
        <div class="md-brand-icon">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="8"/><path d="M12 8v4l2.5 2.5"/>
          </svg>
        </div>
        <div class="md-brand-name">
          ESP32-P4<span class="sub">Monitoring</span>
        </div>
      </div>
      <span class="md-dot" id="hdot"></span>
    </div>

    <div class="md-hero">
      <div class="label">设备状态</div>
      <h1><span class="accent">ESP32-P4</span><br>Neural Monitor</h1>
      <div class="status-line">
        <div class="badge" id="hero-status">
          <span class="bdot"></span>
          <span id="hero-status-text">离线</span>
        </div>
        <span>·</span>
        <span id="last-seen">等待心跳</span>
      </div>
    </div>

    <div class="md-metrics">
      <div class="md-metric">
        <div class="num" id="cnt-face">0</div>
        <div class="lbl">人脸识别</div>
      </div>
      <div class="md-metric">
        <div class="num" id="cnt-voice">0</div>
        <div class="lbl">语音命令</div>
      </div>
      <div class="md-metric">
        <div class="num" id="cnt-heart">0</div>
        <div class="lbl">心跳</div>
      </div>
    </div>

    <div class="scroll-hint">
      向下滚动查看事件
      <span class="arrow">&#x25BC;</span>
    </div>
  </div>
</div>

<!-- Module 2: Events -->
<div class="module" style="background:var(--surface)">
  <div class="module-inner">
    <div class="ev-title">
      <h2>事件记录</h2>
      <span class="refresh"><span id="countdown">3</span>s 刷新</span>
    </div>
    <div class="feed" id="feed">
      <div class="empty-feed"><span class="ei">&#x25CB;</span><span class="et">暂无事件，等待设备上报</span></div>
    </div>
    <div class="scroll-hint" style="margin-top:20px">
      回到顶部
      <span class="arrow">&#x25B2;</span>
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
    var hd=document.getElementById("hdot");
    hd.className="md-dot "+(on?"on":"off");
    var hs=document.getElementById("hero-status");
    var ht=document.getElementById("hero-status-text");
    hs.className="badge "+(on?"on":"off");
    ht.textContent=on?"设备在线":"设备离线";
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
    fb.innerHTML='<div class="empty-feed"><span class="ei">&#x25CB;</span><span class="et">暂无事件，等待设备上报</span></div>';
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
    var dc=tC[e.event_type]||"face",tc=tCls[e.event_type]||"f";
    var tmf=fmt(e.created_at);
    var fullTime=e.created_at?new Date(e.created_at.replace(" ","T")+"Z").toLocaleString("zh-CN",{hour12:false}):"-";
    var ds=esc(JSON.stringify(ob.parsed||{}));
    h+='<div class="ev-item" data-detail=\''+ds+'" data-type="'+esc(e.event_type)+'" data-time="'+esc(fullTime)+'">';
    h+='<div class="ev-line"><div class="dot '+dc+'"></div><div class="bar"></div></div>';
    h+='<div class="ev-body"><div class="ev-info">';
    h+='<div class="type '+tc+'">'+(tL[e.event_type]||e.event_type)+"</div>";
    h+='<div class="det" title="'+esc(dt)+'">'+esc(dt)+"</div></div>";
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
