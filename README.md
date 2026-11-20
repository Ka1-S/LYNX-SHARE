Pictures linked from GitHub issue and discussion replies (LYNX SHARE releases). Not part of the program.

`site/learn/`: the tutorial videos of lynx-share.com/learn (H.264, 1080p and 720p, EN and pt-BR narration). Not in main
so cloning stays small: `scripts/site-media.sh` (run by `scripts/deploy-site.sh`) puts them into
`website/assets/media/learn/` and checks them against `website/_src/learn/media.json`.
