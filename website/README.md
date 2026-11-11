# lynx-share.com

The project website: a small static site. Plain HTML, CSS and JavaScript, no framework. Every page reads without
JavaScript; the script adds the language switch, direct download links and the small interactive parts.

| Page | Address | Source |
|---|---|---|
| Home | `/` | `_src/pages/home.html` |
| Features | `/features/` | `_src/pages/features.html` |
| Why LYNX | `/why/` | `_src/pages/why.html` (what only LYNX SHARE does, each with its limits) |
| What LYNX fixes | `/fixes/` | `_src/pages/fixes.html` (common complaints about DAWs and what the RELEASED version does about each; things only in the source go in its "Being built" list). Linked from Home and Why LYNX |
| Instruments and effects | `/instruments-and-effects/` | `_src/catalog.json`, `_src/catalog-img.json` |
| Download | `/download/` | `_src/pages/download.html` |
| Updates | `/updates/` | `_src/updates.json` |
| Wiki | `/wiki/`, `/wiki/<page>/` | `_src/wiki/*.md`, `_src/wiki/index.json` |
| Wiki, one page per built-in plugin | `/wiki/built-in/<id>/` | `_src/catalog.json` (the `wiki` part of each entry) |
| FAQ | `/faq/` | `_src/faq.md` |

`api/` (crash reports) is not generated and not touched by the build. `update/` (the AppImage update address) is never
in this tree: `scripts/deploy-site.sh` keeps the live one.

## Building

    python3 website/_src/build.py            # writes the pages, wiki/search.json and the English string files in i18n/
    python3 website/_src/build.py --check    # exit 1 if a written file is stale
    python3 website/_src/check-links.py      # every internal link, image and #anchor resolves
    python3 scripts/check-site-i18n.py       # every language has every string (also runs build --check)

