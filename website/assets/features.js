/* Features page: the step sequencer and the demo-song player. */
(() => {
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const t = (...a) => window.LYNX_T ? window.LYNX_T(...a) : a[0];

/* ---------- step sequencer (Web Audio) ---------- */
const seqEl = $('#seq');
let playing = false, stopSeq = () => {};
const audio = $('#audio');
if (seqEl){
  const rows = [
    {n:'Kick', c:'#e5383b', p:[0,4,8,12]}, {n:'Clap', c:'#f4a259', p:[4,12]}, {n:'Hat', c:'#e9c46a', p:[2,6,10,14]},
    {n:'Snare', c:'#c77dff', p:[15]}, {n:'Bass', c:'#4cc9f0', p:[0,3,6,10,11]}, {n:'Pluck', c:'#3ddc84', p:[2,7,9,14]}
  ];
  const grid = rows.map(r => Array.from({length:16}, (_, i) => r.p.includes(i)));
  rows.forEach((r, ri) => {
    const row = document.createElement('div'); row.className = 'row'; row.style.setProperty('--c', r.c);
    row.innerHTML = `<div class="nm">${r.n}</div>`;
    for (let s = 0; s < 16; s++){
      const b = document.createElement('button'); b.className = 'cell' + (grid[ri][s] ? ' on' : ''); b.dataset.step = s + 1;
      b.onclick = () => { grid[ri][s] = !grid[ri][s]; b.classList.toggle('on', grid[ri][s]); if (grid[ri][s]){ ensureCtx(); voice(ri, ctx.currentTime, s); } };
      row.appendChild(b);
    }
    seqEl.appendChild(row);
  });
  const cells = $$('#seq .row').map(r => [...r.querySelectorAll('.cell')]);
  const label = () => cells.forEach((rc, r) => rc.forEach(c => c.setAttribute('aria-label', t('aria.step', rows[r].n, c.dataset.step))));
  label(); document.addEventListener('lynx:lang', label);

  let ctx, master, noiseBuf, step = 0, nextT = 0, timer;
  function ensureCtx(){
    if (ctx){ if (ctx.state === 'suspended') ctx.resume(); return; }
    ctx = new (window.AudioContext || window.webkitAudioContext)();
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 4;
    master = ctx.createGain(); master.gain.value = .8; master.connect(comp); comp.connect(ctx.destination);
    noiseBuf = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate); const d = noiseBuf.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random()*2 - 1;
  }
  const bassNotes = [36,36,39,36,43,41,36,34,36,36,39,41,43,46,43,39], pluckNotes = [72,75,79,82,79,75,77,74,72,75,79,84,82,79,77,75];
  const hz = m => 440*Math.pow(2, (m - 69)/12);
  function env(g, t, a, peak, dec){ g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(peak, t + a); g.gain.exponentialRampToValueAtTime(0.0001, t + a + dec); }
  function noise(t, dec, type, freq, q, vol){ const s = ctx.createBufferSource(); s.buffer = noiseBuf; const f = ctx.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q; const g = ctx.createGain(); env(g, t, .001, vol, dec); s.connect(f); f.connect(g); g.connect(master); s.start(t); s.stop(t + dec + .05); }
  function voice(r, t, s){
    if (r === 0){ const o = ctx.createOscillator(), g = ctx.createGain(); o.frequency.setValueAtTime(160, t); o.frequency.exponentialRampToValueAtTime(42, t + .12); env(g, t, .002, 1, .38); o.connect(g); g.connect(master); o.start(t); o.stop(t + .45); }
    else if (r === 1){ [0, .011, .022].forEach(d => noise(t + d, .16, 'bandpass', 1400, 1.2, .55)); }
    else if (r === 2){ noise(t, .05, 'highpass', 8000, .7, .32); }
    else if (r === 3){ noise(t, .2, 'bandpass', 2200, .8, .6); const o = ctx.createOscillator(), g = ctx.createGain(); o.frequency.value = 190; env(g, t, .001, .4, .12); o.connect(g); g.connect(master); o.start(t); o.stop(t + .2); }
    else if (r === 4){ const f = ctx.createBiquadFilter(), g = ctx.createGain(); f.type = 'lowpass'; f.Q.value = 6; f.frequency.setValueAtTime(1600, t); f.frequency.exponentialRampToValueAtTime(140, t + .22);
      [0, -8, 8].forEach(dt => { const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(bassNotes[s]); o.detune.value = dt; o.connect(f); o.start(t); o.stop(t + .32); });
      env(g, t, .004, .28, .26); f.connect(g); g.connect(master); }
    else { const o = ctx.createOscillator(), o2 = ctx.createOscillator(), f = ctx.createBiquadFilter(), g = ctx.createGain(); o.type = 'square'; o2.type = 'sawtooth'; o.frequency.value = hz(pluckNotes[s]); o2.frequency.value = hz(pluckNotes[s])*1.005;
      f.type = 'lowpass'; f.frequency.setValueAtTime(5000, t); f.frequency.exponentialRampToValueAtTime(400, t + .25); env(g, t, .003, .12, .35); o.connect(f); o2.connect(f); f.connect(g); g.connect(master); o.start(t); o2.start(t); o.stop(t + .4); o2.stop(t + .4); }
  }
  function paint(s){ cells.forEach(rc => rc.forEach((c, i) => c.classList.toggle('now', i === s && playing))); }
  function schedule(){
    const bpm = +$('#bpm').value, sw = +$('#swing').value/100, sd = 60/bpm/4;
    while (nextT < ctx.currentTime + .12){
      const tt = nextT + (step % 2 ? sd*sw*.66 : 0);
      rows.forEach((_, r) => { if (grid[r][step]) voice(r, tt, step); });
      const s = step; setTimeout(() => paint(s), Math.max(0, (tt - ctx.currentTime)*1000));
      nextT += sd; step = (step + 1) % 16;
    }
  }
  const sp = $('#seqPlay'), playIc = sp.innerHTML, stopIc = '<svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>';
  sp.onclick = () => {
    ensureCtx(); playing = !playing; sp.innerHTML = playing ? stopIc : playIc;
    if (playing){ if (audio && !audio.paused) audio.pause(); step = 0; nextT = ctx.currentTime + .05; timer = setInterval(schedule, 25); schedule(); }
    else { clearInterval(timer); paint(-1); }
  };
  stopSeq = () => { if (playing) sp.click(); };
  $('#bpm').oninput = e => $('#bpmv').textContent = e.target.value;
  $('#rnd').onclick = () => grid.forEach((g, r) => g.forEach((_, i) => { const prob = [.3,.12,.5,.08,.3,.25][r]; g[i] = r === 0 ? i % 4 === 0 || Math.random() < .08 : Math.random() < prob; cells[r][i].classList.toggle('on', g[i]); }));
  $('#clr').onclick = () => grid.forEach((g, r) => g.forEach((_, i) => { g[i] = false; cells[r][i].classList.remove('on'); }));
}

/* ---------- demo song with waveform ---------- */
const wave = $('#wave');
if (wave && audio){
  const wc = wave.querySelector('canvas'), wx = wc.getContext('2d'), ap = $('#aPlay'), at = $('#atime'), tour = $('#tour');
  const playIc = ap.innerHTML, stopIc = '<svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>';
  let peaks = [];
  fetch('/assets/media/peaks.json?v=0.1.27').then(r => r.json()).then(p => { peaks = p; draw(); }).catch(() => {});
  function draw(){
    const d = Math.min(devicePixelRatio || 1, 2), w = wave.clientWidth, h = wave.clientHeight; wc.width = w*d; wc.height = h*d; wx.setTransform(d, 0, 0, d, 0, 0);
    wx.clearRect(0, 0, w, h); if (!peaks.length) return;
    const n = Math.floor(w/3), pos = audio.duration ? audio.currentTime/audio.duration : 0;
    for (let i = 0; i < n; i++){
      const bh = Math.max(2, peaks[Math.floor(i/n*peaks.length)]*h*.92);
      wx.fillStyle = i/n < pos ? '#e5383b' : '#3a3d4a';
      wx.fillRect(i*3, (h - bh)/2, 2, bh);
    }
  }
  addEventListener('resize', draw);
  const fmt = s => Math.floor(s/60) + ':' + String(Math.floor(s % 60)).padStart(2, '0');
  function tick(){ draw(); at.textContent = fmt(audio.currentTime) + ' / ' + fmt(audio.duration || 46); if (!audio.paused) requestAnimationFrame(tick); }
  ap.onclick = () => { if (audio.paused){ stopSeq(); if (tour) tour.pause(); audio.play(); } else audio.pause(); };
  audio.onplay = () => { ap.innerHTML = stopIc; tick(); };
  audio.onpause = audio.onended = () => { ap.innerHTML = playIc; tick(); };
  wave.onclick = e => { const r = wave.getBoundingClientRect(), f = (e.clientX - r.left)/r.width;
    const go = () => { audio.currentTime = f*audio.duration; tick(); };
    if (audio.readyState < 1){ audio.preload = 'auto'; audio.addEventListener('loadedmetadata', go, {once:true}); audio.load(); } else go(); };
  if (tour) tour.addEventListener('play', () => { stopSeq(); if (!audio.paused) audio.pause(); });
}
})();
