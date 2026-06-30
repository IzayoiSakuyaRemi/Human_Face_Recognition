import pathlib

html = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-P4 监控面板</title>
<style>
  @import url("https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap");

  :root {
    --bg: #f5f6fa;
    --bg-card: #ffffff;
    --border: #e8ecf1;
    --text-primary: #1a1d23;
    --text-secondary: #6b7280;
    --text-muted: #9ca3af;
    --accent: #2563eb;
    --accent-light: rgba(37,99,235,0.06);
    --green: #059669;
    --green-light: rgba(5,150,105,0.08);
    --red: #dc2626;
    --red-light: rgba(220,38,38,0.08);
    --purple: #7c3aed;
    --purple-light: rgba(124,58,237,0.08);
    --cyan: #0891b2;
    --cyan-light: rgba(8,145,178,0.08);
    --shadow: 0 1px 3px rgba(0,0,0,0.04), 0 1px 2px rgba(0,0,0,0.02);
    --shadow-hover: 0 8px 30px rgba(0,0,0,0.06);
    --radius: 12px;
  }
  *{margin:0;padding:0;box-sizing:border-box}
  body {
    font-family: "Inter", -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text-primary);
    min-height: 100vh;
    padding: 0;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .app {
    max-width: 1040px;
    margin: 0 auto;
    padding: 40px 24px 80px;
  }

  /* Header */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 36px;
  }
  .header-left {
    display: flex;
    align-items: center;
    gap: 14px;
  }
  .header-icon {
    width: 40px;
    height: 40px;
    background: var(--accent-light);
    border-radius: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--accent);
  }
  .header-icon svg { width: 20px; height: 20px; }
  .header-text h1 {
    font-size: 18px;
    font-weight: 600;
    letter-spacing: -0.3px;
  }
  .header-text .sub {
    font-size: 13px;
    color: var(--text-secondary);
    margin-top: 2px;
  }
  .status-badge {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 16px 8px 14px;
    border-radius: 100px;
    font-size: 13px;
    font-weight: 500;
    transition: all 0.3s ease;
    border: 1px solid transparent;
  }
  .status-badge.online {
    background: var(--green-light);
    color: var(--green);
    border-color: rgba(5,150,105,0.12);
  }
  .status-badge.offline {
    background: var(--red-light);
    color: var(--red);
    border-color: rgba(220,38,38,0.12);
  }
  .status-badge .dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: currentColor;
    flex-shrink: 0;
  }
  .status-badge.online .dot {
    animation: pulse 2s ease-in-out infinite;
  }
  @keyframes pulse {
    0%,100%{opacity:1}
    50%{opacity:0.4}
  }

  /* Stats */
  .stats {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 16px;
    margin-bottom: 32px;
  }
  .stat-card {
    background: var(--bg-card);
    border-radius: var(--radius);
    padding: 20px 24px;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
    transition: all 0.2s ease;
  }
  .stat-card:hover {
    box-shadow: var(--shadow-hover);
    transform: translateY(-1px);
  }
  .stat-card .stat-label {
    font-size: 13px;
    color: var(--text-secondary);
    font-weight: 500;
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
  }
  .stat-card .stat-label .icon-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
  }
  .stat-card:nth-child(1) .icon-dot { background: var(--accent); }
  .stat-card:nth-child(2) .icon-dot { background: var(--purple); }
  .stat-card:nth-child(3) .icon-dot { background: var(--green); }
  .stat-card .stat-value {
    font-size: 30px;
    font-weight: 600;
    letter-spacing: -0.5px;
    line-height: 1.2;
  }
  .stat-card:nth-child(1) .stat-value { color: var(--accent); }
  .stat-card:nth-child(2) .stat-value { color: var(--purple); }
  .stat-card:nth-child(3) .stat-value { color: var(--green); }

  /* Control */
  .control-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
  }
  .control-bar .section-title {
    font-size: 15px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .control-bar .countdown {
    font-size: 13px;
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
  }
  .control-bar .countdown span {
    font-weight: 600;
    color: var(--text-primary);
  }

  /* Table */
  .table-wrap {
    background: var(--bg-card);
    border-radius: var(--radius);
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
    overflow: hidden;
  }
  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 14px;
  }
  thead th {
    text-align: left;
    padding: 12px 20px;
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--text-muted);
    border-bottom: 1px solid var(--border);
    background: var(--bg);
  }
  tbody tr {
    border-bottom: 1px solid var(--border);
    transition: background 0.15s ease;
  }
  tbody tr:last-child { border-bottom: none; }
  tbody tr:hover { background: rgba(37,99,235,0.02); }
  tbody td {
    padding: 14px 20px;
    vertical-align: middle;
  }

  .tag {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 4px 12px;
    border-radius: 100px;
    font-size: 12px;
    font-weight: 500;
    letter-spacing: 0.2px;
  }
  .tag.face_recognized,.tag.face_recognize {
    background: var(--cyan-light);
    color: var(--cyan);
  }
  .tag.voice_command {
    background: var(--purple-light);
    color: var(--purple);
  }
  .tag.heartbeat {
    background: var(--green-light);
    color: var(--green);
  }

  .detail-cell {
    max-width: 380px;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
    color: var(--text-secondary);
    font-size: 13px;
  }
  .time-cell {
    white-space: nowrap;
    color: var(--text-muted);
    font-size: 13px;
    font-variant-numeric: tabular-nums;
  }

  .empty-state {
    text-align: center;
    padding: 60px 20px;
    color: var(--text-muted);
  }
  .empty-state .empty-icon {
    font-size: 28px;
    margin-bottom: 12px;
    display: block;
  }
  .empty-state .empty-text {
    font-size: 14px;
  }

  /* Modal */
  .modal-overlay {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 100;
    background: rgba(0,0,0,0.3);
    backdrop-filter: blur(4px);
    align-items: center;
    justify-content: center;
  }
  .modal-overlay.active { display: flex; }
  .modal {
    background: var(--bg-card);
    border-radius: 14px;
    padding: 28px 32px;
    max-width: 480px;
    width: 90vw;
    box-shadow: 0 20px 60px rgba(0,0,0,0.12);
    border: 1px solid var(--border);
    animation: modalIn 0.2s ease;
    position: relative;
  }
  @keyframes modalIn {
    from{opacity:0;transform:scale(0.97) translateY(8px)}
    to{opacity:1;transform:scale(1) translateY(0)}
  }
  .modal-close {
    position: absolute;
    top: 14px;
    right: 16px;
    background: none;
    border: none;
    color: var(--text-muted);
    font-size: 22px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 6px;
    line-height: 1;
    transition: color 0.15s;
  }
  .modal-close:hover { color: var(--text-primary); }
  .modal h3 {
    font-size: 16px;
    font-weight: 600;
    margin-bottom: 20px;
    color: var(--text-primary);
  }
  .modal-body {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 10px 20px;
    font-size: 14px;
  }
  .modal-body .k {
    color: var(--text-muted);
    font-weight: 500;
  }
  .modal-body .v {
    color: var(--text-primary);
    word-break: break-all;
  }

  @media (max-width: 700px) {
    .app { padding: 24px 16px 60px; }
    .header { flex-direction: column; align-items: flex-start; gap: 14px; }
    .stats { grid-template-columns: 1fr; gap: 12px; }
    .stat-card .stat-value { font-size: 26px; }
    thead th, tbody td { padding: 10px 14px; }
    .detail-cell { max-width: 160px; }
    .modal { padding: 20px; }
  }
