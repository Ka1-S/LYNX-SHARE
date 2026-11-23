/* lynx-share.com: a tutorial page (/learn/<slug>/), built by _src/build.py from _src/learn/. The page works without
   this script (English video, every caption track in the CC menu, chapter list as text). The script:
   - plays the narration of the reader's language when the video has one (figure data-voices), otherwise English;
   - turns on the captions in the reader's language when the narration is in another one;
   - lets the chapter buttons seek;
   - with a YouTube address (figure data-yt, {lang: video id}), shows a click-to-load YouTube player: nothing is
     requested from YouTube or Google before that click. */
(() => {
const fig = document.querySelector('.tvid'); if (!fig) return;
const video = fig.querySelector('video'), M = '/assets/media/learn/', v = fig.dataset.v;
const voices = (fig.dataset.voices || 'en').split(' ');
let voice = 'en', lang = 'en';
const started = () => started.done || video.currentTime > 0;

let want = 'en';
function swap(){                            // the reader's narration: done at the first Play, so nothing loads before
  if (want === voice) return false;
  voice = want;
  const s = video.querySelectorAll('source');
  if (s[0]) s[0].src = M + v + '-' + voice + '-1080.mp4';
  if (s[1]) s[1].src = M + v + '-' + voice + '-720.mp4';
  video.load();
  return true;
}
function apply(l){
  lang = l;
  want = voices.includes(l) ? l : 'en';
  if (!started()){
    video.poster = '/assets/img/learn/' + v + '-' + want + '.webp';
    fig.querySelectorAll('a[download]').forEach(a => { a.href = a.href.replace(/-(?:[a-z]{2}(?:-[A-Za-z]+)?)-(720|1080)\.mp4$/, '-' + want + '-$1.mp4'); });
  }
  for (const t of video.textTracks) t.mode = (t.language === l && l !== want) ? 'showing' : 'disabled';
}
video.addEventListener('play', () => { if (!started.done && swap()){ const p = video.play(); if (p && p.catch) p.catch(() => {}); } started.done = true; });
document.addEventListener('lynx:lang', e => apply(e.detail));
apply(document.documentElement.lang || 'en');

fig.parentElement.querySelectorAll('[data-seek]').forEach(b => b.addEventListener('click', () => {
  if (!started.done){ swap(); started.done = true; }   // (a seek before the first Play: the reader's narration first)
  video.currentTime = +b.dataset.seek; const p = video.play(); if (p && p.catch) p.catch(() => {});
}));

let yt = {}; try { yt = JSON.parse(fig.dataset.yt || '{}'); } catch (e) {}
const btn = fig.querySelector('.ytbtn');
if (btn) btn.addEventListener('click', () => {
  const id = yt[lang] || yt[voice] || yt.en || Object.values(yt)[0]; if (!id) return;
  const f = document.createElement('iframe');
  f.src = 'https://www.youtube-nocookie.com/embed/' + encodeURIComponent(id) + '?autoplay=1&rel=0&cc_load_policy=1&cc_lang_pref=' + encodeURIComponent(lang) + '&hl=' + encodeURIComponent(lang);
  f.title = 'YouTube'; f.allow = 'autoplay; encrypted-media; picture-in-picture; fullscreen'; f.allowFullscreen = true;
  f.referrerPolicy = 'strict-origin-when-cross-origin';
  video.pause(); fig.querySelector('.vbox').replaceChildren(f); btn.closest('.ytline').remove();
});
})();
