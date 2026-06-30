import pathlib
f = pathlib.Path(r"D:\WORKS\Human_Face_Recognition_site\templates\index.html")
c = f.read_text(encoding="utf-8")

# Wrap modules in snap container
c = c.replace("<body>", '<body><div class="snap">', 1)

# Close snap container before modal
c = c.replace("<!-- Modal -->", "</div>\n<!-- Modal -->", 1)

# Add snap styles - insert after html{scroll-behavior:smooth}
c = c.replace(
  "html{scroll-behavior:smooth}",
  "html{scroll-behavior:smooth}.snap{height:100vh;overflow-y:scroll;scroll-snap-type:y mandatory;scroll-behavior:smooth}.module{scroll-snap-align:start;scroll-snap-stop:always;min-height:100vh}",
  1
)

f.write_text(c, encoding="utf-8")
print(len(c), "bytes")
