#!/usr/bin/env python3
"""Check the website translations (website/i18n/) against the English strings of the built site.

    scripts/check-site-i18n.py                    check every language; lists the missing keys per language; exit 1 on an error
    scripts/check-site-i18n.py --todo ID FILE     write what language ID still lacks: {"ui": {key: English}, "updates": {...}, "catalog": {...}, "insp": {...}}
    scripts/check-site-i18n.py --merge ID FILE    merge a translated --todo file into the language's files and drop keys the site no longer uses
    scripts/check-site-i18n.py --bodies ID DIR    take translated wiki bodies from DIR: <page>.md (the article's body, no front matter) and
                                                  *.json ({plugin id: {"intro", "does": [one per control], "tips"}}); see "Wiki bodies" below

The site speaks the same 15 languages as the app (src/ui/I18n.cpp: ids and native names must match). English lives in
the pages; website/_src/build.py collects it into website/i18n/en.json {"ui", "featured", "insp"} and
website/i18n/updates/en.json (the Updates page, keys "u.<version>...") and website/i18n/catalog/en.json (the plugin cards, keys "c.<plugin>").
Every other language has the same three files.
Errors: a stale build, a missing file or key (no key may fall back to English), an empty text, {0}-style placeholders
or HTML tags that differ from English, a page without the language's hreflang link. Unused keys are warnings.

Wiki bodies (the articles of website/_src/wiki and the plugin pages made from website/_src/catalog.json): a language has
all of them or none. They live in website/_src/wiki/<id>/<page>.md and website/_src/catalog-wiki/<id>.json, each with
the digest of the English text it was made from ("source"). Errors: a body that is missing or made from an older English
text, one whose blocks, links, pictures or headings differ from the English page, a paragraph left in English.
Program labels in `code` (menu paths, buttons, options) and the control names of the plugin tables are never translated
by hand: --bodies writes them as the app shows them in that language (resources/i18n/<id>.txt), and English where the
app has no translation. A label that no longer matches the app's translation is a warning.
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
PARTS = ("updates", "catalog")          # strings kept in their own files: website/i18n/<part>/<id>.json


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def english():
    en = load(DIR / "en.json")
    for part in PARTS:
        en[part] = load(DIR / part / "en.json")
    return en


def translation(lid):
    tr = load(DIR / f"{lid}.json") if (DIR / f"{lid}.json").exists() else {}
    for part in PARTS:
        f = DIR / part / f"{lid}.json"
        tr[part] = load(f) if f.exists() else {}
    return tr


def site_build():
    """website/_src/build.py as a module: its markdown and the digests of the English bodies."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("lynx_site_build", BUILD)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def app_dict(lid):
    """The app's translation: "English" = "Translation" lines of resources/i18n/<id>.txt."""
    out, f = {}, ROOT / "resources/i18n" / f"{lid}.txt"
    un = lambda x: re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), x)
    for m in re.finditer(r'^"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"', f.read_text(encoding="utf-8") if f.exists() else "", re.M):
        out[un(m.group(1))] = un(m.group(2))
    return out


HEADS = 0           # app_dict key of the index label() builds: head of a label -> the labels that start with it
KEYNAME = re.compile(r"(Ctrl|Shift|Alt|Cmd|Win|Enter|Return|Space|Tab|Esc|Del|Delete|Backspace|Home|End|Up|Down|Left|Right|PgUp|PgDn|Page Up|Page Down|"
                     r"Ins|Insert|F\d{1,2}|Num \S+|[A-Za-z0-9]|[^\w\s])(\s*\+.*)?$")


