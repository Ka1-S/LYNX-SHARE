#!/usr/bin/env python3
"""Build lynx-share.com: writes the static pages of website/ from the sources in website/_src/.

    python3 website/_src/build.py           write the pages and the English string files
    python3 website/_src/build.py --check   exit 1 if a written file is out of date (release step)

Sources (all in _src/):  pages/*.html  body of Home, Features, Why, Download  updates.json  one entry per release
                         catalog.json  the built-in plugins (cards + wiki pages), catalog-img.json their picture sizes
                         wiki/*.md + wiki/index.json  the manual             faq.md        the FAQ
                         strings.json  English of strings that have no element of their own
                         (plugins.js is not shown on the site any more; scripts/gen-inspired.py still writes it)
Output: index.html, features/, why/, instruments-and-effects/, download/, updates/, wiki/, wiki/<page>/,
        wiki/built-in/<plugin>/, faq/ (each an index.html), wiki/search.json, i18n/catalog/en.json,
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
KOFI = "https://ko-fi.com/lynxshare"  # the donation page (optional; = lynx::links::donate in src/core/Links.h)
LOGO_V = "0.1.26-logo2"  # cache-buster of the logo pictures (logo by w3teal; its app-icon form since 2026-11-13)
# the header/footer logo: at 1x the icon's small-size form (32 px, tools/make_logo.py), at 2x the 64 px icon, above that 256 px
LOGO_SRCSET = (f"/assets/img/favicon-32.png?v={LOGO_V} 1x, /assets/img/favicon.png?v={LOGO_V} 2x, "
               f"/assets/img/logo.png?v={LOGO_V} 4x")
LANGS = [["en", "English"], ["zh-Hans", "简体中文"], ["zh-Hant", "繁體中文"], ["nl", "Nederlands"], ["fr", "Français"],
         ["de", "Deutsch"], ["id", "Bahasa Indonesia"], ["it", "Italiano"], ["ja", "日本語"], ["ko", "한국어"],
         ["pl", "Polski"], ["pt-BR", "Português (Brasil)"], ["ru", "Русский"], ["es", "Español"], ["tr", "Türkçe"]]
OGL = {"zh-Hans": "zh_CN", "zh-Hant": "zh_TW", "nl": "nl_NL", "fr": "fr_FR", "de": "de_DE", "id": "id_ID", "it": "it_IT",
       "ja": "ja_JP", "ko": "ko_KR", "pl": "pl_PL", "pt-BR": "pt_BR", "ru": "ru_RU", "es": "es_ES", "tr": "tr_TR"}
NAV = [("features", "/features/", "nav.features", "Features"), ("why", "/why/", "nav.why", "Why LYNX"),
       ("cat", "/instruments-and-effects/", "nav.cat", "Plugins"), ("download", "/download/", "nav.download", "Download"),
       ("updates", "/updates/", "nav.updates", "Updates"), ("wiki", "/wiki/", "nav.wiki", "Wiki"),
       ("faq", "/faq/", "nav.faq", "FAQ")]
# plugin catalogue: type, then its categories (catalog.json "cat")
CATS = [("instrument", ["synth", "drums", "sampler"]),
        ("effect", ["eq", "dynamics", "reverb", "delay", "modulation", "distortion", "pitch", "stereo"]),
        ("tool", ["controller", "visual", "routing"])]
TYPE_OF = {c: t for t, cs in CATS for c in cs}
IC = {
    "win": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M3 5.6l7.4-1v7H3zM11.4 4.4L21 3v8.6h-9.6zM3 12.4h7.4v7.1L3 18.4zM11.4 12.4H21V21l-9.6-1.4z"/></svg>',
    "mac": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M16.4 12.7c0-2.4 2-3.5 2.1-3.6-1.1-1.7-2.9-1.9-3.5-1.9-1.5-.2-2.9.9-3.6.9-.8 0-1.9-.8-3.1-.8-1.6 0-3.1.9-3.9 2.4-1.7 2.9-.4 7.3 1.2 9.7.8 1.2 1.8 2.5 3 2.4 1.2 0 1.7-.8 3.1-.8 1.5 0 1.9.8 3.2.8 1.3 0 2.1-1.2 2.9-2.4.9-1.3 1.3-2.6 1.3-2.7-.1 0-2.7-1-2.7-4zM14.1 5.6c.7-.8 1.1-1.9 1-3-1 0-2.1.6-2.8 1.4-.6.7-1.2 1.8-1 2.9 1.1.1 2.2-.5 2.8-1.3z"/></svg>',
    "linux": '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path fill-rule="evenodd" d="M12 2c-2.3 0-3.7 1.8-3.7 4.3 0 1.3.3 2.2-.6 3.6C6.4 11.7 5 13.9 5 16.4c0 .9.2 1.6.5 2.2-.9.3-1.6.9-1.6 1.6 0 1.1 1.7 1.4 3.3 1.6 1 .1 1.9.6 2.8.6.8 0 1.4-.5 2-.5s1.2.5 2 .5c.9 0 1.8-.5 2.8-.6 1.6-.2 3.3-.5 3.3-1.6 0-.7-.7-1.3-1.6-1.6.3-.6.5-1.3.5-2.2 0-2.5-1.4-4.7-2.7-6.5-.9-1.4-.6-2.3-.6-3.6C15.7 3.8 14.3 2 12 2zm-1.6 3.4a.9 1.1 0 110 2.2.9 1.1 0 010-2.2zm3.2 0a.9 1.1 0 110 2.2.9 1.1 0 010-2.2zM12 11c-2 0-3.4 2.3-3.4 5.2 0 1.5.6 2.6 1.6 3h3.6c1-.4 1.6-1.5 1.6-3 0-2.9-1.4-5.2-3.4-5.2z"/></svg>',
}
GLOBE = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.7 3.8 5.7 3.8 9s-1.3 6.3-3.8 9c-2.5-2.7-3.8-5.7-3.8-9S9.5 5.7 12 3z"/></svg>'

esc = lambda s: html.escape(str(s), quote=True).replace("&#x27;", "'")     # (attributes are always in double quotes)
STR = json.loads((SRC / "strings.json").read_text(encoding="utf-8"))
T = {**STR["js"], **STR["text"]}          # English by key, for strings written from here


def tr(key, *args, tag="span", cls="", count=None):
    """An element carrying a translatable string: <span data-i18n="key" data-args='[...]'>English</span>."""
    text = T[key]
    for i, a in enumerate(args):
        text = text.replace("{%d}" % i, str(a))
    a = " data-args='%s'" % html.escape(json.dumps([str(x) for x in args], ensure_ascii=False), quote=True) if args else ""
    c = ' class="%s"' % cls if cls else ""
    if count is not None:                              # assets/motion.js counts this number up once, when it scrolls in
        a += ' data-count="%d"' % count
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


def markdown(text, ids=None):
    """ids: the heading ids to use, in order (a translated body keeps the English anchors)."""
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
            out.append('<h%d id="%s">%s</h%d>' % (n, next(ids, "") if ids is not None else slug(t), inline(t), n))
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
else if(window.IntersectionObserver&&window.matchMedia&&!matchMedia("(prefers-reduced-motion: reduce)").matches)h.classList.add("mo");
if((" "+(h.getAttribute("data-wt")||"")+" ").indexOf(" "+lang+" ")>=0)h.classList.add("wt");
if(lang!=="en"){h.classList.add("i18n-wait");setTimeout(function(){h.classList.remove("i18n-wait");},2500);}
})();
</script>""" % (json.dumps(LANGS, ensure_ascii=False, separators=(",", ":")), version)


