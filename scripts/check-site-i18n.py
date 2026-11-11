#!/usr/bin/env python3
"""Check the website translations (website/i18n/) against the English strings of the built site.

    scripts/check-site-i18n.py                    check every language; lists the missing keys per language; exit 1 on an error
    scripts/check-site-i18n.py --todo ID FILE     write what language ID still lacks: {"ui": {key: English}, "updates": {...}, "insp": {...}}
    scripts/check-site-i18n.py --merge ID FILE    merge a translated --todo file into the language's files and drop keys the site no longer uses

The site speaks the same 15 languages as the app (src/ui/I18n.cpp: ids and native names must match). English lives in
the pages; website/_src/build.py collects it into website/i18n/en.json {"ui", "featured", "insp"} and
website/i18n/updates/en.json (the Updates page, keys "u.<version>..."). Every other language has the same two files.
Errors: a stale build, a missing file or key (no key may fall back to English), an empty text, {0}-style placeholders
or HTML tags that differ from English, a page without the language's hreflang link. Unused keys are warnings.
The wiki bodies are English only for now: the summary line says how many words that is.
"""
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "website"
DIR = SITE / "i18n"
BUILD = SITE / "_src/build.py"
APP = ROOT / "src/ui/I18n.cpp"
SECTS = ("ui", "featured", "insp")


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def english():
    en = load(DIR / "en.json")
    en["updates"] = load(DIR / "updates/en.json")
    return en


def translation(lid):
    tr = load(DIR / f"{lid}.json") if (DIR / f"{lid}.json").exists() else {}
    up = DIR / f"updates/{lid}.json"
    tr["updates"] = load(up) if up.exists() else {}
    return tr


def page_langs():
    m = re.search(r"LANGS = (\[\[.*?\]\])\n", BUILD.read_text(encoding="utf-8"), re.S)
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


def missing(en, tr):
    return {sect: {k: v for k, v in en[sect].items() if not str(tr.get(sect, {}).get(k, "")).strip()}
            for sect in SECTS + ("updates",)}


def dump(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=0) + "\n", encoding="utf-8")


def todo(lid, out):
    todo = {k: v for k, v in missing(english(), translation(lid)).items() if v}
    dump(Path(out), todo)
    print(f"check-site-i18n: {lid}: {sum(len(v) for v in todo.values())} strings to translate -> {out}")
    return 0


def merge(lid, src):
    en, tr, new = english(), translation(lid), load(Path(src))
    for sect in SECTS + ("updates",):
        have = tr.setdefault(sect, {})
        have.update({k: v for k, v in new.get(sect, {}).items() if k in en[sect]})
        tr[sect] = {k: have[k] for k in en[sect] if k in have}          # English order, unused keys dropped
    dump(DIR / f"updates/{lid}.json", tr.pop("updates"))
    dump(DIR / f"{lid}.json", {s: tr[s] for s in SECTS})
    print(f"check-site-i18n: {lid}: merged {src}; now run website/_src/build.py")
    return 0


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--todo":
        return todo(sys.argv[2], sys.argv[3])
    if len(sys.argv) == 4 and sys.argv[1] == "--merge":
        return merge(sys.argv[2], sys.argv[3])
    errors, warnings = [], []
    if subprocess.run([sys.executable, str(BUILD), "--check"]).returncode:
        errors.append("the built pages are out of date: run website/_src/build.py")
    en = english()
    langs = page_langs()
    if APP.exists() and langs != app_langs():
        errors.append(f"site languages {langs} differ from the app's {app_langs()}")
    pages = sorted(SITE.glob("**/index.html"))
    for lid, _ in langs:
        if lid == "en":
            continue
        for p in pages:
            if f'hreflang="{lid}"' not in p.read_text(encoding="utf-8"):
                errors.append(f"{lid}: no hreflang link in {p.relative_to(SITE)}")
    known = {lid for lid, _ in langs}
    for f in list(DIR.glob("*.json")) + list(DIR.glob("updates/*.json")):
        if f.stem not in known:
            errors.append(f"{f.relative_to(DIR)}: not a site language")
    for lid, _ in langs:
        if lid == "en":
            continue
        tr = translation(lid)
        for sect, keys in missing(en, tr).items():
            if keys:
                errors.append(f"{lid}: {len(keys)} missing in {sect}: " + ", ".join(list(keys)[:12]) + (" ..." if len(keys) > 12 else ""))
        for sect in SECTS + ("updates",):
            have = tr.get(sect, {})
            for k, v in have.items():
                if k not in en[sect]:
                    warnings.append(f"{lid}: unused {sect} '{k}'")
                elif isinstance(v, str) and v.strip() and shape(v) != shape(en[sect][k]):
                    errors.append(f"{lid}: {sect} '{k}' placeholders/tags differ from English")
    for w in warnings:
        print("warning:", w)
    for e in errors:
        print("error:", e)
    words = sum(len(re.sub(r"^---.*?---", "", f.read_text(encoding="utf-8"), flags=re.S).split()) for f in (SITE / "_src/wiki").glob("*.md"))
    print(f"check-site-i18n: {len(langs)} languages, {len(en['ui'])} strings + {len(en['updates'])} update strings, "
          f"{len(errors)} errors, {len(warnings)} warnings; wiki bodies in English only: {words} words")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