def label(text, app):
    """A program label as the app shows it in a language: whole, or each part of a `Menu › Item...` path. Keys, file names and
    anything the app does not translate stay as they are."""
    def one(x):
        if x in app:
            return app[x]
        for suf in ("...", ":"):
            if x.endswith(suf) and x[:-len(suf)] in app:
                return app[x[:-len(suf)]] + suf
            if x + suf in app:
                return re.sub(r"\s*(\.\.\.|…|:|：)$", "", app[x + suf])
        # the page names the head of a longer label ("Capture MIDI" of "Capture MIDI: the notes just played..."): the same head
        for sep, seps in ((" (", (" (", "（", "(")), (": ", (": ", "：", ":")), (". ", (". ", "。"))):
            longer = sorted((k for k in heads.get(x, ()) if k.startswith(x + sep)), key=len)
            for k in longer:
                cut = [app[k].find(t) for t in seps if app[k].find(t) > 0]
                if cut:
                    return app[k][:min(cut)].strip()
        return x
    heads = app.setdefault(HEADS, {}) if app else {}
    if app and not heads:
        for k in list(app):
            if isinstance(k, str):
                m = re.match(r"(.{3,}?)( \(|: |\. )", k)
                if m:
                    heads.setdefault(m.group(1), []).append(k)
                    for mm in re.finditer(r" \(|: |\. ", k):
                        heads.setdefault(k[:mm.start()], []).append(k)
    if KEYNAME.fullmatch(text) or text.startswith((".", "-", "/", "~", "<")) or "\\" in text:
        return text
    return " › ".join(one(x) for x in text.split(" › ")) if " › " in text else one(text)


BLOCK = re.compile(r"(</?(?:p|ul|ol|li|table|thead|tbody|tr|td|th|h2|h3|pre|blockquote|figure|div)\b[^>]*>)")


def body_shape(page, app=None):
    """What a translated body keeps: the blocks in order (headings with their ids) and, inside each, the links, pictures
    and emphasis; and the labels in `code`, as the app shows them when app is given."""
    cells, blocks, codes = [], [], []
    for n, part in enumerate(BLOCK.split(page)):
        if n % 2:
            blocks.append(re.sub(r'\s(?:class|data-i18n|alt|width|height|loading)="[^"]*"', "", part))
            continue
        codes.append(Counter(label(html_text(c), app) if app is not None else html_text(c) for c in re.findall(r"<code>(.*?)</code>", part, re.S)))
        part = re.sub(r"<code>.*?</code>", "<code>", part, flags=re.S)
        cells.append(Counter((t, h) for t, h in re.findall(r'<(/?\w+)(?:[^>]*?(?:href|src)="([^"]*)")?[^>]*>', part)))
    return blocks, cells, codes


def html_text(s):
    return s.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"').replace("&amp;", "&")


def code_labels(text, app):
    """Markdown with every `code` span written as the app shows it."""
    out = []
    for n, part in enumerate(re.split(r"(```.*?```)", text, flags=re.S)):
        out.append(part if n % 2 else re.sub(r"`([^`\n]+)`", lambda m: "`%s`" % label(m.group(1), app), part))
    return "".join(out)


def plain(page):
    """The text blocks of a body, without tags and labels (to spot a paragraph left in English)."""
    out = []
    for part in BLOCK.split(page)[::2]:
        t = re.sub(r"<[^>]+>", "", re.sub(r"<code>.*?</code>", "", part, flags=re.S)).strip()
        out.append(t)
    return out


def english_prose(text):
    """A block of English sentences (a list of product names is the same in every language)."""
    words = text.lower().split()
    return len(words) >= 6 and sum(w.strip(".,:;()") in ("the", "and", "of", "to", "is", "a", "in", "with", "for", "that", "it", "on", "are", "or") for w in words) >= 3


def body_sources(b):
    """The English bodies: {page: (digest, html)} for the articles, {plugin id: (digest, html, entry)} for the plugin pages."""
    index, pages = b.wiki_pages()
    cat = b.catalog()
    arts = {s: (b.digest(p["body"]), p["prose"]) for s, p in pages.items()}
    plugs = {p["id"]: (b.digest(b.plugin_source(p["wiki"])), b.plugin_prose(p, cat, p["wiki"])[0], p) for p in cat if p.get("wiki")}
    return arts, plugs, cat


