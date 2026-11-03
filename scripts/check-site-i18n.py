#!/usr/bin/env python3
"""Check the website translations (website/i18n/<id>.json) against the English text in website/index.html.

    scripts/check-site-i18n.py            check every language; exit 1 on an error
    scripts/check-site-i18n.py --en FILE  write the English source strings (same layout as a language file)

The site speaks the same 15 languages as the app (src/ui/I18n.cpp: ids and native names must match). English lives in
the page; every other language is one JSON file {"ui": {...}, "featured": {...}, "insp": {...}}. Errors: a missing
file or "ui" key, an empty text, {0}-style placeholders or HTML tags that differ from English, a language without its
hreflang link. Missing "featured"/"insp" entries (a plugin added after the translation) are warnings: they show in English.
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "website/index.html"
DIR = ROOT / "website/i18n"
APP = ROOT / "src/ui/I18n.cpp"


def js_unescape(s):
    return re.sub(r"\\(.)", lambda m: m.group(1), s)


def english(page):
    ui = {}
    m = re.search(r"const EN = \{(.*?)\n\};", page, re.S)
    if not m:
        sys.exit("check-site-i18n: EN block not found")
    for k, v in re.findall(r"'([^']+)'\s*:\s*'((?:[^'\\]|\\.)*)'", m.group(1)):
        ui[k] = js_unescape(v)
    ui["meta.title"] = re.search(r"<title>(.*?)</title>", page).group(1)
    ui["meta.desc"] = re.search(r'id="mDesc" content="(.*?)"', page).group(1)
    ui["meta.ogDesc"] = re.search(r'id="mOgD" content="(.*?)"', page).group(1)
    for tag, key, inner in re.findall(r'<(\w+)\b[^>]*?data-i18n="([^"]+)"[^>]*>(.*?)</\1>', page, re.S):
        ui[key] = inner
    for v, k in re.findall(r'alt="([^"]*)" data-i18n-alt="([^"]+)"', page):
        ui[k] = v
    for v, k in re.findall(r'aria-label="([^"]*)"[^>]*data-i18n-aria="([^"]+)"', page):
        ui[k] = v
    ins = json.loads(re.search(r"const INSPIRED = (\{.*?\});\n", page).group(1))
    ui["cl.tm"] = ins["tm"]["en"]
    feat = re.search(r"const FEATURED = \[(.*?)\n\];", page, re.S).group(1)
    featured = {js_unescape(n): js_unescape(e) for n, e in
                re.findall(r"\{n:'((?:[^'\\]|\\.)*)'.*?en:'((?:[^'\\]|\\.)*)'\}", feat, re.S)}
    insp = {g["vendor"] + "/" + i["short"]: i["desc"]["en"] for g in ins["groups"] for i in g["items"] if i["desc"]["en"]}
    return {"ui": ui, "featured": featured, "insp": insp}


def page_langs(page):
    m = re.search(r"LYNX_LANGS=(\[\[.*?\]\]);", page)
    return [tuple(x) for x in json.loads(m.group(1))]


def app_langs():
    src = APP.read_text(encoding="utf-8")
    block = re.search(r"static const std::vector<Language> list \{(.*?)\};", src, re.S).group(1)
    out = []
    for lid, name in re.findall(r'\{ "([^"]+)", ((?:"[^"]*"\s*)+)\}', block):
        raw = "".join(re.findall(r'"([^"]*)"', name))
        out.append((lid, raw.encode("latin-1", "backslashreplace").decode("unicode_escape").encode("latin-1").decode("utf-8")))
    return out


def shape(s):
    """What a translation must keep: placeholders, tags (with their href), entities such as &lt;user&gt;."""
    tags = re.findall(r"<(/?\w+)(?:[^>]*?href=\"([^\"]*)\")?[^>]*>", s)
    return Counter(re.findall(r"\{\d+\}", s)), Counter(tags), Counter(re.findall(r"&lt;|&gt;", s)), s.count("|")


def main():
    page = PAGE.read_text(encoding="utf-8")
    en = english(page)
    if len(sys.argv) == 3 and sys.argv[1] == "--en":
        Path(sys.argv[2]).write_text(json.dumps(en, ensure_ascii=False, indent=0), encoding="utf-8")
        return 0
    errors, warnings = [], []
    langs = page_langs(page)
    if APP.exists() and langs != app_langs():
        errors.append(f"page languages {langs} differ from the app's {app_langs()}")
    for lid, _ in langs:
        if lid != "en" and f'hreflang="{lid}"' not in page:
            errors.append(f"{lid}: no hreflang link")
    known = {lid for lid, _ in langs}
    for f in DIR.glob("*.json"):
        if f.stem not in known:
            errors.append(f"{f.name}: not a site language")
    for lid, _ in langs:
        if lid == "en":
            continue
        f = DIR / f"{lid}.json"
        try:
            tr = json.loads(f.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            errors.append(f"{lid}: {e}")
            continue
        for sect in ("ui", "featured", "insp"):
            have = tr.get(sect, {})
            for k, src in en[sect].items():
                if k not in have:
                    (errors if sect == "ui" else warnings).append(f"{lid}: missing {sect} '{k}'")
                    continue
                v = have[k]
                if not isinstance(v, str) or not v.strip():
                    errors.append(f"{lid}: empty {sect} '{k}'")
                elif shape(v) != shape(src):
                    errors.append(f"{lid}: {sect} '{k}' placeholders/tags differ from English")
            for k in have:
                if k not in en[sect]:
                    warnings.append(f"{lid}: unused {sect} '{k}'")
    for w in warnings:
        print("warning:", w)
    for e in errors:
        print("error:", e)
    n = len(langs)
    print(f"check-site-i18n: {n} languages, {len(en['ui'])} strings, {len(errors)} errors, {len(warnings)} warnings")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
