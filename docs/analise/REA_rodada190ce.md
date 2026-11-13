# Round 190ce: Browser collections, hot-swap and the Device View's device commands, what they are based on

Clean room: LYNX's own code and art. Sources are the reference DAW's own shipped lessons (text read, pictures looked at,
nothing copied), its binary's string tables, its settings file on this machine and screenshots taken in earlier rounds.
Static only: nothing was run or clicked in this round. No trademarks, names or art of the reference DAW in LYNX's UI.

Tags:
- **[LESSON]** Live 12's built-in lessons (`Resources/Builtin Lessons/*/LessonsEN.txt` and their pictures; the lines
  used are collected in `/mnt/e/lynx-work/livebrowser190ce/ev/live-lessons-extract.txt`).
- **[CFG]** Live 12.4's library settings on this machine (`Preferences/Library.cfg`, an XML text file).
- **[BIN]** Live 12.4's string tables (`ableton/live-exe-strings.txt`, round 175a).
- **[SHOT]** screenshots of the running Live 12.4 Trial from earlier rounds (`/mnt/e/lynx-work/detail183a/live/`).
- **[COV]** rows of the coverage survey of round 190bk (`rea190bk/COVERAGE-LIVE-2.md`), which quote the published manual.
- **PROVEN** = one of the tags shows it. **NEEDS A LIVE CHECK** = not shown by anything on disk; LYNX's choice is named.

## 1. Collections

| Fact | Source | Status |
|---|---|---|
| There are seven colour labels; the library settings hold seven label entries numbered 1..7 | [CFG] seven label entries, colour index 1..7 | PROVEN |
| Only the first has a name ("Favorites") and is switched on by default; the other six have no name and are off | [CFG] first entry: display name set, enabled; entries 2..7: empty name, not enabled | PROVEN |
| The colours are red, orange, yellow, green, blue, purple, gray | [BIN] seven collection chip pictures of the hardware UI named by colour; [LESSON] picture `HotSwapBanner.png` shows the first collection in red in the sidebar | PROVEN (the set); the order red..gray = keys 1..7 NEEDS A LIVE CHECK |
| An item's labels are one bit mask (an item carries several at once); labels are set and cleared by mask | [BIN] the file index sets `colors \| mask` and clears `colors & ~mask`; a query asks whether any file has a colour | PROVEN |
| A collection can be renamed and shown or hidden in the sidebar | [CFG] name and enabled flag per label; [COV] browser-0010 | PROVEN |
| A menu command clears all colours of the selection | [BIN] a menu string for "clear all colours" | PROVEN |
| Number keys 1..7 toggle a label on the selected item, 0 clears them | [COV] browser-0007 / browser-0008 (manual) | NEEDS A LIVE CHECK for the key that belongs to each colour |
| Clicking a collection lists every item with that label, of any kind (samples, presets, devices, folders) | [COV] browser-0009 | PROVEN by the manual text only |
| Whether an item can be given a label whose collection is hidden | none | NEEDS A LIVE CHECK (LYNX: yes, and the menu ticks it) |

What LYNX does with it (owner decision L of round 190bk: several collections, Starred is the first):
- FL's Starred flag stays collection 1; six more are added, with the reference's colour set in the order above.
- Stored as one bit mask per item (`core/BrowserCollections.h`), in `browser-collections.txt` in the settings folder;
  the older `browser-starred.txt` is read once and still written, so an older LYNX keeps its stars.
- The STARRED tab: only Starred shown (the default) = the tab exactly as before. More shown = one heading per collection.
  There is no second pane: LYNX's Browser stays one tree with three tabs (FL's layout).
- Keys 1..7 and 0 on a selected Browser row (while the Browser has the focus and the typing keyboard is off), the
  "Collections" sub-menu of a row's right-click menu, colour chips at the right of each labelled row.
- The search also finds items by the name of a collection they are in.

## 2. Hot-swap

| Fact | Source | Status |
|---|---|---|
| Q, or the Hot-Swap button in the device's title bar, starts the mode | [LESSON] Playing Software Instruments › Hot-Swapping Presets | PROVEN |
| The button is a small round two-arrow button at the right of the title bar, next to the save (disk) button | [LESSON] picture `BrowseButtonInst.png` | PROVEN |
| The preset last chosen is shown highlighted in the browser | [LESSON] same page | PROVEN |
| A coloured banner at the top of the browser names what is being swapped and has a close button | [LESSON] same page, picture `HotSwapBanner.png` ("Swapping" + the device kind, a circled X) ; [BIN] a "hot-swap bar" UI class | PROVEN |
| The mode is "a temporary link between the browser and the device" | [LESSON] same page | PROVEN |
| Double-click or Enter on a browser item replaces the device's preset; the mode stays on for the next choice | [LESSON] same page | PROVEN |
| The mode ends with Q again, Esc, the banner's close button or the device's button | [LESSON] same page | PROVEN |
| A sample slot (Simpler) has its own hot-swap button | [LESSON] What's New › Sound Similarity Swapping | PROVEN |
| Whether the device's on / off state survives a swap; whether the browser goes back to where it was when the mode ends; what Undo does while the mode is on | none | NEEDS A LIVE CHECK |

