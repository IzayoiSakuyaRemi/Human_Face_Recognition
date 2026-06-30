import pathlib
import re

f = pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html")
html = f.read_text(encoding="utf-8")

# Step 1: Replace the CSS for grid-bg
old_grid_css = """  .grid-bg{
    position:fixed;inset:0;z-index:0;pointer-events:none;
    background-image:linear-gradient(rgba(0,200,255,0.03) 1px,transparent 1px),linear-gradient(90deg,rgba(0,200,255,0.03) 1px,transparent 1px);
    background-size:60px 60px
  }
  .grid-bg::before{
    content:"";position:absolute;inset:0;
    background:radial-gradient(ellipse 80% 50% at 50% 0%,rgba(0,200,255,0.08),transparent 70%);
    animation:gp 8s ease-in-out infinite alternate
  }
  @keyframes gp{0%{opacity:0.6}100%{opacity:1}}
  .scanlines{"""

new_grid_css = """  .ambient-orbs{position:fixed;inset:0;z-index:0;pointer-events:none;overflow:hidden}
  .ambient-orbs .orb{position:absolute;border-radius:50%;filter:blur(80px);opacity:0;animation:orbF 20s ease-in-out infinite}
  .ambient-orbs .orb:nth-child(1){width:600px;height:600px;background:radial-gradient(circle,rgba(0,229,255,0.12),transparent 70%);top:-15%;left:-10%;animation-delay:0s}
  .ambient-orbs .orb:nth-child(2){width:500px;height:500px;background:radial-gradient(circle,rgba(213,0,249,0.08),transparent 70%);bottom:-20%;right:-8%;animation-delay:-7s}
  .ambient-orbs .orb:nth-child(3){width:400px;height:400px;background:radial-gradient(circle,rgba(0,230,118,0.07),transparent 70%);top:50%;left:50%;animation-delay:-14s}
  @keyframes orbF{0%,100%{opacity:0.3;transform:translate(0,0) scale(1)}25%{opacity:0.6;transform:translate(40px,-30px) scale(1.1)}50%{opacity:0.2;transform:translate(-20px,40px) scale(0.9)}75%{opacity:0.5;transform:translate(30px,20px) scale(1.05)}}
  .grid-bg{position:fixed;inset:0;z-index:0;pointer-events:none;overflow:hidden}
  .grid-bg .gh{position:absolute;inset:0;background-image:linear-gradient(rgba(0,200,255,0.025) 1px,transparent 1px);background-size:100% 60px;animation:gsv 30s linear infinite}
  .grid-bg .gv{position:absolute;inset:0;background-image:linear-gradient(90deg,rgba(0,200,255,0.025) 1px,transparent 1px);background-size:60px 100%;animation:gsh 40s linear infinite}
  .grid-bg .gbh{position:absolute;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,rgba(0,229,255,0.08),transparent);animation:blv 8s linear infinite;top:-1px}
  .grid-bg .gbv{position:absolute;top:0;bottom:0;width:1px;background:linear-gradient(180deg,transparent,rgba(0,229,255,0.08),transparent);animation:blh 12s linear infinite;left:-1px}
  .grid-bg .rg{position:absolute;inset:0;background:radial-gradient(ellipse 80% 50% at 50% 0%,rgba(0,200,255,0.08),transparent 70%);animation:gp 8s ease-in-out infinite alternate}
  @keyframes gp{0%{opacity:0.6}100%{opacity:1}}
  @keyframes gsv{0%{transform:translateY(0)}100%{transform:translateY(60px)}}
  @keyframes gsh{0%{transform:translateX(0)}100%{transform:translateX(60px)}}
  @keyframes blv{0%{transform:translateY(-100vh)}100%{transform:translateY(100vh)}}
  @keyframes blh{0%{transform:translateX(-100vw)}100%{transform:translateX(100vw)}}
  .scanlines{"""

html = html.replace(old_grid_css, new_grid_css)

# Step 2: Replace particles CSS section
old_particles_css_1 = """  .particles{position:fixed;inset:0;z-index:0;pointer-events:none;overflow:hidden}
  .particle{
    position:absolute;width:3px;height:3px;background:var(--neon-cyan);
    border-radius:50%;box-shadow:0 0 6px var(--neon-cyan),0 0 12px rgba(0,229,255,0.3);
    opacity:0;animation:pf 12s infinite ease-in-out
  }
  .particle:nth-child(1){left:10%;top:20%;animation-delay:0s;width:2px;height:2px}
  .particle:nth-child(2){left:25%;top:60%;animation-delay:2s;width:4px;height:4px;background:var(--neon-purple);box-shadow:0 0 8px var(--neon-purple)}
  .particle:nth-child(3){left:45%;top:10%;animation-delay:4s;width:2px;height:2px}
  .particle:nth-child(4){left:65%;top:70%;animation-delay:1s;width:3px;height:3px;background:var(--neon-green);box-shadow:0 0 8px var(--neon-green)}
  .particle:nth-child(5){left:80%;top:30%;animation-delay:3s;width:2px;height:2px}
  .particle:nth-child(6){left:50%;top:85%;animation-delay:5s;width:4px;height:4px;background:var(--neon-purple);box-shadow:0 0 8px var(--neon-purple)}
  .particle:nth-child(7){left:15%;top:45%;animation-delay:6s;width:2px;height:2px}
  .particle:nth-child(8){left:90%;top:55%;animation-delay:2.5s;width:3px;height:3px}
  @keyframes pf{
    0%,100%{opacity:0;transform:translateY(0) scale(0.5)}
    10%{opacity:1}
    30%{opacity:0.8;transform:translateY(-30px) scale(1)}
    60%{opacity:0.3;transform:translateY(-60px) scale(0.6)}
    100%{opacity:0;transform:translateY(-100px) scale(0.2)}
  }
  .container{"""

