import pathlib
f = pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html")
c = f.read_text(encoding="utf-8")

# 1. Enhance module 1 background with subtle gradient glow
old_module1 = """<!-- Module 1: Dashboard -->
<div class="module">"""
new_module1 = """<!-- Module 1: Dashboard -->
<div class="module" style="position:relative;background:linear-gradient(180deg,#f0f1f4 0%,#e8ecf2 100%)">
  <div style="position:absolute;inset:0;pointer-events:none;overflow:hidden">
    <div style="position:absolute;width:700px;height:700px;border-radius:50%;background:radial-gradient(circle,rgba(30,58,95,0.04),transparent 70%);top:-200px;left:50%;transform:translateX(-50%)"></div>
    <div style="position:absolute;width:400px;height:400px;border-radius:50%;background:radial-gradient(circle,rgba(184,134,11,0.03),transparent 70%);bottom:-100px;right:-100px"></div>
    <!-- subtle dot pattern -->
    <div style="position:absolute;inset:0;opacity:0.015;background-image:radial-gradient(circle,#1e3a5f 1px,transparent 1px);background-size:24px 24px"></div>
  </div>"""
c = c.replace(old_module1, new_module1, 1)

# 2. Make module 1 inner content position relative for z-index
c = c.replace('class="module-inner">', 'class="module-inner" style="position:relative;z-index:1">', 1)

# 3. Enhance hero area - add a subtle card
old_hero = """    <div class="md-hero">
      <div class="label">设备状态</div>
      <h1><span class="accent">ESP32-P4</span><br>Neural Monitor</h1>"""
new_hero = """    <div class="md-hero">
      <div class="label" style="color:var(--accent);font-weight:700;letter-spacing:4px">实时监控</div>
      <h1><span class="accent">ESP32-P4</span><br>Neural Monitor</h1>"""
c = c.replace(old_hero, new_hero, 1)

# 4. Enhance metric cards with subtle gradient backgrounds
old_metric_css = """  .md-metric {
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
  }"""
new_metric_css = """  .md-metric {
    background:var(--surface);
    border-radius:16px;
    padding:28px 20px 24px;
    text-align:center;
    border:1px solid var(--border);
    box-shadow:0 1px 3px rgba(0,0,0,0.03);
    transition:all .25s ease;
    position:relative;
    overflow:hidden;
  }
  .md-metric::before {
    content:"";
    position:absolute;top:0;left:0;right:0;height:3px;
    opacity:0.5;transition:opacity .25s;
  }
  .md-metric:nth-child(1)::before{background:linear-gradient(90deg,var(--accent),transparent)}
  .md-metric:nth-child(2)::before{background:linear-gradient(90deg,var(--purple),transparent)}
  .md-metric:nth-child(3)::before{background:linear-gradient(90deg,var(--green),transparent)}
  .md-metric:hover {
    box-shadow:0 8px 28px rgba(0,0,0,0.07);
    transform:translateY(-3px);
  }
  .md-metric:hover::before{opacity:1}"""
c = c.replace(old_metric_css, new_metric_css, 1)

# 5. Enhance module 2 background
old_module2 = """<!-- Module 2: Events -->
<div class="module" style="background:var(--surface)">"""
new_module2 = """<!-- Module 2: Events -->
<div class="module" style="position:relative;background:linear-gradient(180deg,#ffffff 0%,#f7f8fb 100%)">
  <div style="position:absolute;inset:0;pointer-events:none;overflow:hidden">
    <div style="position:absolute;width:500px;height:500px;border-radius:50%;background:radial-gradient(circle,rgba(30,58,95,0.03),transparent 70%);top:-150px;right:-80px"></div>
    <div style="position:absolute;width:300px;height:300px;border-radius:50%;background:radial-gradient(circle,rgba(124,58,237,0.03),transparent 70%);bottom:-80px;left:-80px"></div>
  </div>"""
c = c.replace(old_module2, new_module2, 1)

# 6. Make module 2 inner content also z-index
c = c.replace('<div class="module-inner">\n    <div class="ev-title">', '<div class="module-inner" style="position:relative;z-index:1">\n    <div class="ev-title">', 1)

# 7. Add subtle top accent line to feed
c = c.replace('  .feed {\n    background:var(--surface);\n    border-radius:16px;\n    border:1px solid var(--border);\n    box-shadow:0 1px 3px rgba(0,0,0,0.03);\n    overflow-y:auto;\n    max-height:55vh;\n  }',
  '  .feed {\n    background:var(--surface);\n    border-radius:16px;\n    border:1px solid var(--border);\n    box-shadow:0 1px 3px rgba(0,0,0,0.03);\n    overflow-y:auto;\n    max-height:55vh;\n    position:relative;\n  }\n  .feed::before{content:"";position:absolute;top:0;left:40px;right:0;height:1px;background:linear-gradient(90deg,transparent,var(--border),transparent)}',
  1)

f.write_text(c, encoding="utf-8")
print(len(c), "bytes written")
