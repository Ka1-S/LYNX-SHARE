/* lynx-share.com: motion. Loaded last on every page; every page reads the same without it.
   The inline script in <head> sets html.mo unless the visitor asked for reduced motion (or ?shot, or the browser has
   no IntersectionObserver). Entrances at the top of a page are plain CSS animations (site.css, "motion"). This script
   adds what CSS cannot: reveals further down (.rv is put only on what is still below the fold, so nothing a visitor
   can see is ever hidden and re-shown), numbers that count up once, the glass header, and tilt + glare on screenshots
   for mouse pointers. Only IntersectionObserver and pointer events: no scroll handlers. */
(() => {
const html = document.documentElement, $$ = (s, r) => [...(r || document).querySelectorAll(s)];
if (!html.classList.contains('mo')) return;

/* what reveals, in document order; grids reveal child by child (staggered), everything else as one block */
const GRIDS = '.shots,.whyrow,.frow,.cols,.media,.dlgrid,.policy';
const REVEAL = '.sec>.wrap:not(.split)>:not(' + GRIDS + ',script),.split>*,.shots>figure,.whyrow>*,.frow>*,.cols>div,.media>*,.dlos,.policy>div,' +
  '.flow>li,.rel>header,.rel>div,.wcard,.faq section,.ctype,.cgroup>h3,.pc';
const DUR = 450, STEP = 45, MAXSTEP = 7;

function start(){
  /* glass header once the page has scrolled: a marker at the very top leaves the viewport */
  const top = document.querySelector('.top');
  if (top){
    const mark = document.createElement('i');
    mark.style.cssText = 'position:absolute;top:0;left:0;width:1px;height:10px;pointer-events:none';
    document.body.prepend(mark);
    new IntersectionObserver(e => top.classList.toggle('scrolled', !e[0].isIntersecting)).observe(mark);
    html.classList.add('hd');
  }

  /* reveals */
  const done = el => { el.classList.remove('rv', 'rv-l', 'in'); el.style.removeProperty('--d'); };
  const io = new IntersectionObserver(es => {
    let n = 0;
    es.forEach(e => {
      const el = e.target;
      if (!e.isIntersecting && e.boundingClientRect.top > 0) return;      // still below
      io.unobserve(el);
      if (!e.isIntersecting) return done(el);                              // jumped past it: just show it
      const d = Math.min(n++, MAXSTEP) * STEP;
      el.style.setProperty('--d', d + 'ms'); el.classList.add('in');
      setTimeout(done, d + DUR + 40, el);
    });
  }, {rootMargin: '0px 0px -5% 0px'});
  const fold = innerHeight * .92;
  $$(REVEAL).forEach(el => {
    if (el.getBoundingClientRect().top < fold || el.parentElement.closest('.rv')) return;
    el.classList.add('rv'); if (el.matches('.rel>header')) el.classList.add('rv-l');
    io.observe(el);
  });
  /* for scripts that change what is shown (the catalogue filter): show everything inside root now */
  window.LYNX_MOTION = {flush: root => $$('.rv', root).forEach(el => { io.unobserve(el); done(el); })};

  /* numbers count up once: <b data-count="130">130</b>, or a sentence that holds the number */
  const count = el => {
    const to = el.dataset.count, re = new RegExp('(^|\\D)' + to + '(?!\\d)'), walk = document.createTreeWalker(el, 4);
    for (let n; (n = walk.nextNode());){
      const m = re.exec(n.data); if (!m) continue;
      const at = m.index + m[1].length, a = n.data.slice(0, at), b = n.data.slice(at + to.length);
      const lo = Math.max(Math.pow(10, to.length - 1), Math.round(to * .3)), t0 = performance.now();   // same digit count: no layout shift
      if (lo >= +to) return;
      const step = t => { const p = Math.min(1, (t - t0) / 900); n.data = a + Math.round(lo + (to - lo) * (1 - Math.pow(1 - p, 3))) + b; if (p < 1) requestAnimationFrame(step); };
      return requestAnimationFrame(step);
    }
  };
  const co = new IntersectionObserver(es => es.forEach(e => { if (e.isIntersecting){ co.unobserve(e.target); count(e.target); } }), {threshold: .6});
  $$('[data-count]').forEach(el => co.observe(el));

  /* the hero's glow rests while the hero is off screen */
  const hero = document.querySelector('.hero');
  if (hero) new IntersectionObserver(e => hero.classList.toggle('off', !e[0].isIntersecting)).observe(hero);

  /* wiki sidebar: a group's links slide in when it opens */
  document.addEventListener('toggle', e => {
    const d = e.target;
    if (d.open && d.closest && d.closest('.wnav')){ d.classList.remove('op'); void d.offsetWidth; d.classList.add('op'); }
  }, true);

  /* tilt + glare on the hero picture and the screenshots: mouse only, one frame at a time */
  if (matchMedia('(hover:hover) and (pointer:fine)').matches){
    let cur = null, ev = null, raf = 0;
    const apply = () => {
      raf = 0; if (!cur) return;
      const r = cur.getBoundingClientRect(), x = (ev.clientX - r.left) / r.width - .5, y = (ev.clientY - r.top) / r.height - .5, s = cur.style;
      s.setProperty('--ry', (x * 5).toFixed(2) + 'deg'); s.setProperty('--rx', (-y * 5).toFixed(2) + 'deg');
      s.setProperty('--gx', Math.round(x * r.width) + 'px'); s.setProperty('--gy', Math.round(y * r.height) + 'px');
    };
    document.addEventListener('pointermove', e => {
      let el = e.pointerType === 'mouse' && e.target.closest ? e.target.closest('.screen,.shot') : null;
      if (el && el.closest('.wiki,.lightbox')) el = null;                  // wiki articles stay calm
      if (el !== cur){
        if (cur){ cur.classList.remove('tilt'); cur.style.removeProperty('--rx'); cur.style.removeProperty('--ry'); }
        cur = el; if (cur) cur.classList.add('tilt');
      }
      if (cur){ ev = e; raf = raf || requestAnimationFrame(apply); }
    }, {passive: true});
  }

  /* smooth anchor scrolling, but not for the jump to an address's own #anchor while the page loads */
  const smooth = () => setTimeout(() => html.classList.add('sm'), 400);
  document.readyState === 'complete' ? smooth() : addEventListener('load', smooth);
}

/* another language: wait until its text is in (the page is hidden until then), so the layout measured is the final one */
if (html.classList.contains('i18n-wait')) document.addEventListener('lynx:lang', start, {once: true}); else start();
})();
