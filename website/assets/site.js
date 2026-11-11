/* lynx-share.com: shared script (language, download links, dates, lightbox). Pages read fine without it.
   LYNX_LANGS / LYNX_LANG / LYNX_DICT / LYNX_V come from the inline script in <head>; LYNX_EN holds the English of the
   few strings only this script writes. Every other English string is in the page itself (and in i18n/en.json, which
   _src/build.py writes from the pages). Other languages: i18n/<id>.json {"ui","featured","insp"}, plus
   i18n/<part>/<id>.json for pages that name a part in <html data-dict> (the Updates page), plus the body of a wiki
   page, i18n/wiki/<id>/<page>.html, for the languages <html data-wt> lists. */
(() => {
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const html = document.documentElement;
const LANGS = window.LYNX_LANGS || [['en','English']], EN = window.LYNX_EN || {}, V = window.LYNX_V || '';
const SHORT = {'zh-Hans':'简','zh-Hant':'繁','pt-BR':'PT'};
const OGL = {'en':'en_US','zh-Hans':'zh_CN','zh-Hant':'zh_TW','nl':'nl_NL','fr':'fr_FR','de':'de_DE','id':'id_ID','it':'it_IT','ja':'ja_JP','ko':'ko_KR','pl':'pl_PL','pt-BR':'pt_BR','ru':'ru_RU','es':'es_ES','tr':'tr_TR'};
const PART = html.dataset.dict || '';
const BODIES = (html.dataset.wt || '').split(' '), prose = $('#wprose'), proseEn = prose ? prose.innerHTML : '';
const bodyUrl = l => '/i18n/wiki/' + l + location.pathname.replace(/^\/wiki/, '').replace(/\/$/, '') + '.html?v=' + V;
let lang = 'en', D = {}, FEAT = {}, INSP = {}, pristine = true;

// t('dl.for', 'Linux'): the text in the current language (English when missing), {0} {1}... replaced by the arguments
const t = (k, ...a) => { let v = D[k] ?? EN[k] ?? k; a.forEach((x, i) => v = v.split('{' + i + '}').join(x)); return v; };
window.LYNX_T = t;

const cache = window.LYNX_DICT || {};
function fetchDict(l){
  const get = u => fetch(u).then(r => r.ok ? r.json() : Promise.reject(r.status));
  const body = prose && BODIES.includes(l) ? fetch(bodyUrl(l)).then(r => r.ok ? r.text() : '').catch(() => '') : '';
  return Promise.all([get('/i18n/' + l + '.json?v=' + V), PART ? get('/i18n/' + PART + '/' + l + '.json?v=' + V) : {}, body])
    .then(([a, b, c]) => ({ui: Object.assign({}, a.ui, b), featured: a.featured || {}, insp: a.insp || {}, body: c}));
}
function loadLang(l){
  if (l === 'en' && pristine) return Promise.resolve(null);   // the page is already in English
  if (window.LYNX_FONT) window.LYNX_FONT(l);
  cache[l] = cache[l] || fetchDict(l);
  return cache[l].catch(e => { delete cache[l]; throw e; });
}
let seq = 0;
function setLang(l){
  const s = ++seq;
  return loadLang(l).then(d => { if (s === seq) applyLang(l, d); }, () => { if (s === seq) applyLang('en', null); })
    .finally(() => html.classList.remove('i18n-wait'));
}
function pageUrl(l){ return 'https://lynx-share.com' + location.pathname + (l === 'en' ? '' : '?lang=' + (l === 'pt-BR' ? 'pt' : l)); }
function applyLang(l, d){
  lang = l; html.lang = l; html.dir = 'ltr';
  if (d){
    pristine = false; D = d.ui || {}; FEAT = d.featured || {}; INSP = d.insp || {};
    if (prose){   // the wiki body in this language, or the English one (with the "English only" note) when there is none
      prose.innerHTML = d.body || proseEn; html.classList.toggle('wt', !!d.body);
      $$('.toc a:not([data-i18n])').forEach(a => { const h = document.getElementById(a.hash.slice(1)); if (h) a.innerHTML = h.innerHTML; });
    }
    // a key the dictionary lacks keeps the page's own English (scripts/check-site-i18n.py reports it)
    $$('[data-i18n]').forEach(e => {
      if (e._en === undefined) e._en = e.innerHTML;
      const k = e.dataset.i18n, a = e.dataset.args ? JSON.parse(e.dataset.args) : [];
      e.innerHTML = k in D ? t(k, ...a) : e._en;
    });
    $$('[data-i18n-alt]').forEach(e => { if (e._alt === undefined) e._alt = e.alt; e.alt = D[e.dataset.i18nAlt] || e._alt; });
    $$('[data-i18n-ph]').forEach(e => { if (e._ph === undefined) e._ph = e.placeholder; e.placeholder = D[e.dataset.i18nPh] || e._ph; });
    $$('[data-i18n-aria]').forEach(e => { if (e._aria === undefined) e._aria = e.getAttribute('aria-label'); e.setAttribute('aria-label', D[e.dataset.i18nAria] || e._aria); });
    $$('[data-feat]').forEach(e => { if (FEAT[e.dataset.feat]) e.textContent = FEAT[e.dataset.feat]; else if (e.dataset.en) e.textContent = e.dataset.en; });
    $$('[data-insp]').forEach(e => { if (INSP[e.dataset.insp]) e.textContent = INSP[e.dataset.insp]; else if (e.dataset.en) e.textContent = e.dataset.en; });
    const h1 = $('[data-i18n-words]');
    if (h1 && D[h1.dataset.i18nWords]){ const [a, b = ''] = t(h1.dataset.i18nWords).split('|'); h1.innerHTML = a + (b ? ' <span class="grad">' + b + '</span>' : ''); }
    if (D[html.dataset.title]) document.title = D[html.dataset.title] + ('home' in html.dataset ? '' : ' · LYNX SHARE');
    const md = $('meta[name=description]'); if (md && D[html.dataset.desc]) md.content = D[html.dataset.desc];
    const ot = $('meta[property="og:title"]'); if (ot) ot.content = document.title;
    const od = $('meta[property="og:description"]'); if (od && md) od.content = md.content;
  }
  const can = $('link[rel=canonical]'); if (can) can.href = pageUrl(l);
  const ou = $('meta[property="og:url"]'); if (ou) ou.content = pageUrl(l);
  const ol = $('meta[property="og:locale"]'); if (ol) ol.content = OGL[l] || 'en_US';
  if (sel){ sel.value = l; $('#langCode').textContent = SHORT[l] || l.toUpperCase(); }
  renderDates(); renderVer(); renderDownload();
  document.dispatchEvent(new CustomEvent('lynx:lang', {detail: lang}));
}

const sel = $('#lang');
if (sel){
  sel.innerHTML = LANGS.map(([id, name]) => `<option value="${id}" lang="${id}">${name}</option>`).join('');
  sel.value = window.LYNX_LANG || 'en';
  sel.onchange = () => {
    const l = sel.value;
    try { localStorage.setItem('lynx-lang', l); } catch (e) {}
    // a ?lang= in the address wins over the saved choice: keep it in step so a reload shows the same language
    try { const u = new URL(location.href); if (u.searchParams.has('lang')){ if (l === 'en') u.searchParams.delete('lang'); else u.searchParams.set('lang', l === 'pt-BR' ? 'pt' : l); history.replaceState(null, '', u); } } catch (e) {}
    setLang(l);
  };
}

/* dates: <time datetime="2026-11-09"> in the reader's language */
function renderDates(){
  $$('time[datetime]').forEach(e => { try { e.textContent = new Date(e.dateTime + 'T12:00:00Z').toLocaleDateString(lang, {year:'numeric', month:'long', day:'numeric'}); } catch (x) {} });
}

/* download: the visitor's OS gets the accent; links go straight to the newest matching release file */
const RELEASES = 'https://github.com/Ka1-S/LYNX-SHARE/releases/latest';
const DL = {   // main = primary package, alt = second format; tests run on asset names, newest release with a match wins
  win:   {main: [/setup.*\.exe$/i, /\.exe$/i], alt: [/windows.*\.zip$/i]},
  mac:   {main: [/macos.*\.dmg$/i, /\.dmg$/i]},
  linux: {main: [/\.AppImage$/i], alt: [/linux.*\.tar\.gz$/i]}
};
const dlUrl = {};
function detectOS(){
  let q = null; try { q = new URLSearchParams(location.search).get('os'); } catch (e) {}   // ?os=win|mac|linux|any (testing)
  if (q) return DL[q] ? q : '';
  const uad = navigator.userAgentData, ua = navigator.userAgent || '';
  const p = ((uad && uad.platform) || navigator.platform || '').toLowerCase();
  if ((uad && uad.mobile) || /android|iphone|ipad|ipod/i.test(ua) || /android|iphone|ipad|ipod/.test(p)) return '';
  if (/mac/.test(p) || /Macintosh|Mac OS X/.test(ua)) return navigator.maxTouchPoints > 1 ? '' : 'mac';   // touch "Mac" = iPadOS
  if (/win/.test(p) || /Windows/.test(ua)) return 'win';
  if (/cros/.test(p) || /CrOS/.test(ua)) return '';
  if (/linux|x11|bsd/.test(p) || /Linux|X11/.test(ua)) return 'linux';
  return '';
}
const userOS = detectOS();
function renderDownload(){
  $$('[data-dl]').forEach(a => { a.href = dlUrl[a.dataset.dl] || RELEASES; });
  $$('[data-os]').forEach(e => {
    const you = e.dataset.os === userOS;
    e.classList.toggle('you', you);
    if (e.classList.contains('btn')){ e.classList.toggle('btn-red', you); e.classList.toggle('btn-ghost', !you); }
    const em = e.querySelector('em[data-yours]'); if (em) em.textContent = you ? t('dl.yours') : '';
  });
  $$('.dl-link').forEach(a => { if (userOS && dlUrl[userOS + '.main']) a.href = dlUrl[userOS + '.main']; });
  const badge = $('#storeBadge');   // Microsoft's own localized badge
  if (badge) badge.src = 'https://get.microsoft.com/images/' + ({'en':'en-us','zh-Hans':'zh-cn','zh-Hant':'zh-tw','pt-BR':'pt-br'}[lang] || lang) + '%20light.svg';
}

/* latest release from GitHub (the page already names the version it was built for) */
let rel = null;
function renderVer(){
  if (!rel) return;
  let d = ''; try { d = rel.at ? ' · ' + new Date(rel.at).toLocaleDateString(lang) : ''; } catch (e) {}
  $$('.ver-line').forEach(e => e.textContent = 'v' + rel.v + d);
  $$('.ver-tag').forEach(e => e.textContent = rel.v.toUpperCase());
}
if ($('[data-dl]') || $('.ver-line')){
  fetch('https://api.github.com/repos/Ka1-S/LYNX-SHARE/releases?per_page=10').then(r => r.ok ? r.json() : null).then(rs => {
    if (!Array.isArray(rs)) return;
    rs = rs.filter(x => !x.draft); if (!rs.length) return;
    rel = {v: rs[0].tag_name.replace(/^v/, ''), at: rs[0].published_at};
    Object.keys(DL).forEach(os => Object.keys(DL[os]).forEach(k => {
      for (const re of DL[os][k]) for (const r of rs){
        const a = (r.assets || []).find(x => re.test(x.name));
        if (a){ dlUrl[os + '.' + k] = a.browser_download_url; return; }
      }
    }));
    renderVer(); renderDownload();
  }).catch(() => {});
}

/* screenshots open full size */
if ($('a.shot')){
  const lb = document.createElement('div'); lb.className = 'lightbox'; lb.innerHTML = '<img alt="">'; document.body.appendChild(lb);
  const im = lb.firstChild;
  document.addEventListener('click', e => {   // delegated: a translated wiki body brings its own links
    const a = e.target.closest ? e.target.closest('a.shot') : null; if (!a) return;
    e.preventDefault(); im.src = a.href; im.alt = (a.querySelector('img') || {}).alt || ''; lb.classList.add('open');
  });
  lb.onclick = () => lb.classList.remove('open');
  addEventListener('keydown', e => { if (e.key === 'Escape') lb.classList.remove('open'); });
}

setLang(window.LYNX_LANG || 'en');
})();
