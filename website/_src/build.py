#!/usr/bin/env python3
"""Build lynx-share.com: writes the static pages of website/ from the sources in website/_src/.

    python3 website/_src/build.py           write the pages and the English string files
    python3 website/_src/build.py --check   exit 1 if a written file is out of date (release step)

Sources (all in _src/):  pages/*.html  body of Home, Features, Download      updates.json  one entry per release
                         wiki/*.md + wiki/index.json  the manual             faq.md        the FAQ
                         strings.json  English of strings that have no element of their own
                         plugins.js    plugin lists (its INSPIRED block is written by scripts/gen-inspired.py)
Output: index.html, features/, download/, updates/, wiki/, wiki/<page>/, faq/ (each an index.html),
        i18n/en.json + i18n/updates/en.json (the English of every data-i18n key: what the translations are checked
        against) and i18n/latest/<lang>.json (the newest release's strings, for the box on Home).
Python 3 standard library only. No page needs JavaScript to be read.
"""
import hashlib
import html
import json
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent
OUT = SRC.parent
SITE = "https://lynx-share.com"
REPO = "https://github.com/Ka1-S/LYNX-SHARE"
LANGS = [["en", "English"], ["zh-Hans", "简体中文"], ["zh-Hant", "繁體中文"], ["nl", "Nederlands"], ["fr", "Français"],
         ["de", "Deutsch"], ["id", "Bahasa Indonesia"], ["it", "Italiano"], ["ja", "日本語"], ["ko", "한국어"],
         ["pl", "Polski"], ["pt-BR", "Português (Brasil)"], ["ru", "Русский"], ["es", "Español"], ["tr", "Türkçe"]]
OGL = {"zh-Hans": "zh_CN", "zh-Hant": "zh_TW", "nl": "nl_NL", "fr": "fr_FR", "de": "de_DE", "id": "id_ID", "it": "it_IT",
       "ja": "ja_JP", "ko": "ko_KR", "pl": "pl_PL", "pt-BR": "pt_BR", "ru": "ru_RU", "es": "es_ES", "tr": "tr_TR"}
NAV = [("features", "/features/", "nav.features", "Features"), ("download", "/download/", "nav.download", "Download"),
       ("updates", "/updates/", "nav.updates", "Updates"), ("wiki", "/wiki/", "nav.wiki", "Wiki"),
       ("faq", "/faq/", "nav.faq", "FAQ")]
IC = {
    "win": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M3 5.6l7.4-1v7H3zM11.4 4.4L21 3v8.6h-9.6zM3 12.4h7.4v7.1L3 18.4zM11.4 12.4H21V21l-9.6-1.4z"/></svg>',
    "mac": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M16.4 12.7c0-2.4 2-3.5 2.1-3.6-1.1-1.7-2.9-1.9-3.5-1.9-1.5-.2-2.9.9-3.6.9-.8 0-1.9-.8-3.1-.8-1.6 0-3.1.9-3.9 2.4-1.7 2.9-.4 7.3 1.2 9.7.8 1.2 1.8 2.5 3 2.4 1.2 0 1.7-.8 3.1-.8 1.5 0 1.9.8 3.2.8 1.3 0 2.1-1.2 2.9-2.4.9-1.3 1.3-2.6 1.3-2.7-.1 0-2.7-1-2.7-4zM14.1 5.6c.7-.8 1.1-1.9 1-3-1 0-2.1.6-2.8 1.4-.6.7-1.2 1.8-1 2.9 1.1.1 2.2-.5 2.8-1.3z"/></svg>',
    "linux": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path fill-rule="evenodd" d="M12 2c-2.3 0-3.7 1.8-3.7 4.3 0 1.3.3 2.2-.6 3.6C6.4 11.7 5 13.9 5 16.4c0 .9.2 1.6.5 2.2-.9.3-1.6.9-1.6 1.6 0 1.1 1.7 1.4 3.3 1.6 1 .1 1.9.6 2.8.6.8 0 1.4-.5 2-.5s1.2.5 2 .5c.9 0 1.8-.5 2.8-.6 1.6-.2 3.3-.5 3.3-1.6 0-.7-.7-1.3-1.6-1.6.3-.6.5-1.3.5-2.2 0-2.5-1.4-4.7-2.7-6.5-.9-1.4-.6-2.3-.6-3.6C15.7 3.8 14.3 2 12 2zm-1.6 3.4a.9 1.1 0 110 2.2.9 1.1 0 010-2.2zm3.2 0a.9 1.1 0 110 2.2.9 1.1 0 010-2.2zM12 11c-2 0-3.4 2.3-3.4 5.2 0 1.5.6 2.6 1.6 3h3.6c1-.4 1.6-1.5 1.6-3 0-2.9-1.4-5.2-3.4-5.2z"/></svg>',
}
GLOBE = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.7 3.8 5.7 3.8 9s-1.3 6.3-3.8 9c-2.5-2.7-3.8-5.7-3.8-9S9.5 5.7 12 3z"/></svg>'