def check_bodies(b, langs, errors, warnings):
    """Returns {language: words of English it has bodies for}."""
    arts, plugs, cat = body_sources(b)
    done = {}
    for lid, _ in langs:
        if lid == "en":
            continue
        app = app_dict(lid)
        d = SITE / "_src/wiki" / lid
        f = SITE / "_src/catalog-wiki" / f"{lid}.json"
        mine = load(f) if f.exists() else {}
        have = {p.stem for p in d.glob("*.md")} if d.is_dir() else set()
        if not have and not mine:
            continue                                   # a language without wiki bodies: the pages say so
        for extra in sorted(have - set(arts)) + sorted(set(mine) - set(plugs)):
            warnings.append(f"{lid}: wiki body '{extra}' has no English page")
        lack = sorted(set(arts) - have) + sorted(set(plugs) - set(mine))
        if lack:
            errors.append(f"{lid}: {len(lack)} wiki bodies missing: " + ", ".join(lack[:12]) + (" ..." if len(lack) > 12 else ""))
        pairs = []
        for s in sorted(have & set(arts)):
            meta, body = b.front(d / f"{s}.md")
            if meta.get("source") != arts[s][0]:
                errors.append(f"{lid}: wiki body '{s}' was made from an older English text")
                continue
            ids = re.findall(r'<h[23] id="([^"]+)"', arts[s][1])
            pairs.append((s, arts[s][1], b.markdown(body, iter(ids))))
        for pid in sorted(set(mine) & set(plugs)):
            w = mine[pid]
            if w.get("source") != plugs[pid][0]:
                errors.append(f"{lid}: plugin page '{pid}' was made from an older English text")
                continue
            en = plugs[pid][2]["wiki"]
            if [label(a, app) for a, _ in en.get("controls", [])] != [a for a, _ in w.get("controls", [])]:
                warnings.append(f"{lid}: plugin page '{pid}': control names differ from the app's")
            pairs.append((pid, plugs[pid][1], b.plugin_prose(plugs[pid][2], cat, w)[0]))
        words = 0
        for name, en, tr in pairs:
            eb, ec, ecode = body_shape(en, app)
            tb, tc, tcode = body_shape(tr)
            if eb != tb:
                errors.append(f"{lid}: wiki body '{name}': blocks or headings differ from English")
            elif ec != tc:
                n = next(i for i, (x, y) in enumerate(zip(ec, tc)) if x != y)
                errors.append(f"{lid}: wiki body '{name}': links, pictures or emphasis differ from English near \"{plain(en)[n][:50]}\"")
            elif ecode != tcode:
                n = next(i for i, (x, y) in enumerate(zip(ecode, tcode)) if x != y)
                warnings.append(f"{lid}: wiki body '{name}': labels differ from the app's: {sorted((tcode[n] - ecode[n]).keys())[:3]}")
            same = [x for x, y in zip(plain(en), plain(tr)) if x == y and english_prose(x)]
            if same and eb == tb:
                errors.append(f"{lid}: wiki body '{name}': left in English: \"{same[0][:60]}\"")
            words += len(re.sub(r"<[^>]+>", " ", en).split())
        done[lid] = words
    return done