SIZES = json.loads((SRC / "img-sizes.json").read_text(encoding="utf-8"))     # written by import-shots.py
SIZES.update({"/assets/img/plugins/%s.webp" % k: v[:2] for k, v in json.loads((SRC / "catalog-img.json").read_text(encoding="utf-8")).items()})


def dense_srcset(tag):
    """srcset with the @2x file when scripts/site-shots.sh drew one (twice the size, drawn by the program at 2x) for a
    picture of the site's own outside plugins/ (the plugin cards choose their srcset themselves, see plugin_img)."""
    src = re.search(r'\ssrc="(/assets/img/(?!plugins/)[^"?]+?)\.(webp|png)(\?[^"]*)?"', tag)
    if " srcset=" in tag or not src or not (OUT / (src.group(1) + "@2x." + src.group(2))[1:]).is_file():
        return tag
    one = "%s.%s%s" % (src.group(1), src.group(2), src.group(3) or "")
    two = "%s@2x.%s%s" % (src.group(1), src.group(2), src.group(3) or "")
    return tag.replace(src.group(0), '%s srcset="%s 1x, %s 2x"' % (src.group(0), one, two), 1)


def sized(page):
    """width and height on every picture of the site's own that lacks them: nothing is stretched, nothing jumps on load."""
    def fix(m):
        tag = dense_srcset(m.group(0))
        src = re.search(r'\ssrc="([^"?]+)', tag)
        if " width=" in tag or not src or src.group(1) not in SIZES:
            return tag
        return '%s width="%d" height="%d">' % (tag[:-1], *SIZES[src.group(1)])
    return re.sub(r"<img\b[^>]*>", fix, page)


HERO_H1 = re.compile(r'(<h1 data-i18n-words="[^"]+">)(.*?)\s*<span class="grad">(.*?)</span></h1>', re.S)


def hero_words(m):
    """The headline word by word (site.css animates .w in turn; assets/site.js writes the same for other languages)."""
    ws = m.group(2).split()
    return "%s%s <span class=\"grad w\" style=\"--i:%d\">%s</span></h1>" % (
        m.group(1), " ".join('<span class="w" style="--i:%d">%s</span>' % (i, w) for i, w in enumerate(ws)), len(ws), m.group(3))


