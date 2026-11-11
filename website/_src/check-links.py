#!/usr/bin/env python3
"""Check every internal link, image, script and #anchor of the built site (website/**/index.html).

    python3 website/_src/check-links.py     exit 1 if something points nowhere
"""
import re
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent
pages = {("/" + str(p.parent.relative_to(OUT)).replace(".", "") + "/").replace("//", "/"): p.read_text(encoding="utf-8")
         for p in sorted(OUT.glob("**/index.html")) if "_src" not in p.parts}
ids = {u: set(re.findall(r'\bid="([^"]+)"', t)) for u, t in pages.items()}
bad, n = [], 0
for url, text in pages.items():
    for ref in set(re.findall(r'(?:href|src|poster)="([^"]+)"', text)):
        if re.match(r"(https?:|mailto:|data:)", ref):
            continue
        n += 1
        path, _, frag = ref.partition("#")
        path = path.split("?")[0] or url
        if not path.startswith("/"):
            bad.append(f"{url}: relative link {ref}")
        elif path in pages:
            if frag and frag not in ids[path]:
                bad.append(f"{url}: no #{frag} in {path}")
        elif not (OUT / path.lstrip("/")).is_file():
            bad.append(f"{url}: {ref} not found")
for b in bad:
    print("error:", b)
print(f"check-links: {len(pages)} pages, {n} internal links, {len(bad)} broken")
sys.exit(1 if bad else 0)