esc = lambda s: html.escape(str(s), quote=True)
STR = json.loads((SRC / "strings.json").read_text(encoding="utf-8"))
T = {**STR["js"], **STR["text"]}          # English by key, for strings written from here


def tr(key, *args, tag="span", cls=""):
    """An element carrying a translatable string: <span data-i18n="key" data-args='[...]'>English</span>."""
    text = T[key]
    for i, a in enumerate(args):
        text = text.replace("{%d}" % i, str(a))
    a = " data-args='%s'" % esc(json.dumps([str(x) for x in args], ensure_ascii=False)) if args else ""
    c = ' class="%s"' % cls if cls else ""
    return '<%s%s data-i18n="%s"%s>%s</%s>' % (tag, c, key, a, text, tag)


# ---------------------------------------------------------------- markdown (the subset the wiki and the FAQ use)
def inline(s):
    out, pos = [], 0
    for m in re.finditer(r"`([^`]+)`", s):             # code spans first: nothing inside them is markup
        out.append(_inline(s[pos:m.start()]))
        out.append("<code>%s</code>" % esc(m.group(1)))
        pos = m.end()
    out.append(_inline(s[pos:]))
    return "".join(out)


def _inline(s):
    s = esc(s).replace("&#x27;", "'")
    s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", r'<img src="\2" alt="\1" loading="lazy">', s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", s)
    return s


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def markdown(text):
    lines, out, i = text.strip("\n").split("\n"), [], 0
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
        elif ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>%s</code></pre>" % esc("\n".join(lines[i + 1:j])))
            i = j + 1
        elif re.match(r"#{2,3} ", ln):
            n = len(ln.split(" ")[0])
            t = ln[n + 1:].strip()
            out.append('<h%d id="%s">%s</h%d>' % (n, slug(t), inline(t), n))
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in re.split(r"(?<!\\)\|", lines[i].strip().strip("|"))])
                i += 1
            body = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c.replace("\\|", "|")) for c in r) for r in rows[2:])
            head = "".join("<th>%s</th>" % inline(c) for c in rows[0])
            out.append('<div class="tablewrap"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (head, body))
        elif re.match(r"(- |\d+\. )", ln):
            tag = "ul" if ln.startswith("- ") else "ol"
            pat = r"- " if tag == "ul" else r"\d+\. "
            items = []
            while i < len(lines) and (re.match(pat, lines[i]) or (lines[i].startswith("  ") and items)):
                if re.match(pat, lines[i]):
                    items.append(re.sub("^" + pat, "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(x) for x in items), tag))
        elif ln.startswith("> "):
            buf = []
            while i < len(lines) and lines[i].startswith(">"):
                buf.append(lines[i].lstrip("> ").strip())
                i += 1
            out.append("<blockquote><p>%s</p></blockquote>" % inline(" ".join(buf)))
        else:
            buf = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(#{2,3} |```|\||- |\d+\. |> )", lines[i]):
                buf.append(lines[i].strip())
                i += 1
            p = inline(" ".join(buf))
            if re.fullmatch(r'<img [^>]+>', p):       # a picture on its own line opens full size
                src = re.search(r'src="([^"]+)"', p).group(1)
                p = '<figure><a class="shot" href="%s">%s</a></figure>' % (src, p)
                out.append(p)
            else:
                out.append("<p>%s</p>" % p)
    return "\n".join(out)


def front(path):
    text = path.read_text(encoding="utf-8")
    m = re.match(r"---\n(.*?)\n---\n(.*)", text, re.S)
    if not m:
        sys.exit("build: %s has no front matter" % path)
    meta = dict((k.strip(), v.strip()) for k, v in (ln.split(":", 1) for ln in m.group(1).splitlines() if ":" in ln))
    return meta, m.group(2)


# ---------------------------------------------------------------- shared page frame
def head_script(version):
    # language before first paint: ?lang= > saved choice > browser languages > English (same rules as src/ui/I18nLocale.h)
    return """<script>
(function(){
var L=window.LYNX_LANGS=%s;
var pick=function(s){
  var p=String(s||"").toLowerCase().replace(/_/g,"-").split("-"),l=p[0],i;
  if(l==="zh"){for(i=1;i<p.length;i++){if(/^(hant|tw|hk|mo)$/.test(p[i]))return"zh-Hant";if(/^(hans|cn|sg)$/.test(p[i]))return"zh-Hans";}return"zh-Hans";}
  if(l==="pt")return"pt-BR";if(l==="in")return"id";
  for(i=0;i<L.length;i++)if(L[i][0].toLowerCase()===l)return L[i][0];
  return"";
};
var q=null,lang="",saved="",h=document.documentElement;
try{q=new URLSearchParams(location.search).get("lang");}catch(e){}
try{saved=localStorage.getItem("lynx-lang")||"";}catch(e){}
lang=pick(q)||pick(saved);
if(!lang){var nl=navigator.languages&&navigator.languages.length?navigator.languages:[navigator.language];for(var i=0;i<nl.length&&!lang;i++)lang=pick(nl[i]);}
lang=lang||"en";
if(q&&pick(q))try{localStorage.setItem("lynx-lang",lang);}catch(e){}
window.LYNX_LANG=lang;window.LYNX_V="%s";
var F={"ja":"JP","ko":"KR","zh-Hans":"SC","zh-Hant":"TC"};
(window.LYNX_FONT=function(l){if(!F[l]||document.getElementById("cjk-"+l))return;var k=document.createElement("link");k.id="cjk-"+l;k.rel="stylesheet";
  k.href="https://fonts.googleapis.com/css2?family=Noto+Sans+"+F[l]+":wght@400;500;700&display=swap";document.head.appendChild(k);})(lang);
h.lang=lang;h.classList.remove("nojs");if(location.search.indexOf("shot")>=0)h.classList.add("shot");
if(lang!=="en"){h.classList.add("i18n-wait");setTimeout(function(){h.classList.remove("i18n-wait");},2500);}
})();
</script>""" % (json.dumps(LANGS, ensure_ascii=False, separators=(",", ":")), version)


def frame(path, cur, title_key, desc_key, body, version, scripts=(), part="", home=False):
    title = T[title_key] if home else T[title_key] + " · LYNX SHARE"
    desc = T[desc_key]
    url = SITE + path
    alts = "".join('<link rel="alternate" hreflang="%s" href="%s%s">\n' % (l, url, "" if l == "en" else "?lang=" + ("pt" if l == "pt-BR" else l)) for l, _ in LANGS)
    ogl = "".join('<meta property="og:locale:alternate" content="%s">\n' % v for v in OGL.values())
    nav = "".join('<a href="%s"%s data-i18n="%s">%s</a>' % (h, ' aria-current="page"' if k == cur else "", key, en) for k, h, key, en in NAV if k != "download")
    attrs = ' data-title="%s" data-desc="%s"' % (title_key, desc_key) + (' data-dict="%s"' % part if part else "") + (' data-home' if home else "")
    js = "".join('<script src="%s?v=%s" defer></script>\n' % (s, version) for s in ("/assets/site.js",) + tuple(scripts))
    return f"""<!doctype html>
<html lang="en" class="nojs"{attrs}>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{esc(desc)}">
<meta property="og:title" content="{esc(title)}">
<meta property="og:description" content="{esc(desc)}">
<meta property="og:site_name" content="LYNX SHARE">
<meta property="og:image" content="{SITE}/assets/img/hero.webp?v=0.1.25">
<meta property="og:url" content="{url}">
<meta property="og:locale" content="en_US">
{ogl}<link rel="canonical" href="{url}">
{alts}<link rel="alternate" hreflang="x-default" href="{url}">
{head_script(version)}
<meta name="theme-color" content="#0d0e12">
<link rel="icon" href="/assets/img/favicon.png?v=0.1.23">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Space+Grotesk:wght@500;700&family=JetBrains+Mono:wght@500&display=swap" rel="stylesheet">
<link rel="stylesheet" href="/assets/site.css?v={version}">
</head>
<body>
<header class="top"><div class="wrap">
  <a href="/" class="brand"><img src="/assets/img/logo.png?v=0.1.23" alt="" width="32" height="32">LYNX SHARE</a>
  <nav class="links" aria-label="Site">{nav}</nav>
  <div class="navbtns">
    <label class="lang">{GLOBE}<span id="langCode" aria-hidden="true">EN</span><select id="lang" aria-label="Language" data-i18n-aria="aria.lang"></select></label>
    <a class="btn btn-red btn-sm" href="/download/"{' aria-current="page"' if cur == "download" else ""} data-i18n="nav.download">Download</a>
  </div>
</div></header>
<main>
{body}
</main>
<footer><div class="wrap">
  <a href="/" class="brand" style="font-size:16px"><img src="/assets/img/logo.png?v=0.1.23" alt="" width="26" height="26" style="width:26px;height:26px">LYNX SHARE</a>
  <span>© 2026 · AGPL-3.0</span>
  <div class="links2"><a href="{REPO}">GitHub</a><a href="{REPO}/releases">Releases</a><a href="{REPO}/issues">Issues</a><a href="https://discord.gg/XHKkbtbkY">Discord</a><a href="/#signing" data-i18n="f.sign">Code signing &amp; privacy</a></div>
  <p class="legal" data-i18n="legal">LYNX SHARE is an independent project, not affiliated with or endorsed by Image-Line, Ableton or BandLab. FL Studio and FL Studio Mobile are trademarks of Image-Line Software; Ableton and Live are trademarks of Ableton AG; BandLab is a trademark of BandLab Technologies.</p>
</div></footer>
<script>window.LYNX_EN={json.dumps(STR["js"], ensure_ascii=False, separators=(",", ":"))};</script>
{js}</body>
</html>
"""


# ---------------------------------------------------------------- updates
def vkey(rel):
    return "u." + rel["version"].split("-")[0]          # u.0.1.25


def item(text):
    return _inline_item(text)


def _inline_item(s):
    return inline(s)


GROUPS = [("new", "up.new", "n"), ("better", "up.better", "b"), ("fixed", "up.fixed", "f"), ("known", "up.known", "k")]


def render_updates(rels):
    jump = "".join('<a href="#%s">%s</a>' % (r["tag"], r["version"].split("-")[0]) for r in rels)
    out = ['<div class="phead"><div class="wrap"><h1 data-i18n="up.h1">%s</h1><p data-i18n="up.sub">%s</p><nav class="jump" aria-label="Versions">%s</nav></div></div>'
           % (T["up.h1"], T["up.sub"], jump), '<div class="wrap">']
    for r in rels:
        k = vkey(r)
        num, _, stage = r["version"].partition("-")
        parts = ['<article class="rel" id="%s"><header><h2><a href="#%s">%s</a>%s</h2><time datetime="%s">%s</time><a class="gh" href="%s" data-i18n="up.gh">%s</a></header><div>'
                 % (r["tag"], r["tag"], num, "<small>%s</small>" % stage if stage else "", r["date"], r["date"], esc(r["url"]), T["up.gh"]),
                 '<h3 data-i18n="%s.t">%s</h3><p class="sum" data-i18n="%s.s">%s</p>' % (k, inline(r["title"]), k, inline(r["summary"]))]
        if r.get("shots"):
            parts.append('<div class="relshots">%s</div>' % "".join(
                '<figure><a class="shot" href="%s"><img src="%s" alt="%s" loading="lazy"></a></figure>' % (s["src"], s["src"], esc(s["alt"])) for s in r["shots"]))
        for g, gkey, short in GROUPS:
            items = r.get(g) or []
            if items:
                parts.append('<h4 class="g-%s" data-i18n="%s">%s</h4><ul>%s</ul>' % (g, gkey, T[gkey], "".join(
                    '<li data-i18n="%s.%s%d">%s</li>' % (k, short, i, inline(x)) for i, x in enumerate(items))))
        parts.append("</div></article>")
        out.append("".join(parts))
    out.append("</div>")
    return "\n".join(out)


def render_latest(r):
    k = vkey(r)
    items = "".join('<li data-i18n="%s.n%d">%s</li>' % (k, i, inline(x)) for i, x in enumerate((r.get("new") or [])[:4]))
    return ('<div class="latest"><div><div class="ver">%s</div><time datetime="%s">%s</time><a class="tlink" href="%s" data-i18n="up.gh">%s</a></div>'
            '<div><h3 data-i18n="%s.t">%s</h3><p class="muted" data-i18n="%s.s">%s</p><ul style="margin-top:12px">%s</ul>'
            '<p class="more"><a class="tlink" href="/updates/#%s" data-i18n="home.latestAll">%s</a></p></div></div>'
            % (r["version"], r["date"], r["date"], esc(r["url"]), T["up.gh"], k, inline(r["title"]), k, inline(r["summary"]), items, r["tag"], T["home.latestAll"]))


# ---------------------------------------------------------------- plugin lists (Features)
def plugin_data():
    src = (SRC / "plugins.js").read_text(encoding="utf-8")
    grab = lambda name: json.loads(re.search(r"const %s = (\[.*?\n\]);" % name, src, re.S).group(1))
    ins = json.loads(re.search(r"const INSPIRED = (\{.*?\});\n", src).group(1))
    return grab("PLUGINS"), grab("FEATURED"), ins


def render_plugins(plugins):
    out = []
    for n, fl, ty, s in plugins:
        out.append('<div class="pl" data-t="%s"><i class="st %s"></i><div class="tx"><b>%s</b>%s</div><span class="k">%s</span></div>'
                   % (ty, s, esc(n), tr("pl.insp", esc(fl), tag="small"), "GEN" if ty == "g" else "FX"))
    return "".join(out)


def render_featured(featured):
    out = []
    for f in featured:
        out.append('<article class="ft"><div class="top"><span class="badge"><i class="st %s"></i>%s</span><span class="k">%s</span></div><h4>%s</h4>'
                   '<div class="by">%s · %s</div><p data-feat="%s" data-en="%s">%s</p><div class="how">%s</div></article>'
                   % (f["s"], tr("pl." + f["s"]), "GEN" if f["t"] == "g" else "FX", esc(f["n"]), tr("pl.insp", esc(f["fl"])), esc(f["by"]),
                      esc(f["n"]), esc(f["en"]), esc(f["en"]), tr("ft." + f["how"])))
    return "".join(out)


def render_classics(ins):
    groups = ins["groups"]
    allitems = [i for g in groups for i in g["items"]]
    own = len({i["lynx"] for i in allitems if i["state"] == "own"})
    auto = sum(1 for i in allitems if i["auto"])
    wip = sum(1 for i in allitems if i["state"] == "wip")
    stats = "".join("<span><b>%d</b>%s</span>" % (n, tr(k)) for n, k in ((own, "cl.s1"), (auto, "cl.s2"), (wip, "cl.s3")) if n)
    tabs = '<button class="pl-f on" data-v="all" data-i18n="pl.all">%s</button>' % T["pl.all"] + "".join(
        '<button class="pl-f" data-v="%s">%s</button>' % (esc(g["vendor"]), esc(g["vendor"])) for g in groups)
    out = ['<div class="cl-stats">%s</div><div class="pl-bar" id="clTabs">%s</div><div id="clGroups">' % (stats, tabs)]
    for g in groups:
        main = [i for i in g["items"] if i["state"] != "standin"]
        alt = [i for i in g["items"] if i["state"] == "standin"]
        cnt = lambda s: sum(1 for i in g["items"] if i["state"] == s)
        summ = " · ".join(tr(k, c) for c, k in ((cnt("own"), "cl.natN" if cnt("own") > 1 else "cl.nat1"), (cnt("wip"), "cl.prog"),
                                                (cnt("standin"), "cl.subN" if cnt("standin") > 1 else "cl.sub1")) if c)
        cards = []
        for i in main:
            foot = []
            if i["state"] == "wip" and i["lynx"]:
                foot.append(tr("cl.today", esc(i["lynx"])))
            if i["auto"]:
                foot.append(tr("cl.auto"))
            key = g["vendor"] + "/" + i["short"]
            kind = '<span class="k" data-i18n="kind.%s">%s</span>' % (esc(i["kind"]), esc(T.setdefault("kind." + i["kind"], i["kind"]))) if i.get("kind") else ""
            cards.append('<article class="cl %s"><div class="top"><span class="badge"><i class="st %s"></i>%s</span>%s</div><div class="orig">%s</div><h4>%s</h4>'
                         '<p data-insp="%s" data-en="%s">%s</p>%s</article>'
                         % (i["state"], "x" if i["state"] == "own" else "w", tr("cl.own" if i["state"] == "own" else "cl.wip"), kind,
                            tr("pl.insp", "<em>%s %s</em>" % (esc(g["label"]), esc(i["short"]))), esc(i["name"]["en"]),
                            esc(key), esc(i["desc"]["en"]), esc(i["desc"]["en"]), '<div class="foot">%s</div>' % "".join(foot) if foot else ""))
        alts = ""
        if alt:
            alts = '<div class="cl-alt">%s%s</div>' % (tr("cl.alt"), "".join(
                '<span class="cl-chip">%s <i>→</i> <b>%s</b></span>' % (esc(i["short"]), esc(i["lynx"])) for i in alt))
        out.append('<div class="cl-v" data-v="%s"><div class="cl-vh"><h3>%s</h3><p>%s</p></div><div class="cl-grid">%s</div>%s</div>'
                   % (esc(g["vendor"]), esc(g["vendor"]), summ, "".join(cards), alts))
    out.append("</div>")
    return "".join(out)


# ---------------------------------------------------------------- wiki + FAQ
def wiki_pages():
    index = json.loads((SRC / "wiki/index.json").read_text(encoding="utf-8"))
    pages = {}
    for g in index:
        for s in g["pages"]:
            meta, body = front(SRC / "wiki" / (s + ".md"))
            pages[s] = {"slug": s, "meta": meta, "body": body}
            T["wk.%s.t" % s] = meta["title"]
            T["wk.%s.s" % s] = meta["summary"]
        T[g["key"]] = g["title"]
    return index, pages


def wiki_nav(index, cur):
    out = ['<nav class="wnav" id="wnav" aria-label="Wiki">']
    for g in index:
        out.append('<h2 data-i18n="%s">%s</h2>' % (g["key"], esc(g["title"])))
        for s in g["pages"]:
            out.append('<a href="/wiki/%s/"%s data-i18n="wk.%s.t">%s</a>' % (s, ' aria-current="page"' if s == cur else "", s, esc(T["wk.%s.t" % s])))
    out.append("</nav>")
    return "".join(out)


def render_wiki_page(index, pages, s):
    order = [p for g in index for p in g["pages"]]
    n = order.index(s)
    p = pages[s]
    short = ' <span class="tag" data-i18n="wiki.short">%s</span>' % T["wiki.short"] if p["meta"].get("status") == "short" else ""
    prev = '<a href="/wiki/%s/"><span data-i18n="wiki.prev">%s</span><b data-i18n="wk.%s.t">%s</b></a>' % (order[n - 1], T["wiki.prev"], order[n - 1], esc(T["wk.%s.t" % order[n - 1]])) if n else "<span></span>"
    nxt = '<a href="/wiki/%s/" style="text-align:right"><span data-i18n="wiki.next">%s</span><b data-i18n="wk.%s.t">%s</b></a>' % (order[n + 1], T["wiki.next"], order[n + 1], esc(T["wk.%s.t" % order[n + 1]])) if n + 1 < len(order) else "<span></span>"
    return ('<div class="wrap wiki">%s<article><a class="wjump" href="#wnav" data-i18n="wiki.all">%s</a><h1><span data-i18n="wk.%s.t">%s</span>%s</h1><p class="sub" data-i18n="wk.%s.s">%s</p>'
            '<p class="enonly" data-i18n="wiki.enOnly">%s</p><div class="prose">%s</div><nav class="wpn">%s%s</nav></article></div>'
            % (wiki_nav(index, s), T["wiki.all"], s, esc(T["wk.%s.t" % s]), short, s, esc(T["wk.%s.s" % s]), T["wiki.enOnly"], markdown(p["body"]), prev, nxt))


def render_wiki_index(index, pages):
    groups = []
    for g in index:
        rows = "".join('<li><a href="/wiki/%s/" data-i18n="wk.%s.t">%s</a><span data-i18n="wk.%s.s">%s</span></li>'
                       % (s, s, esc(T["wk.%s.t" % s]), s, esc(T["wk.%s.s" % s])) for s in g["pages"])
        groups.append('<section class="wgroup"><h2 data-i18n="%s">%s</h2><ul class="wlist">%s</ul></section>' % (g["key"], esc(g["title"]), rows))
    return ('<div class="phead"><div class="wrap"><h1 data-i18n="wiki.h1">%s</h1><p data-i18n="wiki.sub">%s</p></div></div>'
            '<div class="wrap wikiidx">'
            '<p class="enonly" data-i18n="wiki.enOnly">%s</p>%s</div>' % (T["wiki.h1"], T["wiki.sub"], T["wiki.enOnly"], "".join(groups)))


def render_faq():
    meta, body = front(SRC / "faq.md")
    out = ['<div class="phead"><div class="wrap"><h1 data-i18n="faq.h1">%s</h1><p data-i18n="faq.sub">%s</p></div></div><div class="wrap" style="padding-bottom:80px"><div class="faq prose">' % (T["faq.h1"], T["faq.sub"])]
    for n, sec in enumerate(re.split(r"^## ", body, flags=re.M)[1:], 1):
        q, _, a = sec.partition("\n")
        out.append('<section id="%s"><h2 data-i18n="faq.q%d">%s</h2><div data-i18n="faq.a%d">%s</div></section>' % (slug(q)[:48].strip("-"), n, inline(q.strip()), n, markdown(a).replace("\n", "")))
    out.append("</div></div>")
    return "".join(out)


# ---------------------------------------------------------------- English strings out of the built pages
ELEM = re.compile(r'<(\w+)\b([^>]*?)\sdata-i18n="([^"]+)"([^>]*)>(.*?)</\1>', re.S)


def fill_args(page):
    """Elements written with data-args in a page source hold {0}-style English: show the filled text."""
    def sub(m):
        attrs = m.group(2) + m.group(4)
        a = re.search(r"data-args='([^']*)'", attrs)
        if not a or "{0}" not in m.group(5):
            return m.group(0)
        text = m.group(5)
        T.setdefault(m.group(3), text)
        for i, v in enumerate(json.loads(html.unescape(a.group(1)))):
            text = text.replace("{%d}" % i, v)
        return "<%s%s data-i18n=\"%s\"%s>%s</%s>" % (m.group(1), m.group(2), m.group(3), m.group(4), text, m.group(1))
    return ELEM.sub(sub, page)


def collect(page, en, where):
    def put(k, v):
        if k in T and "{0}" in T[k]:
            v = T[k]                                   # the template, not this element's filled text
        if en.setdefault(k, v) != v:
            sys.exit("build: '%s' has two different English texts (%s):\n  %s\n  %s" % (k, where, en[k], v))
    for m in ELEM.finditer(page):
        put(m.group(3), m.group(5))
    for v, k in re.findall(r'alt="([^"]*)" data-i18n-alt="([^"]+)"', page):
        put(k, v)
    for v, k in re.findall(r'aria-label="([^"]*)"[^>]*data-i18n-aria="([^"]+)"', page):
        put(k, v)
    m = re.search(r'<h1 data-i18n-words="([^"]+)">(.*?)</h1>', page, re.S)
    if m:
        put(m.group(1), re.sub(r'\s*<span class="grad">(.*?)</span>', r"|\1", m.group(2)))


def main():
    check = "--check" in sys.argv
    rels = json.loads((SRC / "updates.json").read_text(encoding="utf-8"))
    latest = rels[0]
    index, pages = wiki_pages()
    plugins, featured, ins = plugin_data()
    fl = [p for p in plugins if p[3] != "o"]
    sub = {"VER_UP": latest["version"].upper(), "VER_LINE": "v" + latest["version"], "LATEST": render_latest(latest),
           "IC_WIN": IC["win"], "IC_MAC": IC["mac"], "IC_LINUX": IC["linux"],
           "OS_BTNS": "".join('<a class="btn btn-ghost" data-os="%s" data-dl="%s.main" href="%s/releases/latest">%s<span>%s</span><small data-i18n="%s">%s</small><em data-yours></em></a>'
                              % (o, o, REPO, IC[o], name, k, T[k]) for o, name, k in (("win", "Windows", "dl.fInstaller"), ("mac", "macOS", "dl.fUniversal"), ("linux", "Linux", "dl.fAppImage"))),
           "PLUGINS": render_plugins(plugins), "FEATURED": render_featured(featured), "CLASSICS": render_classics(ins), "CL_TM": ins["tm"]["en"],
           "N_ALL": str(len(plugins)), "N_FL": str(len(fl)), "N_EXACT": str(sum(1 for p in fl if p[3] in "xn"))}

    def page(name):
        body = (SRC / "pages" / (name + ".html")).read_text(encoding="utf-8")
        body = re.sub(r"\{\{(\w+)\}\}", lambda m: sub[m.group(1)], body)
        return fill_args(body)

    bodies = [   # path, nav item, title key, description key, body, scripts, extra dictionary
        ("/", "home", "m.home.t", "m.home.d", page("home"), (), "latest"),
        ("/features/", "features", "fe.h1", "m.features.d", page("features"), ("/assets/features.js",), ""),
        ("/download/", "download", "dlp.h1", "m.download.d", page("download"), (), ""),
        ("/updates/", "updates", "up.h1", "m.updates.d", render_updates(rels), (), "updates"),
        ("/wiki/", "wiki", "wiki.h1", "m.wiki.d", render_wiki_index(index, pages), (), ""),
        ("/faq/", "faq", "faq.h1", "m.faq.d", render_faq(), (), ""),
    ] + [("/wiki/%s/" % s, "wiki", "wk.%s.t" % s, "wk.%s.s" % s, render_wiki_page(index, pages, s), (), "") for g in index for s in g["pages"]]

    en = dict(T)
    for b in bodies:
        collect(b[4], en, b[0])
    T.update(en)
    collect(frame("/", "", "m.home.t", "m.home.d", "", "0", home=True), en, "frame")
    ui = {k: v for k, v in en.items() if not k.startswith("u.")}
    upd = {k: v for k, v in en.items() if k.startswith("u.")}
    featured_en = {f["n"]: f["en"] for f in featured}
    insp_en = {g["vendor"] + "/" + i["short"]: i["desc"]["en"] for g in ins["groups"] for i in g["items"] if i["desc"]["en"]}
    dump = lambda d: json.dumps(d, ensure_ascii=False, indent=0) + "\n"
    files = {"i18n/en.json": dump({"ui": ui, "featured": featured_en, "insp": insp_en}), "i18n/updates/en.json": dump(upd)}
    lk = vkey(latest) + "."
    for f in sorted((OUT / "i18n/updates").glob("*.json")) if (OUT / "i18n/updates").is_dir() else []:
        d = upd if f.stem == "en" else json.loads(f.read_text(encoding="utf-8"))
        files["i18n/latest/%s.json" % f.stem] = dump({k: v for k, v in d.items() if k.startswith(lk)})
    files["i18n/latest/en.json"] = dump({k: v for k, v in upd.items() if k.startswith(lk)})
    # one version stamp for css/js/json addresses: changes when any of them does
    stamp = hashlib.sha1()
    for name in ("assets/site.css", "assets/site.js", "assets/features.js"):
        stamp.update((OUT / name).read_bytes())
    for name in sorted(files):
        stamp.update(files[name].encode())
    for f in sorted((OUT / "i18n").glob("*.json")):
        if f.name != "en.json":
            stamp.update(f.read_bytes())
    version = stamp.hexdigest()[:8]
    for path, cur, tk, dk, body, scripts, part in bodies:
        files[(path.strip("/") + "/index.html").lstrip("/")] = frame(path, cur, tk, dk, body, version, scripts, part, home=path == "/")

    stale = []
    for name, text in files.items():
        f = OUT / name
        if not f.exists() or f.read_text(encoding="utf-8") != text:
            stale.append(name)
            if not check:
                f.parent.mkdir(parents=True, exist_ok=True)
                f.write_text(text, encoding="utf-8")
    if check and stale:
        print("build: out of date:", ", ".join(stale), "(run website/_src/build.py)")
        return 1
    print("build: %d files, %d strings + %d update strings, %s" % (len(files), len(ui), len(upd), "up to date" if check else "%d written" % len(stale)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