def frame(path, cur, title_key, desc_key, body, version, scripts=(), part="", home=False, wt=""):
    """title_key: a string key, or ("", "Plain title") for a title that is the same in every language."""
    plain = title_key[1] if isinstance(title_key, tuple) else None
    title_key = "" if plain else title_key
    title = T[title_key] if home else (plain or T[title_key]) + " · LYNX SHARE"
    desc = re.sub(r"<[^>]+>", "", T[desc_key])
    url = SITE + path
    alts = "".join('<link rel="alternate" hreflang="%s" href="%s%s">\n' % (l, url, "" if l == "en" else "?lang=" + ("pt" if l == "pt-BR" else l)) for l, _ in LANGS)
    ogl = "".join('<meta property="og:locale:alternate" content="%s">\n' % v for v in OGL.values())
    nav = "".join('<a href="%s"%s data-i18n="%s">%s</a>' % (h, ' aria-current="page"' if k == cur else "", key, en) for k, h, key, en in NAV if k != "download")
    attrs = (' data-title="%s"' % title_key if title_key else "") + ' data-desc="%s"' % desc_key + (' data-dict="%s"' % part if part else "") + (' data-home' if home else "") + (' data-wt="%s"' % wt if wt else "")
    js = "".join('<script src="%s?v=%s" defer></script>\n' % (s, version) for s in ("/assets/site.js",) + tuple(scripts) + ("/assets/motion.js",))
    body = HERO_H1.sub(hero_words, body)
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
<meta property="og:image" content="{SITE}/assets/img/hero.webp?v=0.1.27">
<meta property="og:url" content="{url}">
<meta property="og:locale" content="en_US">
{ogl}<link rel="canonical" href="{url}">
{alts}<link rel="alternate" hreflang="x-default" href="{url}">
{head_script(version)}
<meta name="theme-color" content="#0d0e12">
<link rel="icon" type="image/png" sizes="64x64" href="/assets/img/favicon.png?v={LOGO_V}">
<link rel="icon" type="image/png" sizes="32x32" href="/assets/img/favicon-32.png?v={LOGO_V}">
<link rel="icon" type="image/png" sizes="16x16" href="/assets/img/favicon-16.png?v={LOGO_V}">
<link rel="apple-touch-icon" href="/assets/img/apple-touch-icon.png?v={LOGO_V}">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Space+Grotesk:wght@500;700&family=JetBrains+Mono:wght@500&display=swap" rel="stylesheet">
<link rel="stylesheet" href="/assets/site.css?v={version}">
</head>
<body>
<header class="top"><div class="wrap">
  <a href="/" class="brand"><img src="/assets/img/logo.png?v={LOGO_V}" srcset="{LOGO_SRCSET}" alt="" width="32" height="32">LYNX SHARE</a>
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
  <a href="/" class="brand" style="font-size:16px"><img src="/assets/img/logo.png?v={LOGO_V}" srcset="{LOGO_SRCSET}" alt="" width="26" height="26" style="width:26px;height:26px">LYNX SHARE</a>
  <span>© 2026 · AGPL-3.0</span>
  <span data-i18n="f.logo">Logo by w3teal</span>
  <div class="links2"><a href="{REPO}">GitHub</a><a href="{REPO}/releases">Releases</a><a href="{REPO}/issues">Issues</a><a href="https://discord.gg/XHKkbtbkY">Discord</a><a href="/#support" data-i18n="f.support">Support</a><a href="/#signing" data-i18n="f.sign">Code signing &amp; privacy</a></div>
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


# ---------------------------------------------------------------- built-in plugin catalogue
def catalog():
    """catalog.json: one entry per built-in plugin of the released version; catalog-img.json: picture sizes (import-shots.py)."""
    items = json.loads((SRC / "catalog.json").read_text(encoding="utf-8"))
    dims = json.loads((SRC / "catalog-img.json").read_text(encoding="utf-8"))
    order = [c for _, cs in CATS for c in cs]
    for p in items:
        p["type"] = TYPE_OF[p["cat"]]
        p["dim"] = dims.get(p["id"])
        T["c." + p["id"]] = inline(p["desc"])
    items.sort(key=lambda p: (order.index(p["cat"]), p["name"].lower()))
    return items


def plugin_tags(p):
    out = '<span class="tag t-%s" data-i18n="cat.t.%s">%s</span><span class="tag" data-i18n="cat.c.%s">%s</span>' % (
        p["type"], p["type"], T["cat.t." + p["type"]], p["cat"], T["cat.c." + p["cat"]])
    if p.get("flp"):
        out += tr("cat.flp", esc(p["flp"]), cls="tag flp")
    return out