new_particles_css = """  .particles{position:fixed;inset:0;z-index:0;pointer-events:none;overflow:hidden}
  .particle{position:absolute;border-radius:50%;opacity:0;animation:pf15 15s infinite ease-in-out}
  .particle:nth-child(1){left:5%;top:15%;width:3px;height:3px;background:var(--neon-cyan);animation-delay:0s;box-shadow:0 0 8px var(--neon-cyan)}
  .particle:nth-child(2){left:12%;top:55%;width:2px;height:2px;background:var(--neon-cyan);animation-delay:1.5s;box-shadow:0 0 4px var(--neon-cyan);animation-duration:18s}
  .particle:nth-child(3){left:20%;top:80%;width:4px;height:4px;background:var(--neon-cyan);animation-delay:3s;box-shadow:0 0 10px var(--neon-cyan)}
  .particle:nth-child(4){left:30%;top:35%;width:2px;height:2px;background:var(--neon-cyan);animation-delay:4.5s;box-shadow:0 0 4px var(--neon-cyan);animation-duration:20s}
  .particle:nth-child(5){left:40%;top:70%;width:4px;height:4px;background:var(--neon-purple);animation-delay:2s;box-shadow:0 0 12px var(--neon-purple)}
  .particle:nth-child(6){left:48%;top:20%;width:2px;height:2px;background:var(--neon-purple);animation-delay:5s;box-shadow:0 0 6px var(--neon-purple);animation-duration:17s}
  .particle:nth-child(7){left:55%;top:88%;width:3px;height:3px;background:var(--neon-purple);animation-delay:7s;box-shadow:0 0 8px var(--neon-purple)}
  .particle:nth-child(8){left:65%;top:25%;width:3px;height:3px;background:var(--neon-green);animation-delay:1s;box-shadow:0 0 8px var(--neon-green)}
  .particle:nth-child(9){left:72%;top:62%;width:2px;height:2px;background:var(--neon-green);animation-delay:3.5s;box-shadow:0 0 4px var(--neon-green);animation-duration:22s}
  .particle:nth-child(10){left:80%;top:10%;width:4px;height:4px;background:var(--neon-green);animation-delay:6s;box-shadow:0 0 10px var(--neon-green)}
  .particle:nth-child(11){left:88%;top:48%;width:2px;height:2px;background:var(--neon-cyan);animation-delay:2.5s;box-shadow:0 0 4px var(--neon-cyan);animation-duration:19s}
  .particle:nth-child(12){left:95%;top:75%;width:3px;height:3px;background:var(--neon-cyan);animation-delay:5.5s;box-shadow:0 0 8px var(--neon-cyan)}
  .particle:nth-child(13){left:18%;top:42%;width:1px;height:1px;background:#fff;animation-delay:4s;box-shadow:none;animation-duration:25s}
  .particle:nth-child(14){left:52%;top:50%;width:1px;height:1px;background:#fff;animation-delay:6.5s;box-shadow:none;animation-duration:23s}
  .particle:nth-child(15){left:78%;top:90%;width:1.5px;height:1.5px;background:#fff;animation-delay:8s;box-shadow:none;animation-duration:21s}
  @keyframes pf15{0%,100%{opacity:0;transform:translateY(0) translateX(0) scale(0.3)}8%{opacity:1;transform:translateY(-20px) translateX(5px) scale(1)}25%{opacity:0.7;transform:translateY(-60px) translateX(-10px) scale(0.9)}50%{opacity:0.4;transform:translateY(-120px) translateX(8px) scale(0.5)}75%{opacity:0.1;transform:translateY(-180px) translateX(-5px) scale(0.3)}100%{opacity:0;transform:translateY(-250px) translateX(3px) scale(0.1)}}
  .container{"""

html = html.replace(old_particles_css_1, new_particles_css)

# Step 3: Replace the HTML grid-bg div with enhanced version
html = html.replace(
  '<div class="grid-bg"></div>',
  '<div class="grid-bg"><div class="gh"></div><div class="gv"></div><div class="gbh"></div><div class="gbv"></div><div class="rg"></div></div>'
)

# Step 4: Add ambient-orbs before grid-bg
html = html.replace(
  '<div class="grid-bg">',
  '<div class="ambient-orbs"><div class="orb"></div><div class="orb"></div><div class="orb"></div></div>\n<div class="grid-bg">',
  1
)

# Step 5: Add more particles
html = html.replace(
  '<div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>\n</div>',
  '<div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div>\n</div>'
)

# Also fix the particle div count in the first line
html = html.replace(
  '<div class="particles">\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>',
  '<div class="particles">\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>\n  <div class="particle"></div><div class="particle"></div><div class="particle"></div><div class="particle"></div>'
)

f.write_text(html, encoding="utf-8")
print("Final size:", len(html), "bytes")
