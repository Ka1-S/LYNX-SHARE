/* Instruments and effects: the category chips filter the groups. Without this script they are plain links to them. */
(() => {
const bar = document.getElementById('cfilter');
if (!bar) return;
const chips = [...bar.querySelectorAll('a[data-f]')];
const show = (f, jump) => {
  const [kind, v] = f.split(':');
  document.querySelectorAll('.cgroup').forEach(g => { g.hidden = !(f === 'all' || (kind === 't' && g.dataset.type === v) || (kind === 'c' && g.id === 'g-' + v)); });
  document.querySelectorAll('.ctype').forEach(h => { h.hidden = !(f === 'all' || (kind === 't' && h.dataset.type === v)); });
  chips.forEach(c => c.classList.toggle('on', c.dataset.f === f));
};
chips.forEach(c => c.addEventListener('click', e => {
  e.preventDefault();
  show(c.dataset.f);
  try { history.replaceState(null, '', c.dataset.f === 'all' ? location.pathname + location.search : c.getAttribute('href')); } catch (x) {}
}));
// an address with #g-eq or #t-effect opens filtered; #p-<plugin> shows everything and scrolls to the card
const h = location.hash.slice(1);
const hit = chips.find(c => c.getAttribute('href') === '#' + h && c.dataset.f !== 'all');
if (hit) show(hit.dataset.f);
})();