Python 3 standard library only. **The built pages are committed**: edit the sources in `_src/`, never the
`index.html` files, then run the build and commit both. `_src/build.py` holds the shared header and footer, the
markdown subset the wiki uses (headings, lists, tables, code, links, one picture), and the page list.
`assets/site.css` and `assets/site.js` are shared by every page; `assets/features.js` (Features), `assets/catalog.js`
(the catalogue's filter) and `assets/wiki.js` (the wiki search over `wiki/search.json`, which the build writes) belong
to one page each. `assets/motion.js` is loaded last on every page: reveals on scroll, numbers that count up
(`data-count`), the glass header, tilt and glare on screenshots. The entrances at the top of a page and all hover
motion are CSS (`site.css`, "motion"). Motion is on only under `html.mo`, which the script in `<head>` sets unless the
visitor asked for reduced motion; a page hides something only when `motion.js` ran and the thing is below the fold, so
a failed script never leaves a blank page. Stills for checking a page: add `?shot` (no motion at all).
The server never serves `_src/` (`.htaccess`).

Look at the result with `python3 -m http.server -d website 8000` (links are root-absolute, so open it through a
server, not as files).

## Adding a release to the Updates page (release step)

1. Add ONE object at the top of `_src/updates.json`, written from the GitHub release notes in plain language:
   `version`, `tag`, `date` (UTC), `url`, `title` (what the release is about, not a slogan), `summary` (one or two
   sentences), then `new`, `better`, `fixed` and optionally `known`: lists of one- or two-sentence items. `**bold**`
   and `` `code` `` are allowed in items. `shots` (optional): `[{"src": "/assets/img/x.webp?v=<version>", "alt": "..."}]`,
   only pictures taken from that release's build.
2. `python3 website/_src/build.py`. The newest entry also fills the "Latest update" box on Home and the version
   shown on Home and Download.
3. Translate it: `python3 scripts/check-site-i18n.py --todo <id> todo.json` writes the English strings language
   `<id>` lacks; translate the values (keys and markup unchanged) and `--merge <id> todo.json`. Repeat for the 14
   languages, run the build again, then `scripts/check-site-i18n.py` must report 0 errors.
4. Refresh the screenshots in `assets/img/` if a window changed (they must come from the build people download),
   bump their `?v=` in the page sources, and deploy.
5. Built-in plugins: if the release added, removed or changed one, update `_src/catalog.json` and redraw the pictures
   (next section).

Editing the text of an existing item means its translations are stale: remove that key from the 14 language files
so the check reports it.

## The plugin catalogue and its pictures

`_src/catalog.json` has one entry per plugin built into the RELEASED version: `id`, `name` (as the app shows it),
`cat` (`synth`, `drums`, `sampler`; `eq`, `dynamics`, `reverb`, `delay`, `modulation`, `distortion`, `pitch`, `stereo`;
`controller`, `visual`, `routing`: the first three are instruments, the last three tools), `flp` (the FL Studio plugin
whose settings the engine opens from a `.flp`, or empty), `desc` (the card text, translated: key `c.<id>` in
`i18n/catalog/<lang>.json`), `img` (which render is its picture) and `wiki` (`intro`, `controls` rows, `tips`: the
plugin's wiki page, English only; `null` = no page, the card has no link). The count on Home and in the page
heading is the length of this file.

Pictures are drawn by the released program itself, off-screen (no window, no input):

    LYNX.exe --pluginthumbs R\thumbs        # every plugin, small: the list of what is built in
    LYNX.exe --artshot R\art after          # full-size editors
    LYNX.exe --plugui R\plugui              # more effect editors
    LYNX.exe --flextrashot R\extra
    LYNX.exe --showcase R\snap\Demo.lynx
    LYNX.exe --snapshot R\snap\s R\snap\Demo.lynx      # windows and dialogs: the wiki's pictures
    python3 website/_src/import-shots.py R               # needs Pillow; writes assets/img/plugins, assets/img/wiki

`import-shots.py` converts the render each entry names in `img` and every `/assets/img/wiki/<name>.webp` a page or
article uses (from `snap/s-<name>.png`), and writes the sizes to `_src/catalog-img.json` and `_src/img-sizes.json`.
The build puts `width` and `height` on every picture from those two files, and the stylesheet never shows a picture
above its own size. `--artshot` also draws each editor at twice the size: that becomes `<id>@2x.webp` (`srcset`). The
other modes of the program draw at 100% only, so window and dialog pictures have no @2x file. A plugin without a
render gets a name plate on its card, never a made-up picture. If `R/logo-<version>.png` (the logo that version
draws, from its `src/ui/LogoData.h`) differs from the brand's mark, the logo button in the plugin headers is painted
over with `assets/img/logo.png`; see the note at the top of the script. The 0.1.25 pictures are painted this way
(103 editors, and `assets/img/eq.webp` is a copy of one): at the 0.1.26 release, whose program draws the vector
pepper itself, render them again and leave `R/logo-<version>.png` out so nothing is painted.

## 0.1.26 release: pictures and catalogue still to do (texts are ready)

The 0.1.26 texts are in the page sources; every picture is still from 0.1.25. No picture was added or changed for
0.1.26, because pictures must come from the released build. At the release, from the 0.1.26 build:

- **Windows and features** (bump `?v=0.1.25` to `?v=0.1.26` in `_src/pages/*.html` after replacing each file):
  `assets/img/hero.webp`, `playlist.webp`, `rack.webp`, `pianoroll.webp`, `mixer.webp`, `synth.webp`, `eq.webp`,
  `automation.webp`, `cliplaunch.webp` (now with the Clip View and Device View), and `assets/media/poster.jpg`.
  `lynx-tour.mp4` and `demo.mp3` are from 0.1.23: re-render or keep the line that says so.
- **Painted logos**: the 103 plugin editors in `assets/img/plugins/` whose header logo was painted over, and
  `assets/img/eq.webp` (a copy of one). 0.1.26 draws the vector pepper itself: render them again with
  `import-shots.py` and leave `R/logo-<version>.png` out so nothing is painted.
- **Why LYNX pictures and captions**: the three captions that say "0.1.25" (`why.online.cap`, `why.work.cap`,
  `why.crash.cap`) go with their pictures: change the text (and drop the key from the 14 language files) when the
  picture is replaced. A shot of the Share plugins dialog and of the Simple workflow would fit the new paragraphs.
- **New pictures the texts would carry** (none is referenced yet, so nothing is broken without them): Simple
  workflow, Clip View with racks, Slicex and FPC editors, Freeze in the Channel rack, the loudness meter.
- **Catalogue** (`_src/catalog.json`, 167 entries, all from 0.1.25): 0.1.26 adds Slicex, FPC, Strike Shaper, GMSynth
  (FLM) and the Pocket effects (Distortion, Limiter, Limiter Classic, Leveller, Gate, Trance Delay, Tape Delay,
  MultiFX, Autoduck, Chorus, Spacer, Stereoizer, Wow & Flutter, Phaser, Flanger, Tremolo, Waveshaper, Compressor,
  Spreader). Each needs an entry (`desc`, `wiki`), its picture and 14 translations; Compressor (FET), Apex Limiter
  (Levelling), LYNX Tune (Breathiness), Fruity Slicer (envelope page) and LYNX Dance (sprite sheets) need their
  pictures and wiki tables refreshed.
- **Wiki**: Simple layout, Layouts, File formats, Coming from Ableton Live and Coming from FL Studio are updated.
  Still written for 0.1.25: Plugins (docked windows, Reload, Pin on top), Mixer, Playlist, Piano roll, MIDI
  controllers (profiles, MPE), Online sessions (shared plugins), Recording, Settings (LYNX extras), Shortcuts (its
  summary names 0.1.25) and Command line.
- The Updates entry's `date` is 2026-11-11, the day it was written: set the release day.

## Other notes

- **Wiki pages**: one markdown file each, with `title`, `summary` and `status` (`full` or `short`) in the front
  matter; `_src/wiki/index.json` sets the four groups and the order (`hidden`: built but not listed; `catalog`: the
  group that also lists the plugin pages; `links`: other site pages listed in the group). The sidebar groups are
  `<details>`, so they fold without JavaScript; "On this page" is made from the article's `##` headings. Article
  shape: what it is, a numbered first task, tables for tools and keys, "Good to know", "Related". Write only what the released version does. Titles,
  summaries and the sidebar are translated; the bodies are English only for now (pages show a note in the reader's
  language).
- **Old addresses**: the site used to be one page. `/#features`, `/#download`, `/#source`, `/#signing` and
  `/#crash-reports` still exist on Home; `/#classics`, `/#try`, `/#media`, `/#gallery`, `/#plugins` and `/#dlPort`
  are sent on to their new page by a small script at the end of Home.
- **Download links** (`DL` in `assets/site.js`) run over `/releases?per_page=10` of the GitHub API; for each OS and
  format the newest non-draft release with a matching file wins: Windows `/setup.*\.exe$/i` (fallback `/\.exe$/i`)
  and `/windows.*\.zip$/i`; macOS `/macos.*\.dmg$/i` (fallback `/\.dmg$/i`); Linux `/\.AppImage$/i` and
  `/linux.*\.tar\.gz$/i`. Without JavaScript, or when the API fails, every button links to `releases/latest`.
  The visitor's OS (`navigator.userAgentData.platform`, then `navigator.platform` / `userAgent`; a touch "Mac" is
  iPadOS) only gets the accent colour. `?os=win|mac|linux|any` forces it (testing, screenshots).
- **Languages**: the same 15 as the app (ids and native names from `src/ui/I18n.cpp`). English is in the pages; the
  others are `i18n/<id>.json` (`ui` strings, `featured` and `insp` plugin descriptions) and `i18n/updates/<id>.json`
  (the Updates page) and `i18n/catalog/<id>.json` (the plugin cards), fetched only for the chosen language. `i18n/latest/<id>.json` is written by the build from the
  updates file. Order: `?lang=` (`pt` still works) > saved choice > `navigator.languages` > English. A string is
  translatable when its element has `data-i18n="key"` (`data-i18n-alt`, `data-i18n-aria`, `data-i18n-ph` for attributes;
  `data-args='["x"]'` fills `{0}`); a new English text needs a new key. CJK pages use the system font first and
  fetch Noto Sans only when it is missing.
- **Features** no longer lists plugins: its two plugin sections (`#plugins`, `#classics`, ids kept) summarise and link
  to the catalogue and to `docs/PLUGINS.md` at the release tag. `_src/plugins.js` is not read by the build any more;
  it stays only because `scripts/gen-inspired.py` writes its `INSPIRED` block (remove both together if wanted).
- **Screenshots** come from LYNX SHARE's own showcase project (`LYNX.exe --showcase Demo.lynx`, then
  `LYNX.exe --snapshot <prefix> Demo.lynx` with `LYNX_SNAPSHOT_SIZE=1920x1080`), never from Image-Line demo projects.
  `assets/media/demo.mp3` is `LYNX.exe --export Demo.lynx demo.wav`; `lynx-tour.mp4` is a slideshow of the
  screenshots over that audio; `peaks.json` is its waveform (600 peaks).
- `?shot` in the address is kept for headless screenshots (nothing on the pages animates on load any more).
- Publish with `scripts/deploy-site.sh` (Hostinger API token in `~/.config/hostinger/token`, never committed).
