import pathlib
f = pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html")
content = f.read_text(encoding="utf-8")

# Add smooth scroll to html
content = content.replace("html{scroll-behavior:smooth}", "")
content = content.replace("  body {", "  html{scroll-behavior:smooth}\n  body {", 1)

# Replace events-head h2 margin-bottom
content = content.replace(
  '  .events-head h2 {\n    font-size: 15px;\n    font-weight: 700;\n    color: var(--text);\n  }',
  '  .events-head h2 {\n    font-size: 16px;\n    font-weight: 700;\n    color: var(--text);\n  }'
)

# Add a divider and more spacing before events section
old = '  <!-- Events -->\n  <div class="events-head">\n    <h2>'
new = '  <div style="height:1px;background:linear-gradient(90deg,var(--border),transparent 60%);margin:12px 0 44px"></div>\n  <!-- Events -->\n  <div class="events-head">\n    <h2>'
content = content.replace(old, new, 1)

# Add more bottom space
content = content.replace(
  '</div>\n</div>\n\n<!-- Modal -->',
  '  <div style="height:32px"></div>\n</div>\n\n<!-- Modal -->',
  1
)

f.write_text(content, encoding="utf-8")
print(len(content), "bytes written")