def plugin_img(p, lazy=True, card=False):
    """width/height are the picture's own size, so it is never shown larger. Editors that 0.1.25 could also draw at
    twice the size have <id>@2x.webp for dense screens; a card is small enough for the first file unless it is narrow."""
    if not p["dim"]:
        return '<span class="plate">%s</span>' % esc(p["name"])       # no picture of this editor: a name plate, not a fake
    src = "/assets/img/plugins/%s.webp" % p["id"]
    dense = ' srcset="%s 1x, %s 2x"' % (src, src.replace(".webp", "@2x.webp")) if len(p["dim"]) > 2 and not (card and p["dim"][0] >= 720) else ""
    return '<img src="%s"%s alt="%s" width="%d" height="%d"%s>' % (src, dense, esc(p["name"]), p["dim"][0], p["dim"][1], ' loading="lazy"' if lazy else "")


def render_catalog(cat, version):
    n = lambda f: sum(1 for p in cat if f(p))
    chips = ['<a href="#cat" data-f="all" class="on"><span data-i18n="cat.all">%s</span><b data-count="%d">%d</b></a>' % (T["cat.all"], len(cat), len(cat))]
    body = []
    for ty, cs in CATS:
        chips.append('<a href="#t-%s" data-f="t:%s" class="ty"><span data-i18n="cat.g.%s">%s</span><b data-count="%d">%d</b></a>' % (ty, ty, ty, T["cat.g." + ty], n(lambda p: p["type"] == ty), n(lambda p: p["type"] == ty)))
        body.append('<h2 class="ctype" id="t-%s" data-type="%s"><span data-i18n="cat.g.%s">%s</span> <small data-count="%d">%d</small></h2>' % (ty, ty, ty, T["cat.g." + ty], n(lambda p: p["type"] == ty), n(lambda p: p["type"] == ty)))
        for c in cs:
            mine = [p for p in cat if p["cat"] == c]
            if not mine:
                continue
            chips.append('<a href="#g-%s" data-f="c:%s"><span data-i18n="cat.c.%s">%s</span><b>%d</b></a>' % (c, c, c, T["cat.c." + c], len(mine)))
            cards = []
            for p in mine:
                href = "/wiki/built-in/%s/" % p["id"] if p.get("wiki") else ""
                pic = plugin_img(p, card=True)
                name = esc(p["name"])
                cards.append('<article class="pc" id="p-%s">%s<div class="pb"><h4>%s</h4><p data-i18n="c.%s">%s</p><div class="tags">%s</div></div></article>' % (
                    p["id"], '<a class="pimg" href="%s" tabindex="-1" aria-hidden="true">%s</a>' % (href, pic) if href else '<div class="pimg">%s</div>' % pic,
                    '<a href="%s">%s</a>' % (href, name) if href else name, p["id"], T["c." + p["id"]], plugin_tags(p)))
            body.append('<section class="cgroup" id="g-%s" data-type="%s"><h3><span data-i18n="cat.c.%s">%s</span> <small>%d</small></h3><div class="cgrid">%s</div></section>'
                        % (c, ty, c, T["cat.c." + c], len(mine), "".join(cards)))
    return ('<div class="phead"><div class="wrap"><h1 data-i18n="cat.h1">%s</h1>%s<nav class="cfilter" id="cfilter" aria-label="Categories">%s</nav></div></div>'
            '<div class="wrap cat" id="cat">%s<p class="note" data-i18n="cat.tm">%s</p></div>'
            % (T["cat.h1"], tr("cat.sub", len(cat), version, tag="p", count=len(cat)), "".join(chips), "".join(body), T["cat.tm"]))


def render_whyrow(cat):
    cells = "".join('<a class="wcell" href="/why/#%s"><b data-i18n="why.%s.t">%s</b><span data-i18n="why.%s.l">%s</span></a>' % (a, k, T["why.%s.t" % k], k, T["why.%s.l" % k])
                    for a, k in (("online", "online"), ("projects", "projects"), ("fl-settings", "fl"), ("workflows", "work"), ("crash", "crash"), ("free", "free")))
    cells += '<a class="wcell hot" href="/instruments-and-effects/">%s<span data-i18n="home.catL">%s</span></a>' % (tr("home.catT", len(cat), tag="b", count=len(cat)), T["home.catL"])
    return '<div class="whyrow">%s</div>' % cells


# ---------------------------------------------------------------- wiki + FAQ
def wiki_pages():
    index = json.loads((SRC / "wiki/index.json").read_text(encoding="utf-8"))
    pages = {}
    for g in index:
        for s in g["pages"] + g.get("hidden", []):
            meta, body = front(SRC / "wiki" / (s + ".md"))
            pages[s] = {"slug": s, "meta": meta, "body": body, "prose": markdown(body)}
            T["wk.%s.t" % s] = meta["title"]
            T["wk.%s.s" % s] = meta["summary"]
        T[g["key"]] = g["title"]
    return index, pages


