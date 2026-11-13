# REA round 190bt: FL's "Remote control settings" window, the control menu and the multilink menu

Goal (coverage programme of 0.1.27, list item 11 / decision F of round 190bk): the detailed way of linking a control
to a MIDI controller, as FL has it. LYNX's one-step learning (right-click a control › *Link to MIDI controller*,
move the knob) stays the default way; this round adds the window FL opens with *Link to controller...*.

Method: static study only of the FL Studio 2026 engine library on the study install (form resources, menu resources,
strings, numeric constants and the behaviour of the form's handlers, read with headless Ghidra on the shared project).
FL was not started. Clean-room: this file describes behaviour in words; no decompiled text was kept (the scratch
folder of the round was deleted). Code: `src/core/LinkFormula.{h,cpp}` (no JUCE), `src/ui/RemoteControl.{h,cpp}`,
tests `tests/Link190btTests.inc`.

Evidence markers: **[FORM]** = the form resource (positions, captions, hints, ranges, defaults), **[MENU]** = a menu
resource of the same form, **[BEHAV]** = what the form's handlers do (static reading), **[TEST]** = LYNX's unit
tests, **[design]** = LYNX's own choice, **[?]** = not settled statically, listed in section 8.

## 1. The window

Form `TMIDIInputForm`, caption "Remote control settings", client area 256 x 490 with FL's own 26 px caption strip.
From top to bottom [FORM]:

| Control | Caption / hint | Range and default |
|---|---|---|
| Parameter list | hint "Current parameter" | the control (the queued controls for a multilink) |
| Assignment list | hint "Current assignment" | "Link 1", "Link 2"..., last "New link"; usable only with more than one row [BEHAV] |
| Group "MIDI controller" | | |
| Port | label "Port", hint "MIDI input port number" | number wheel -1...255, default -1 (none) |
| Channel | label "Channel", hint "MIDI channel number" | 1...16, default 1 |
| Ctrl | label "Ctrl", hint "Continuous Controller number" | -1...999, default -1 (none); a menu lists the standard controllers as "CC #n (name)" plus four special sources [MENU] |
| Auto detect | hint "Just move your MIDI controller" | checked |
| Omni | hint "Input goes to the selected channel / track" | unchecked |
| Mode | hint "How to treat a note or button link" | Default / Hold / Latch / Inc / Stay |
| Group "Internal controller" | list "(none)" + the project's internal controllers; "Record automation", hint "Allow recording of automation" | (none); unchecked |
| Curve | a small graph of the mapping | grid of 4 x 4 cells, curve limited to 0...1 [BEHAV] |
| Group "Mapping formula" | edit, hint "Input to output mapping formula"; button, hint "Presets"; a two-line message | "Input" |
| Group "Smoothing" | check box in the group's title, hint "Enable automation smoothing"; "Time" slider, hint "Smoothing envelope time" | off; -1024...1024, default 469 |
| Remove conflicts | hint "Auto-remove other conflicting assignments" | checked |
| Reset / Skip / Accept | Reset's hint "Reset all settings" | |

Form `TMIDIInputForm_Generic`, caption "Global link settings": the same window without the assignment list, Omni,
the internal controller group and Remove conflicts [FORM].

## 2. What the buttons do [BEHAV]

- **Auto detect**: while checked, a controller message that arrives fills Port (the device's port number), Channel
  and Ctrl. The hint bar says what kind of control it looks like ("Absolute controller", "Endless controller" with
  the step, "Switch"). With the MIDI settings' "Auto accept detected controller" (checked by default in FL) the
  window then accepts by itself: that is FL's one-step link.
- **Accept**: stores the link (one undo step, "edit remote control settings") and goes on as Skip does. A link is
  kept only when Ctrl is not "none"; with an internal controller chosen the link is to that controller instead.
- **Skip**: with several controls queued ("Multilink..." i of n in the hint) shows the next one; otherwise closes.
- **Reset**: Port none, Channel 1, Ctrl none, Omni off, Mode Default, internal controller (none), Record automation
  off, formula "Input", smoothing off with the slider at its default.
- **Conflicts**: another control linked to the same controller (same port, channel and number) is a conflict; the
  "Remove conflicts" caption turns red while there is one. On Accept, with the box checked, those other links are
  removed.
- **Port**: port, channel and number together are the controller's identity. A link with a port follows only the
  devices whose "Port" (MIDI settings, input list) is that number; a link with no port follows the devices with no
  port.
- **Omni**: for a channel's control the link moves that control of the selected channel; for a control of a mixer
  slot's plugin, the same slot of the selected mixer track. [?] which other controls the box is enabled for.

## 3. The mapping formula

An expression over the variable `Input` (the controller's position, 0...1). The result, limited to 0...1, is the
control's position. An empty text or just `Input` (any letter case) reads "(default 1:1 mapping)"; a text that
compiles reads "Compiled ok." [BEHAV].

Functions FL adds for this window, with their number of values [BEHAV]: `Rand()`, `SeededRand(x)`, `Min(a,b)`,
`Max(a,b)`, `Int(x)`, `Round(x)`, `Frac(x)`, `IfE / IfL / IfG / IfLE / IfGE(a,b)` (1 when a = b, a < b, a > b,
a <= b, a >= b, else 0), `Case(c,a,b)` (a when c is 1, else b), `Sum(a,b)`, `Neg(x)`, `Log2(x)`, `Tg`, `CTg`,
`ArcTg`, `Sec`, `CoSec`, and `Tension(x,t)`:

- t = 0: x
- t < 0: ((1 + |t|)^x - 1) / |t|   (slow at the bottom)
- t > 0: 1 - ((1 + t)^(1 - x) - 1) / t   (fast at the bottom)

The general names of FL's expression parser are present as well (pi, div, mod, not / and / or / xor, shl / shr, exp,
ln, log10, logN, power, intpower, sqr, sqrt, abs, round, trunc, floor, ceil, min, max, the trigonometric and
hyperbolic families, degtorad / radtodeg, if). LYNX's parser is its own (recursive descent, Pascal precedence,
case-insensitive, `$` hexadecimal numbers) [design]; `inter`, `internoswap`, `fold`, `warp`, `map` are not built
because their meaning was not settled [?].

Presets [MENU], exactly FL's names, order and formula texts:

| Section | Preset | Formula |
|---|---|---|
| Linear (absolute) | Default | `Input` |
| | Inverted | `1-Input` |
| | Log scale | `Tension(Input,10)` |
| | Inverted log scale | `Tension(Input,-10)` |
| | Switch | `Round(Input)` |
| | Steppy | `Int(Input*9)/8` |
| | Steppy (cents to semitones) | `Int(Input*25)/24` |
| | First half | `Input*0.5` |
| | Last half | `Input*0.5+0.5` |
| | Center dead zone (small) | `Min(Input*1.1,0.5)+Max(Input*1.1,0.6)-0.6` |
| | Center dead zone (large) | `Min(Input*1.3,0.5)+Max(Input*1.3,0.8)-0.8` |
| | MIDI CC centered around 63 | `Input*1.00793650794` |
| | MIDI CC centered around 64 | `Input*0.9921875` |
| Incremental (relative) | Default increment | `Input` |
| | Inverted increment | `-Input` |
| | 4x smaller / 2x smaller / 2x bigger / 4x bigger increment | `Input*0.25`, `Input*0.5`, `Input*2`, `Input*4` |
| | 1/50, 1/20, 1/10 increment | `0.02`, `0.05`, `0.1` |
| | Force boolean | `Ceil(Input)` |
| Special | Launchpad XY - X | `(Frac(Input*7.875+0.001)-0.001)*1.142857142857143` |
| | Launchpad XY - Y | `Int(Input*8)*0.14285714285714285` |

The incremental presets are for endless (relative) controllers, where FL adds n x f(Input) to the control's
position. LYNX applies a link's formula to absolute controllers; relative encoders stay with the controller
profiles (round 190aw). The menu lists the incremental presets because they are valid formulas, but what `Input`
is for an endless controller is open [?].

## 4. Smoothing

The slider's hint reads "Disabled" at 0, otherwise the filter and its time: "1 pole" for negative positions,
"3 poles" for positive ones, and the time (11^(|position| / 1024) - 1) x 100 ms: 0...1000 ms, the default 469 is
about 200 ms with 3 poles [BEHAV]. Moving the slider sets the check box (on unless the position is 0).

LYNX's filter [design]: as many equal one-pole low-passes in a row as the hint's poles, each with the time
constant time / poles, stepped on the message thread with the real elapsed time. FL's exact step response is not
settled [?].

## 5. The control menu and the multilink menu

The control menu (menu resource `WheelMenu`) has, beside the learn item LYNX already had, "Link to controller..."
and "Override global link...", which open the two windows above; the tempo's menu lists fixed tempos "80 BPM" ...
"160 BPM" in steps of 10 [MENU].

The multilink menu (`MLWheelMenu`) acts on the controls tweaked while "Multilink to controllers" is on [MENU]:
Automation › Edit events, Init song with these positions, Init pattern with these positions, Create automation
clips; Remote control › Link to controllers..., Override global links...; Value › Randomize, Humanize, Cancel.
Where FL pops this menu up is not settled [?]; LYNX shows the items it can perform in the Multilink button's menu
[design].

## 6. What LYNX built

- The two windows, laid out from the form's coordinates (x 1.3), with FL's captions, hints, ranges and defaults.
  Right-click a control › *Link to controller...* / *Override global link...*; double-click a link in
  Settings › MIDI.
- `MidiLink` gained port, formula, smoothing (on, slider position) and Omni. A `.lynx` project keeps the six-number
  `midiLinks` rows as before and writes the new fields in a second list, `midiLinkSettings`, only for links that
  have any: older versions read the links as plain links, older projects load unchanged [TEST / --linkshot checks].
- Global links keep port, formula and smoothing in the MIDI settings text (`linkset=` after its `link=` line);
  input devices keep their port (`inputport=`). Text written by older versions reads the same [TEST].
- Settings › MIDI › input devices: a "Port" number per device (FL's "Port" wheel of the input list).
- The control menu's two items, the tempo menu's fixed tempos, the Multilink button's menu items "Init song with
  these positions", "Create automation clips", "Link to controllers...", "Override global links...", "Cancel".
- LYNX-own extras [design]: channel 0 = "any channel" (what a learned link stores; FL's wheel starts at 1), and a
  read-out of the smoothing's poles and time beside the slider (FL shows it in the hint bar).

Left out, because LYNX could not perform them (each would be a dead control): the Mode list and note / button
links; the Ctrl menu's special sources (channel aftertouch, key aftertouch, pitch bend wheel, Note C0); the
"Record automation" box of an internal-controller link; the "Auto accept detected controller" option (LYNX's learn
item is that one-step path); relative handling for links made by hand; the multilink menu's Edit events, Init
pattern with these positions, Randomize and Humanize.

## 7. Best way?

- **FL**: one window for everything; the quick path is the same window accepting by itself after the first
  controller message. Strong: formula + curve preview, conflicts shown before accepting, per-link smoothing.
  Weak: a text formula is opaque to many users (hence the presets), ports are bare numbers.
- **Ableton Live**: MIDI map mode, click a control and move a knob; the mapping browser lists every link with an
  editable Min / Max range. No formula, no smoothing; takeover mode is global. Simpler, covers the common needs
  (invert and range) without a formula.
- **Users**: forum threads praise FL's formula presets (inverted, centre dead zone for worn pitch wheels) and its
  smoothing for stepped 7-bit knobs; the common complaints are forgotten port numbers (a link that "does nothing")
  and not knowing a link exists. Live users praise the visible mapping list.
- **Verdict**: keep LYNX's one-step learn as the default; FL's window as the detailed path (built here, FL-exact);
  and keep LYNX's visible link list in Settings › MIDI, now showing each link's port, formula, smoothing and Omni
  and opening the window on a double-click. Options for the owner, not built: a Min / Max range pair beside the
  formula (Live's way, would write the formula for the user), and naming the device beside a port number.

## 8. Needs a live FL check

The handoff of the round (`HANDOFF.md` of the round's work folder) lists these as questions; in short: the text
shown for "none" in the Port and Ctrl wheels; which controls enable Omni; `Input` and detection for endless
controllers; the smoothing filter's step response; what Accept does with a formula that does not compile; the
parser's precedence and messages; `inter` / `internoswap` / `fold` / `warp` / `map`; the note-link modes; where the
multilink menu appears and how much Randomize / Humanize move; the "Set..." item of the tweak menu.

## 9. Round 190cs: FL-exact after the live check (round 190cq) and a second static read

Evidence: the live FL session of round 190cq (FL Studio 2026 v26.1.7, shots `link190bt-*` in its work folder) and
static reads of FLEngine_x64.dll (headless Ghidra, read-only, decompile scratch deleted). Facts in our own words.

**Parser** [H 0xbaec40, operator table]: each operator is registered with a priority, a lower number binding
tighter: 10 prefix minus and plus, postfix `!` (factorial of the rounded value) and `%` (hundredths), `in`; 20 `^`;
30 `*` `/` `div` `mod` `fmod`; 40 `+` `-`; 50 `>` `>=` `<=` `<` `<>` `=`; 60 `not`; 70 `or` `and` `xor` `shl` `shr`.
So `-Input^2` is `(-Input)^2` (live: FL draws +x²). `=` compares with a 1e-30 tolerance [H 0xbae410]. The checks run
in this order [H 0xbab340]: names (`Invalid identifier: <name>`, [H 0xbacb80]), brackets by count (`Unequal
brackets`, [H 0xba9ee0]), neighbours (`Empty brackets ()`, `Missing operand between )(`, `Missing operand between )
and constant/variable`, `Missing operand between constant/variable and (`, `<operand> two space limited
variables/constants`), then the tree (`Syntax error: function or operand has too few arguments`, `Too many
arguments`, `Syntax error`). The window's message line shows one line: the live "Syntax error: function or operand"
and "input two space limited" are the first lines of the full texts. The message and curve change on Enter only
(live); the store at Accept compiles the edit's text again and keeps nothing when it does not compile [H 0xf8bc70]:
the link maps 1:1 and reopens as "Input" (live).

**Functions** [H 0xbae920 ... 0xbaec10]: `inter(x, a, b)` 1 when x lies between a and b in either order, else 0,
comparing values rounded to thousandths; `internoswap` the same for a <= b only; `fold(x)` a triangle 0 → 1 → 0 over
0 → 1 → 2 (any x); `warp(x, p)` leaves 0...1 alone (1 wraps to 0) and gives FMod(x + p, 1) outside, FMod keeping
the sign (x − Trunc(x / y)·y, [H 0x477690]); `map(x, lo, hi)` = lo + (hi − lo)·x. The live curves agree point for
point, including the drop at the right edge of `warp(Input, p)`.

**Mode** (NoteLinkModeBox, both windows): stored in bits 8...15 of the link's flags [H 0xf8bc70, 0xf8beb0]; Reset
clears it with the rest of the MIDI controller group [H 0xf8a060]. The link dispatcher [H 0xfdaec0]: Default and
Hold pass the event on unchanged; Latch, Inc and Stay act only on events with a value above 0 (presses) and ignore
releases; Stay passes the value, Latch and Inc go through the relative path [H 0x1051370] with step 0 / 1: Latch
switches to whichever of f(0) and f(1) is farther from the control's position, Inc adds f(resolution) (an event's
resolution starts at 1/128, [H 0xfd82b0]), limited to the range. Key events reach links with controller number
256 + key and value velocity / 127, 0 on release [H 0xfda230; scale 1/127 at the global it reads]. The Ctrl menu's
"Note C0" is number 256 [FORM CtrlMenu Tag 256]; 253 / 254 / 255 are channel aftertouch, key aftertouch, pitch bend.

**Layout and colours** (live): no assignment list until the control has a link; Skip only while several controls
are queued; the Internal controller group only when the project has an internal controller, with "Record
automation" (hint "Allow recording of automation", off) at its bottom right; picking a controller clears Ctrl. The
global window has no parameter list, Omni, Remove conflicts or Skip, and no gap above its buttons. Omni is enabled
on a mixer track's volume, greyed on the tempo. Curve preview: background #363f45, the 1:1 line #b8bec3, a compiled
curve #f2d99f, an error #fe3820; hovering reads "In: 0.48 out: 0.48". The smoothing hint reads "286ms  3 poles" /
"7ms 1 pole". The parameter caption is FL's event name ("Channel volume"; "Insert 1 - Volume").

**Built in LYNX**: all of the above. Key links: a linked key's presses and releases go to its link instead of the
instrument (any channel / port the link names); in `.lynx` they sit in their own list `midiNoteLinks` (older
versions skip it instead of reading a controller link), the mode in `midiLinkSettings` / `mode`; global ones on a
`notelink=` line, the mode on `linkmode=`. "Record automation" writes the internal controller's mapped output into
the target's automation while automation is recorded (`Automation::recordLive`, saved only when on).

**Not proven, so not claimed**: Default versus Hold differ nowhere in the dispatcher read here (a device script may
mark its own controls; plain MIDI keys behave the same in both); the resolution of a plain-MIDI key's event is taken
as the record's starting 1/128 (the plain-MIDI record builder itself was not found); `2Input-1` gives "Invalid
identifier: 1" in FL and a "two space limited" message in LYNX; the associativity of `^` (LYNX: left to right);
`and` / `or` / `xor` / `shl` / `shr` on non-whole values; what Auto detect shows for a key in FL's Ctrl wheel (LYNX:
the number, 256 + key).