</style>
</head>
<body>
<div class="app">
  <div class="header">
    <div class="header-left">
      <div class="header-icon">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <path d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20z"/>
          <path d="M12 6v6l4 2"/>
        </svg>
      </div>
      <div class="header-text">
        <h1>ESP32-P4 监控</h1>
        <div class="sub">人脸识别 &middot; 语音命令 &middot; 设备状态</div>
      </div>
    </div>
    <div class="status-badge" id="status">
      <span class="dot"></span>
      <span id="status-text">检测中...</span>
    </div>
  </div>

  <div class="stats">
    <div class="stat-card">
      <div class="stat-label"><span class="icon-dot"></span>人脸识别</div>
      <div class="stat-value" id="cnt-face">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-label"><span class="icon-dot"></span>语音命令</div>
      <div class="stat-value" id="cnt-voice">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-label"><span class="icon-dot"></span>心跳</div>
      <div class="stat-value" id="cnt-heart">0</div>
    </div>
  </div>

  <div class="control-bar">
    <span class="section-title">事件记录</span>
    <span class="countdown">刷新 <span id="countdown">3</span>s</span>
  </div>

  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th style="width:130px">事件类型</th>
          <th>详情</th>
          <th style="width:170px">时间</th>
        </tr>
      </thead>
      <tbody id="tbody">
        <tr class="empty-state"><td colspan="3"><span class="empty-icon">&#x221E;</span><span class="empty-text">暂无数据，等待设备上报</span></td></tr>
      </tbody>
    </table>
  </div>