def wiki_order(index, cat):
    """Every wiki page in reading order: (address, title key or "", English title, group key)."""
    out = []
    for g in index:
        out += [("/wiki/%s/" % s, "wk.%s.t" % s, T["wk.%s.t" % s], g["key"]) for s in g["pages"]]
        if g.get("catalog"):
            out += [("/wiki/built-in/%s/" % p["id"], "", p["name"], g["key"]) for p in cat if p.get("wiki")]
    return out


def wlink(e, cur="", tag=""):
    url, key, title = e[0], e[1], e[2]
    inner = '<%s%s>%s</%s>' % (tag or "span", ' data-i18n="%s"' % key if key else "", esc(title), tag or "span") if (tag or key) else esc(title)
    if not tag and key:
        return '<a href="%s"%s data-i18n="%s">%s</a>' % (url, ' aria-current="page"' if url == cur else "", key, esc(title))
    return '<a href="%s"%s>%s</a>' % (url, ' aria-current="page"' if url == cur else "", inner)


SEARCH = ('<div class="wsearch"><input type="search" id="wq" placeholder="%s" data-i18n-ph="ws.ph" aria-label="%s" data-i18n-aria="ws.ph" autocomplete="off" spellcheck="false">'
          '<kbd>Ctrl K</kbd><div class="wres" id="wres" hidden></div></div>')


def wiki_nav(index, cat, cur):
    out = ['<nav class="wnav" id="wnav" aria-label="Wiki">', SEARCH % (T["ws.ph"], T["ws.ph"])]
    for g in index:
        links = [wlink(("/wiki/%s/" % s, "wk.%s.t" % s, T["wk.%s.t" % s]), cur) for s in g["pages"]]
        inside = any(cur == "/wiki/%s/" % s for s in g["pages"] + g.get("hidden", []))
        if g.get("catalog"):
            for ty, cs in CATS:
                mine = [p for p in cat if p["type"] == ty and p.get("wiki")]
                here = any(cur == "/wiki/built-in/%s/" % p["id"] for p in mine)
                inside = inside or here
                links.append('<details class="sub"%s><summary><span data-i18n="cat.g.%s">%s</span> <small>%d</small></summary>%s</details>' % (
                    " open" if here else "", ty, T["cat.g." + ty], len(mine),
                    "".join(wlink(("/wiki/built-in/%s/" % p["id"], "", p["name"]), cur) for p in mine)))
        links += ['<a href="%s" data-i18n="%s">%s</a>' % (h, k, en) for h, k, en in g.get("links", [])]
        out.append('<details%s><summary data-i18n="%s">%s</summary>%s</details>' % (" open" if inside or not cur else "", g["key"], esc(g["title"]), "".join(links)))
    out.append("</nav>")
    return "".join(out)


def enonly(url):
    """The "English only" note, on pages some language has no translated body for (html.wt hides it for the others)."""
    return "" if len(BODY_LANGS.get(url, ())) == len(LANGS) - 1 else '<p class="enonly" data-i18n="wiki.enOnly">%s</p>' % T["wiki.enOnly"]


def wiki_shell(index, cat, order, url, head, prose, toc):
    n = [e[0] for e in order].index(url) if url in [e[0] for e in order] else -1
    prev = '<a href="%s"><span data-i18n="wiki.prev">%s</span>%s</a>' % (order[n - 1][0], T["wiki.prev"], wlink(order[n - 1], tag="b")[len('<a href="%s">' % order[n - 1][0]):-4]) if n > 0 else "<span></span>"
    nxt = '<a href="%s" style="text-align:right"><span data-i18n="wiki.next">%s</span>%s</a>' % (order[n + 1][0], T["wiki.next"], wlink(order[n + 1], tag="b")[len('<a href="%s">' % order[n + 1][0]):-4]) if 0 <= n < len(order) - 1 else "<span></span>"
    tocbox = ""
    if len(toc) > 1:
        tocbox = '<nav class="toc" aria-label="On this page"><h2 data-i18n="wiki.toc">%s</h2><ol>%s</ol></nav>' % (
            T["wiki.toc"], "".join('<li><a href="#%s"%s>%s</a></li>' % (i, ' data-i18n="%s"' % k if k else "", t) for i, k, t in toc))
    return ('<div class="wrap wiki">%s<article><a class="wjump" href="#wnav" data-i18n="wiki.all">%s</a>%s'
            '%s<div class="wbody%s">%s<div class="prose" id="wprose">%s</div></div><nav class="wpn">%s%s</nav></article></div>'
            % (wiki_nav(index, cat, url), T["wiki.all"], head, enonly(url), " hastoc" if tocbox else "", tocbox, prose, prev, nxt))


