# Round 168u — website in the app's 15 languages

LYNX-own work (website, no FL equivalent).

## Where I stopped
- Done: website/index.html language picker (native select, globe + code), lazy `website/i18n/<id>.json` for 14
  languages (pt-BR moved out of the page), hreflang + x-default for all 15, per-language title/meta/og/canonical,
  alt + aria texts, CJK font stacks (system first, Noto fetched on demand), Intl.Segmenter hero title for zh/ja,
  `?v=0.1.21.1` cache-buster, `scripts/check-site-i18n.py` (0 errors, 0 warnings). Screenshots of all 15 at 360 px
  and 1440 px: /mnt/e/lynx-work/site168u/shots/ (report.txt = measured heights / overflow list; remaining overflow
  entries are the featured-plugin carousel, which scrolls sideways on purpose).
- Paused (coordinator, round 168w): landing not started (waiter cancelled while queued on the merge lock).
- Next: `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/coord/land.sh site168u`, then
  `scripts/deploy-site.sh` from main, then check https://lynx-share.com/?lang=<id> for every id (title per language,
  i18n/<id>.json 200).
- Page weight: index.html 127,449 -> ~127,000 bytes (36.8 -> 36.6 KB gzip); a non-English visit adds one JSON
  (~22-30 KB, ~8-10 KB gzip).