</div>

<div class="modal-overlay" id="modal">
  <div class="modal">
    <button class="modal-close" id="modal-close">&times;</button>
    <h3 id="modal-title">事件详情</h3>
    <div class="modal-body" id="modal-body"></div>
  </div>
</div>

<script>
var API_EVENTS="/api/events?limit=20";
var API_STATUS="/api/status";
var REFRESH=3,countdown=REFRESH,lastId=0;

function fmt(t){
  if(!t)return"-";
  var d=new Date(t.replace(" ","T")+"Z");
  return isNaN(d.getTime())?t:d.toLocaleString("zh-CN",{hour12:false});
}

var tL={face_recognized:"人脸识别",face_recognize:"人脸识别",voice_command:"语音命令",heartbeat:"心跳"};
function tag(t){return'<span class="tag '+t+'">'+(tL[t]||t)+"</span>";}

var kL={id:"用户ID",user:"用户",action:"动作",cmd:"指令",command:"命令",uptime:"运行时长",wifi_rssi:"WiFi信号",rssi:"信号强度",time:"时间",name:"姓名",confidence:"置信度"};

async function fStatus(){
  try{
    var r=await fetch(API_STATUS),d=await r.json();
    var el=document.getElementById("status"),tx=document.getElementById("status-text");
    el.className="status-badge "+(d.online?"online":"offline");
    tx.textContent=d.online?"设备在线":"设备离线";
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
  var tb=document.getElementById("tbody");
  if(!ev.length){
    tb.innerHTML='<tr class="empty-state"><td colspan="3"><span class="empty-icon">&#x221E;</span><span class="empty-text">暂无数据，等待设备上报</span></td></tr>';
    return;
  }
  var l=ev[0],n=l.id>lastId;if(n)lastId=l.id;
  var h="";
  for(var i=0;i<ev.length;i++){
    var e=ev[i],dt="",ob={};
    try{
      var d=JSON.parse(e.detail||"{}");ob.parsed=d;
      var ps=[];for(var k in d)if(d.hasOwnProperty(k))ps.push((kL[k]||k)+"="+d[k]);
      dt=ps.join(", ");
    }catch(_){dt=e.detail||"-";}
    var ds=esc(JSON.stringify(ob.parsed||{})),ts=esc(e.event_type),tms=esc(fmt(e.created_at));
    h+='<tr style="cursor:pointer" data-detail=\''+ds+'" data-type="'+ts+'" data-time="'+tms+'">';
    h+="<td>"+tag(e.event_type)+"</td>";
    h+='<td class="detail-cell" title="'+esc(dt)+'">'+esc(dt)+"</td>";
    h+='<td class="time-cell">'+fmt(e.created_at)+"</td></tr>";
  }
  tb.innerHTML=h;
  var rs=tb.querySelectorAll("tr:not(.empty-state)");
  for(var i=0;i<rs.length;i++){
    rs[i].addEventListener("click",function(ev2){
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
</script>
</body>
</html>
"""

pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html").write_text(html, encoding="utf-8")
print("Written", len(html), "bytes")