def bodies(lid, src):
    """Take translated bodies from a folder: labels as the app shows them, the digest of the English source, into website/_src."""
    b = site_build()
    arts, plugs, _ = body_sources(b)
    app = app_dict(lid)
    n = 0
    for f in sorted(Path(src).glob("*.md")):
        if f.stem not in arts:
            print(f"check-site-i18n: {f.name}: no such wiki page, skipped")
            continue
        text = f.read_text(encoding="utf-8")
        text = re.sub(r"\A---\n.*?\n---\n", "", text, flags=re.S).strip("\n")
        out = SITE / "_src/wiki" / lid / f.name
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("---\nsource: %s\n---\n%s\n" % (arts[f.stem][0], code_labels(text, app)), encoding="utf-8")
        n += 1
    f = SITE / "_src/catalog-wiki" / f"{lid}.json"
    mine = load(f) if f.exists() else {}
    for g in sorted(Path(src).glob("*.json")):
        for pid, t in load(g).items():
            if pid not in plugs:
                print(f"check-site-i18n: {g.name}: no plugin page '{pid}', skipped")
                continue
            en = plugs[pid][2]["wiki"]
            does = t.get("does", [])
            if len(does) != len(en.get("controls", [])) or len(t.get("tips", [])) != len(en.get("tips", [])):
                print(f"check-site-i18n: {g.name}: '{pid}' has another number of controls or tips than English, skipped")
                continue
            mine[pid] = {"source": plugs[pid][0], "intro": code_labels(t["intro"], app),
                         "controls": [[label(a, app), code_labels(d, app)] for (a, _), d in zip(en.get("controls", []), does)],
                         "tips": [code_labels(x, app) for x in t.get("tips", [])]}
            n += 1
    if mine:
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(json.dumps({k: mine[k] for k in plugs if k in mine}, ensure_ascii=False, indent=0) + "\n", encoding="utf-8")
    print(f"check-site-i18n: {lid}: {n} wiki bodies taken from {src}; now run website/_src/build.py")
    return 0


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
            for sect in SECTS + PARTS}


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
    for sect in SECTS + PARTS:
        have = tr.setdefault(sect, {})
        have.update({k: v for k, v in new.get(sect, {}).items() if k in en[sect]})
        tr[sect] = {k: have[k] for k in en[sect] if k in have}          # English order, unused keys dropped
    for part in PARTS:
        dump(DIR / part / f"{lid}.json", tr.pop(part))
    dump(DIR / f"{lid}.json", {s: tr[s] for s in SECTS})
    print(f"check-site-i18n: {lid}: merged {src}; now run website/_src/build.py")
    return 0


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--todo":
        return todo(sys.argv[2], sys.argv[3])
    if len(sys.argv) == 4 and sys.argv[1] == "--merge":
        return merge(sys.argv[2], sys.argv[3])
    if len(sys.argv) == 4 and sys.argv[1] == "--bodies":
        return bodies(sys.argv[2], sys.argv[3])
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
    for f in list(DIR.glob("*.json")) + [f for part in PARTS for f in DIR.glob(part + "/*.json")]:
        if f.stem not in known:
            errors.append(f"{f.relative_to(DIR)}: not a site language")
    for lid, _ in langs:
        if lid == "en":
            continue
        tr = translation(lid)
        for sect, keys in missing(en, tr).items():
            if keys:
                errors.append(f"{lid}: {len(keys)} missing in {sect}: " + ", ".join(list(keys)[:12]) + (" ..." if len(keys) > 12 else ""))
        for sect in SECTS + PARTS:
            have = tr.get(sect, {})
            for k, v in have.items():
                if k not in en[sect]:
                    warnings.append(f"{lid}: unused {sect} '{k}'")
                elif isinstance(v, str) and v.strip() and shape(v) != shape(en[sect][k]):
                    errors.append(f"{lid}: {sect} '{k}' placeholders/tags differ from English")
    done = check_bodies(site_build(), langs, errors, warnings)
    for w in warnings:
        print("warning:", w)
    for e in errors:
        print("error:", e)
    words = sum(len(re.sub(r"^---.*?---", "", f.read_text(encoding="utf-8"), flags=re.S).split()) for f in (SITE / "_src/wiki").glob("*.md"))
    pwords = 0                                    # the generated plugin pages: intro, controls, tips
    for p in load(SITE / "_src/catalog.json"):
        w = p.get("wiki") or {}
        pwords += len(" ".join([w.get("intro", "")] + [x for r in w.get("controls", []) for x in r] + w.get("tips", [])).split())
    none = [lid for lid, _ in langs if lid != "en" and lid not in done]
    print(f"check-site-i18n: {len(langs)} languages, {len(en['ui'])} strings + {len(en['updates'])} update strings + "
          f"{len(en['catalog'])} catalogue strings, {len(errors)} errors, {len(warnings)} warnings; "
          f"wiki bodies ({words} words in articles + {pwords} in plugin pages): translated in {len(done)} languages"
          + (", English only in " + " ".join(none) if none else ""))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