What LYNX does with it (`core/HotSwap.h`, `ui/BrowserComponent`, `ui/DetailView`):
- One mode of LYNX's one Browser. Started with Q, the two-arrow button of a device card, "Hot-swap in the Browser" in a
  card's or a mixer slot's menu. Ended with Q, Esc, the bar's X, the card's button, or by itself when the device is deleted.
- Linked to a mixer effect: Plugin database effects, FL effect presets and LYNX effect presets of the same effect take its
  place; the slot keeps its place, its Mix and its sidechain sources, and the device comes in with its own on / off
  state (round 190cy, see below).
- Linked to a channel: Plugin database generators and synth presets replace the instrument (place, notes, insert and
  levels stay); a channel that plays a sample takes another sample, and the Browser opens at the current sample.
- Each swap is one undo step; Undo brings the earlier device back and the link follows it (LYNX's choice).
- When the mode ends the Browser shows the tab and the search it showed before (LYNX's choice, kept as a LYNX extra by
  the owner's decision of round 190cy: Live leaves its browser where hot-swap took it).
- Not built: hot-swap of rack chains and drum pads, swap to the next / previous similar sample.

### Live check 190cw (Live 12.4.16) and round 190cy
- A swapped-in device does not keep the old one's on / off: an Utility that was off, swapped for the preset "Wide
  Stereo", came in on. LYNX: a new effect keeps the state it was loaded with (on); a preset of the same effect (LYNX's
  `.lynxpreset`, a hosted plugin's preset file) switches the device on. A channel keeps its mute (the instrument card's
  LED is the channel's mute, shared with the Pattern workflow).
- Hot-swap starts on the current preset (Mono.adv selected). LYNX: an effect whose settings came from a preset (its
  preset name) opens on that preset's file, among FL's presets of the plugin it stands for and LYNX's own saved presets
  of the effect: in its folder when the Browser shows that folder, else as a list of the effect's presets with the
  current one selected (the list goes when the mode ends). Factory presets have no file and are not shown.
- Undo while hot-swapping, Q with nothing selected: as built.
- Collections: the key of a hidden collection labels the item and shows the collection (also from the row menu).
  The Clip-launch workflow's row menu lists the collections flat, as Live's item menu: "Clear all colours" (0), then
  the seven with their keys; Rename and Hide are on a collection's heading, which also has "Show in the STARRED tab"
  (Live's Collections › Edit). The Pattern workflow keeps round 190ce's "Collections" sub-menu.

## 3. Device commands in the Device View

| Fact | Source | Status |
|---|---|---|
| A device's title-bar menu: Cut Ctrl+X, Copy Ctrl+C, Rename Ctrl+R, Edit Info Text, Delete, Group Ctrl+G, Group to Drum Rack, Fold, Show Preset Name, Compare (switch to B / copy A to B), Save as Default Preset | [SHOT] `detail183a/live/d04-title-menu.png` | PROVEN |
| Duplicate Ctrl+D; Ctrl-drag copies a device; 0 switches the selected device on / off | [COV] deviceview-0013, -0014, -0023 (manual) | PROVEN by the manual text only |
| Ungroup is Ctrl+Shift+G | [COV] racks rows (manual) | PROVEN by the manual text only |
| A save (disk) button sits in the title bar beside the hot-swap button | [LESSON] picture `BrowseButtonInst.png`; [LESSON] Saving Presets | PROVEN |

What LYNX does with it: Cut / Copy / Paste / Duplicate and Ctrl-drag for effect cards (`core/DetailChain.h`
pasteEffect / duplicateEffect), 0 for the selected card, Ctrl+G / Ctrl+Shift+G on the selected effect card (the Mixer's
existing Group / Ungroup), the hot-swap and save buttons in a card's title bar. A copy is a new device with its own
running instance. Left out: the instrument card (a channel is not a device in a chain), several cards selected at once,
a drag to another track, Edit Info Text, Group to Drum Rack, Save as Default Preset.

## 4. Map mode (not built in this round)

The reference's MIDI / Key Map Mode [LESSON: Connecting MIDI Controllers] colours every mappable control and takes the
next controller move as its link. LYNX's links stay FL's (Link to controller, Multilink; `Song::midiLinks`). An overlay
needs a registry of every control's place and target that LYNX's panels do not have yet, and the link record itself is
being changed by round 190bt (port, formula, smoothing). It is left for a round of its own, storing into `midiLinks`.
