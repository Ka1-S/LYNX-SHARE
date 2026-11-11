/* Wiki search: titles and headings from /wiki/search.json (built by _src/build.py), nothing leaves the browser.
   Ctrl+K (or /) focuses the box, arrows move, Enter opens. Without this script the box is hidden. */
(() => {
const box = document.getElementById('wq'), res = document.getElementById('wres');
if (!box || !res) return;
const t = window.LYNX_T || (k => k);
let index = null, groups = {}, sel = 0, hits = [];
const group = k => { const v = t(k); return v === k ? (groups[k] || '') : v; };
const V = window.LYNX_V || '';
const loadIndex = () => index ? Promise.resolve(index) : fetch('/wiki/search.json?v=' + V).then(r => r.json()).then(d => { groups = d.g; return index = d.p; });
// the headings of the translated bodies, by page: {address: [[id, text], ...]} (languages without them search the English ones)
let local = {}, localFor = 'en';
const loadLocal = () => {
  const l = document.documentElement.lang || 'en';
  if (l === localFor) return Promise.resolve();
  localFor = l; local = {};
  return l === 'en' ? Promise.resolve() : fetch('/i18n/wiki/' + l + '/heads.json?v=' + V).then(r => r.ok ? r.json() : {}).then(d => { if (localFor === l) local = d; }).catch(() => {});
};
const load = () => Promise.all([loadIndex(), loadLocal()]);
const anchor = s => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
const norm = s => String(s).toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]));
const title = e => { const v = e[1] ? t(e[1]) : e[2]; return v === e[1] ? e[2] : v; };
function search(q){
  const words = norm(q).split(/\s+/).filter(Boolean);
  if (!words.length) return [];
  const out = [];
  for (const e of index){
    const ti = norm(title(e) + ' ' + e[2]);
    let score = 0, head = null;
    for (const w of words){
      if (ti.startsWith(w)) score += 6; else if (ti.includes(w)) score += 4;
      else {
        const h = (local[e[0]] || []).find(x => norm(x[1]).includes(w)) || (e[4].map(x => [anchor(x), x]).find(x => norm(x[1]).includes(w)));
        if (h){ score += 1; head = head || h; } else { score = -1; break; }
      }
    }
    if (score > 0) out.push([score, e, head]);
  }
  return out.sort((a, b) => b[0] - a[0]).slice(0, 12);
}
function draw(){
  if (!box.value.trim()){ res.hidden = true; return; }
  res.hidden = false;
  res.innerHTML = hits.length ? hits.map(([, e, head], i) => `<a href="${e[0]}${head ? '#' + head[0] : ''}" class="${i === sel ? 'on' : ''}">${esc(title(e))}<small>${esc(head ? head[1] : group(e[3]))}</small></a>`).join('')
                              : `<p>${esc(t('ws.none'))}</p>`;
}
box.addEventListener('input', () => load().then(() => { hits = search(box.value); sel = 0; draw(); }));
box.addEventListener('focus', () => { load(); if (box.value.trim()) draw(); });
box.addEventListener('keydown', e => {
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp'){ e.preventDefault(); if (hits.length){ sel = (sel + (e.key === 'ArrowDown' ? 1 : hits.length - 1)) % hits.length; draw(); } }
  else if (e.key === 'Enter' && hits[sel]){ e.preventDefault(); const a = res.querySelectorAll('a')[sel]; if (a) location.href = a.href; }
  else if (e.key === 'Escape'){ res.hidden = true; box.blur(); }
});
document.addEventListener('click', e => { if (!e.target.closest('.wsearch')) res.hidden = true; });
// /wiki/?q=reverb opens with the results shown
try { const q = new URLSearchParams(location.search).get('q'); if (q){ box.value = q; load().then(() => { hits = search(q); draw(); }); } } catch (x) {}
document.addEventListener('keydown', e => {
  const typing = /^(INPUT|TEXTAREA|SELECT)$/.test((e.target || {}).tagName || '');
  if ((e.key === 'k' && (e.ctrlKey || e.metaKey)) || (e.key === '/' && !typing)){ e.preventDefault(); box.scrollIntoView({block: 'nearest'}); box.focus(); box.select(); }
});
})();