def render_wiki_page(index, cat, order, pages, s):
    p = pages[s]
    short = ' <span class="tag" data-i18n="wiki.short">%s</span>' % T["wiki.short"] if p["meta"].get("status") == "short" else ""
    head = '<h1><span data-i18n="wk.%s.t">%s</span>%s</h1><p class="sub" data-i18n="wk.%s.s">%s</p>' % (s, esc(T["wk.%s.t" % s]), short, s, esc(T["wk.%s.s" % s]))
    prose = p["prose"]
    toc = [(i, "", t) for i, t in re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', prose)]
    p["heads"] = [re.sub(r"<[^>]+>", "", t) for _, _, t in toc]
    return wiki_shell(index, cat, order, "/wiki/%s/" % s, head, prose, toc)


def plugin_prose(p, cat, w):
    """The body of a plugin's page from its texts w (English, or a translation of them); returns (html, toc)."""
    parts = []
    if p["dim"]:
        src = "/assets/img/plugins/%s%s.webp" % (p["id"], "@2x" if len(p["dim"]) > 2 else "")
        parts.append('<figure><a class="shot" href="%s">%s</a></figure>' % (src, plugin_img(p, lazy=False)))
    parts.append("<p>%s</p>" % inline(w["intro"]))
    toc = []
    if w.get("controls"):
        toc.append(("controls", "wiki.controls", T["wiki.controls"]))
        parts.append('<h2 id="controls" data-i18n="wiki.controls">%s</h2><div class="tablewrap"><table><thead><tr><th data-i18n="wiki.control">%s</th><th data-i18n="wiki.does">%s</th></tr></thead><tbody>%s</tbody></table></div>'
                     % (T["wiki.controls"], T["wiki.control"], T["wiki.does"], "".join("<tr><td>%s</td><td>%s</td></tr>" % (inline(a), inline(b)) for a, b in w["controls"])))
    if w.get("tips"):
        toc.append(("tips", "wiki.tips", T["wiki.tips"]))
        parts.append('<h2 id="tips" data-i18n="wiki.tips">%s</h2><ul>%s</ul>' % (T["wiki.tips"], "".join("<li>%s</li>" % inline(t) for t in w["tips"])))
    toc.append(("related", "wiki.related", T["wiki.related"]))
    parts.append('<h2 id="related" data-i18n="wiki.related">%s</h2><ul><li><a href="/wiki/built-in/" data-i18n="wiki.relOverview">%s</a></li><li><a href="/instruments-and-effects/#g-%s">%s</a></li></ul>'
                 % (T["wiki.related"], T["wiki.relOverview"], p["cat"], tr("wiki.relCat", len(cat))))
    return "".join(parts), toc


def render_plugin_page(index, cat, order, p):
    head = '<h1>%s</h1><p class="sub" data-i18n="c.%s">%s</p><div class="tags">%s</div>' % (esc(p["name"]), p["id"], T["c." + p["id"]], plugin_tags(p))
    prose, toc = plugin_prose(p, cat, p["wiki"])
    return wiki_shell(index, cat, order, "/wiki/built-in/%s/" % p["id"], head, prose, toc)


# ---------------------------------------------------------------- translated wiki bodies
BODY_LANGS = {}     # page address -> the languages that have its body


def digest(text):
    return hashlib.sha1(text.strip().encode("utf-8")).hexdigest()[:10]


def plugin_source(w):
    """What a plugin page's translation was made from (its digest goes into the translation as "source")."""
    return json.dumps([w["intro"], w.get("controls", []), w.get("tips", [])], ensure_ascii=False)


def wiki_bodies(index, pages, cat):
    """The wiki bodies in other languages, shown in place of the English body by assets/site.js.

    Articles: _src/wiki/<lang>/<page>.md (front matter "source": digest of the English body it was made from).
    Plugin pages: _src/catalog-wiki/<lang>.json {plugin id: {"source", "intro", "controls", "tips"}}.
    A body whose source no longer matches the English is left out (scripts/check-site-i18n.py reports it).
    Written to i18n/wiki/<lang>/<page>.html and built-in/<plugin>.html, plus heads.json for the search."""
    files = {}
    for lang, _ in LANGS[1:]:
        heads = {}
        for s, p in pages.items():
            f = SRC / "wiki" / lang / (s + ".md")
            if not f.exists():
                continue
            meta, body = front(f)
            if meta.get("source") != digest(p["body"]):
                continue
            ids = re.findall(r'<h[23] id="([^"]+)"', p["prose"])
            prose = markdown(body, iter(ids))
            if re.findall(r'<h[23] id="([^"]*)"', prose) != ids:
                sys.exit("build: %s has other headings than the English page" % f)
            url = "/wiki/%s/" % s
            files["i18n/wiki/%s/%s.html" % (lang, s)] = prose + "\n"
            heads[url] = [[i, re.sub(r"<[^>]+>", "", t)] for i, t in re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', prose)]
            BODY_LANGS.setdefault(url, []).append(lang)
        f = SRC / "catalog-wiki" / (lang + ".json")
        mine = json.loads(f.read_text(encoding="utf-8")) if f.exists() else {}
        for p in cat:
            w = mine.get(p["id"])
            if not (w and p.get("wiki")) or w.get("source") != digest(plugin_source(p["wiki"])):
                continue
            files["i18n/wiki/%s/built-in/%s.html" % (lang, p["id"])] = plugin_prose(p, cat, w)[0] + "\n"
            BODY_LANGS.setdefault("/wiki/built-in/%s/" % p["id"], []).append(lang)
        if heads:
            files["i18n/wiki/%s/heads.json" % lang] = json.dumps(heads, ensure_ascii=False, separators=(",", ":")) + "\n"
    return files


def render_wiki_index(index, cat, pages):
    cards = []
    for g in index:
        rows = ['<li><a href="/wiki/%s/" data-i18n="wk.%s.t">%s</a><span data-i18n="wk.%s.s">%s</span></li>'
                % (s, s, esc(T["wk.%s.t" % s]), s, esc(T["wk.%s.s" % s])) for s in g["pages"]]
        count = len(g["pages"])
        if g.get("catalog"):
            withpage = [p for p in cat if p.get("wiki")]
            count += len(withpage)
            for ty, _ in CATS:
                rows.append('<li><a href="/instruments-and-effects/#t-%s"><span data-i18n="cat.g.%s">%s</span> (%d)</a></li>' % (ty, ty, T["cat.g." + ty], sum(1 for p in withpage if p["type"] == ty)))
            rows.append('<li><a href="/instruments-and-effects/">%s</a></li>' % tr("wiki.catLink", len(cat)))
        rows += ['<li><a href="%s" data-i18n="%s">%s</a></li>' % (h, k, en) for h, k, en in g.get("links", [])]
        cards.append('<section class="wcard"><h2 data-i18n="%s">%s</h2><p class="wn">%s</p><p data-i18n="%s.d">%s</p><ul class="wlist">%s</ul></section>'
                     % (g["key"], esc(g["title"]), tr("wiki.pages", count, count=count), g["key"], T[g["key"] + ".d"], "".join(rows)))
    return ('<div class="phead"><div class="wrap"><h1 data-i18n="wiki.h1">%s</h1><p data-i18n="wiki.sub">%s</p>%s</div></div>'
            '<div class="wrap wikiidx">'
            '%s<div class="wcards">%s</div></div>' % (T["wiki.h1"], T["wiki.sub"], SEARCH % (T["ws.ph"], T["ws.ph"]), enonly("/wiki/"), "".join(cards)))


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
    for v, k in re.findall(r'placeholder="([^"]*)" data-i18n-ph="([^"]+)"', page):
        put(k, v)
    m = re.search(r'<h1 data-i18n-words="([^"]+)">(.*?)</h1>', page, re.S)
    if m:
        put(m.group(1), re.sub(r'\s*<span class="grad">(.*?)</span>', r"|\1", m.group(2)))


def main():
    check = "--check" in sys.argv
    rels = json.loads((SRC / "updates.json").read_text(encoding="utf-8"))
    latest = rels[0]
    index, pages = wiki_pages()
    cat = catalog()
    order = wiki_order(index, cat)
    body_files = wiki_bodies(index, pages, cat)
    full = [l for l, _ in LANGS[1:] if all(l in BODY_LANGS.get(e[0], ()) for e in order)]
    if full:
        BODY_LANGS["/wiki/"] = full                # the index page: languages with every body
    collect((SRC / "pages/why.html").read_text(encoding="utf-8"), T, "why")      # Home's row reuses the Why page's headings
    sub = {"VER_UP": latest["version"].upper(), "VER_LINE": "v" + latest["version"], "LATEST": render_latest(latest),
           "IC_WIN": IC["win"], "IC_MAC": IC["mac"], "IC_LINUX": IC["linux"],
           "OS_BTNS": "".join('<a class="btn btn-ghost" data-os="%s" data-dl="%s.main" href="%s/releases/latest">%s<span>%s</span><small data-i18n="%s">%s</small><em data-yours></em><i class="sh" aria-hidden="true"></i></a>'
                              % (o, o, REPO, IC[o], name, k, T[k]) for o, name, k in (("win", "Windows", "dl.fInstaller"), ("mac", "macOS", "dl.fUniversal"), ("linux", "Linux", "dl.fAppImage"))),
           "WHYROW": render_whyrow(cat), "CAT_SUB": tr("cat.sub", len(cat), latest["version"].split("-")[0], tag="p", count=len(cat)),
           "WHY_FL_P1": T["why.fl.p1"], "WHY_FL_X2": T["why.fl.x2"], "KOFI": KOFI}

    def page(name):
        body = (SRC / "pages" / (name + ".html")).read_text(encoding="utf-8")
        body = re.sub(r"\{\{(\w+)\}\}", lambda m: sub[m.group(1)], body)
        return fill_args(body)

    bodies = [   # path, nav item, title key, description key, body, scripts, extra dictionary
        ("/", "home", "m.home.t", "m.home.d", page("home"), (), "latest"),
        ("/features/", "features", "fe.h1", "m.features.d", page("features"), ("/assets/features.js",), ""),
        ("/download/", "download", "dlp.h1", "m.download.d", page("download"), (), ""),
        ("/why/", "why", "why.h1", "m.why.d", page("why"), (), ""),
        ("/fixes/", "fixes", "fix.h1", "m.fixes.d", page("fixes"), (), ""),
        ("/instruments-and-effects/", "cat", "cat.h1", "m.cat.d", render_catalog(cat, latest["version"].split("-")[0]), ("/assets/catalog.js",), "catalog"),
        ("/updates/", "updates", "up.h1", "m.updates.d", render_updates(rels), (), "updates"),
        ("/wiki/", "wiki", "wiki.h1", "m.wiki.d", render_wiki_index(index, cat, pages), ("/assets/wiki.js",), ""),
        ("/faq/", "faq", "faq.h1", "m.faq.d", render_faq(), (), ""),
    ] + [("/wiki/%s/" % s, "wiki", "wk.%s.t" % s, "wk.%s.s" % s, render_wiki_page(index, cat, order, pages, s), ("/assets/wiki.js",), "")
         for g in index for s in g["pages"] + g.get("hidden", [])
    ] + [("/wiki/built-in/%s/" % p["id"], "wiki", ("", p["name"]), "c." + p["id"], render_plugin_page(index, cat, order, p), ("/assets/wiki.js",), "catalog")
         for p in cat if p.get("wiki")]
    # the wiki search: address, title key, English title, group key, headings
    heads = {"/wiki/%s/" % s: pages[s].get("heads", []) for s in pages}
    search = [[u, k, t, g, heads.get(u, [])] for u, k, t, g in order]

    en = dict(T)
    for b in bodies:
        collect(b[4], en, b[0])
    T.update(en)
    collect(frame("/", "", "m.home.t", "m.home.d", "", "0", home=True), en, "frame")
    ui = {k: v for k, v in en.items() if not k.startswith(("u.", "c."))}
    upd = {k: v for k, v in en.items() if k.startswith("u.")}
    cen = {k: v for k, v in en.items() if k.startswith("c.")}
    dump = lambda d: json.dumps(d, ensure_ascii=False, indent=0) + "\n"
    files = dict(body_files)
    files.update({"i18n/en.json": dump({"ui": ui, "featured": {}, "insp": {}}), "i18n/updates/en.json": dump(upd), "i18n/catalog/en.json": dump(cen),
             "wiki/search.json": json.dumps({"g": {g["key"]: g["title"] for g in index}, "p": search}, ensure_ascii=False, separators=(",", ":")) + "\n"})
    lk = vkey(latest) + "."
    for f in sorted((OUT / "i18n/updates").glob("*.json")) if (OUT / "i18n/updates").is_dir() else []:
        d = upd if f.stem == "en" else json.loads(f.read_text(encoding="utf-8"))
        files["i18n/latest/%s.json" % f.stem] = dump({k: v for k, v in d.items() if k.startswith(lk)})
    files["i18n/latest/en.json"] = dump({k: v for k, v in upd.items() if k.startswith(lk)})
    # one version stamp for css/js/json addresses: changes when any of them does
    stamp = hashlib.sha1()
    for name in ("assets/site.css", "assets/site.js", "assets/features.js", "assets/catalog.js", "assets/wiki.js", "assets/motion.js"):
        stamp.update((OUT / name).read_bytes())
    for name in sorted(files):
        stamp.update(files[name].encode())
    for f in sorted((OUT / "i18n").glob("*.json")) + sorted((OUT / "i18n/catalog").glob("*.json")):
        if f.name != "en.json":
            stamp.update(f.read_bytes())
    version = stamp.hexdigest()[:8]
    for path, cur, tk, dk, body, scripts, part in bodies:
        files[(path.strip("/") + "/index.html").lstrip("/")] = frame(path, cur, tk, dk, body, version, scripts, part, home=path == "/",
                                                                     wt=" ".join(BODY_LANGS.get(path, ())) if path.startswith("/wiki/") else "")

    files = {name: sized(text) if name.endswith(".html") else text for name, text in files.items()}
    stale = [str(f.relative_to(OUT)) for f in sorted((OUT / "i18n/wiki").glob("**/*.*")) if str(f.relative_to(OUT)) not in files]
    if not check:
        for name in stale:                         # bodies whose translation is gone or out of date
            (OUT / name).unlink()
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
    print("build: %d files, %d strings + %d update strings + %d catalogue strings, %s" % (len(files), len(ui), len(upd), len(cen), "up to date" if check else "%d written" % len(stale)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
