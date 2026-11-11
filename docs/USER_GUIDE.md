# LYNX SHARE user guide

How to use LYNX SHARE's own features, the ones FL Studio does not have. For the FL-style workflow (Channel Rack,
Piano Roll, Playlist, Mixer) see [WORKFLOW_FL.md](WORKFLOW_FL.md); for the built-in plugins see [PLUGINS.md](PLUGINS.md).

## Projects from other apps

**File › Open...** (Ctrl+O), or a double-click in the Browser, opens:

| File | From | What happens |
|---|---|---|
| `.flp` | FL Studio | imported: channels, samples, patterns, arrangement, automation, mixer and FL's native plugins |
| `.als` | Ableton Live 11 / 12 | imported: tracks, clips (warped audio too), groups, returns, mixer and automation; Session clips go to the clip grid (Settings › Session clips of Live sets: or onto the Playlist) |
| `.flm` | FL Studio Mobile | imported, partial: arrangement, samples, drum kits, levels and routing; other instruments play a built-in synth for now |
| `.mid` | any app | opened as a project |

After an import, **Help › Last project import report** lists what was converted, approximated or left out, and
**Help › Missing files...** helps find samples that moved. Save the result as a `.lynx` project.

To go back, **File › Export** has **FL Studio project...** (`.flp`, or a zip with the samples) and **Ableton Live set
(.als)...** (Live 12 or Live 11, with the samples collected). Each export shows what did not go over: LYNX-only
instruments and effects are rendered to audio or replaced in an `.flp`, and are not converted yet in an `.als`.

**Layout style** (Settings, or View › Layout style): **Pattern workflow** (FL Studio-style, the default) or
**Clip-launch workflow** (an Ableton Live-style clip grid with scenes beside the Playlist). Switching moves panels only;
the project stays the same.

## Online sessions: make a song with a friend (phase 1, packages)

Send your project to a friend, who records their part in their own LYNX SHARE with their own plugins and sends it back; you
merge it into your project. Phase 1 works with files only: no network, no account, no server. Send the package however
you like (e-mail, WhatsApp, a shared cloud folder, a USB stick). The live version (both of you in the same project at
the same time) is described [below](#online-sessions-live).

### 1. The host sends the project

1. Open your project and choose **File › Online session › Send project...**
2. Type **your name** and **your friend's name**.
3. Choose what your friend keeps when the session ends:
   - **Only their own part** (recommended): after they send their part back, their copy of the project is reduced to
     what they made.
   - **A full copy of the project**: they keep everything.

   This is a courtesy between musicians, not copy protection: the package is a normal zip.
4. Click **Save package...** and choose where to save the `.lynxpack`.
5. LYNX SHARE renders the parts that use a third-party plugin (VST3, VST2, CLAP) into audio **stand-ins**, so your
   friend hears them even without your plugins, and writes the package: the project, every sample and recording it
   uses (each file once, checked with SHA-256), the stand-ins and a manifest (the plugins used, with versions).
6. Send the `.lynxpack` file to your friend.

Up to 4 people take part in one project (you included): each guest gets their own colour, their own mixer inserts (20
of them, after the ones you use) and their own Playlist tracks (16 of them).

### 2. The friend opens it and records their part

1. In LYNX SHARE, **File › Open...** the `.lynxpack` (or double-click it on Windows).
2. A summary shows who sent it, which mixer inserts and Playlist tracks are theirs, and what they keep at the end. Click
   **Open**. The project is unpacked into `Documents/LYNX/Projects/Received/<package name>/`.
3. The song plays the same as on the host's PC. A channel or insert whose plugin is not installed here plays its
   stand-in and has an amber **R** badge in the Channel Rack and the Mixer (the hint bar explains it). Its plugin
   settings stay in the project: the host still has them.
4. If a plugin is installed here in another version, LYNX SHARE asks whether to **play the renders** (sounds as sent)
   or **use my version** (may sound different).
5. Record your part: add your channels, patterns and audio, use your inserts and your Playlist tracks. New channels and
   patterns you make are yours; the host's are left as they are.
6. Save as usual (**File › Save**) while working.

### 3. The friend sends their part back

1. **File › Online session › Send my part back...** and choose where to save the package.
2. LYNX SHARE renders stand-ins for **your** parts that use third-party plugins (so the host hears them even without
   your plugins) and writes a small package with only your area: your channels, your notes, your patterns and
   automations, your Playlist clips, your inserts, your recordings and samples, and your stand-ins.
3. Send it to the host. If the host chose "Only their own part", your copy is now reduced to your part.

### 4. The host merges the part

1. Open the project you sent (the same one: the package checks it) and choose **File › Online session › Merge a
   part...** (or open the part's `.lynxpack` with **File › Open...**).
2. The friend's part is added in their colour: their channels, patterns, notes, clips and inserts. Nothing of yours is
   changed. If you used one of their inserts or Playlist tracks meanwhile, theirs moved to free ones (the message says so).
3. Merging is **one undo step**: **Edit › Undo** takes it out again.
4. Their media are copied next to your project (`<project folder>/Session media/<their name>/`); their stand-ins play
   where you lack their plugins (amber **R** badge).
5. Merging a newer part from the same friend replaces their earlier part (the things they deleted are removed too).

### Good to know

- Stand-ins are renders of the part (instrument channels: per pattern, played at each Playlist clip; inserts or parts
  with song-level automation: the whole song). They follow the clips you move, but not edits to the notes of a plugin
  you do not have: ask its owner for a new package.
- Stand-ins are saved next to the project in `Stand-ins/`. A part that did not change is not rendered again.
- The package keeps every plugin's settings (opaque, as the plugin saved them). Nothing is uploaded anywhere.
- Both sides should use the same LYNX SHARE version (the open summary warns when they differ).

## Online sessions: live

Up to four people in the same project at the same time, over the internet, with no account and no router setup.
Everything travels encrypted with a key made from the invite code; the LYNX SHARE relay only passes the sealed
data on. Everyone must use the same LYNX SHARE version.

1. The host opens the project and chooses **File › Online session › Start live session...**, picks a name and what
   guests may edit, and sends the **invite code** to the others.
2. Each guest chooses **File › Online session › Join live session...** and types the code; the host lets them in.
3. Everyone works in their own part (their channels, patterns, inserts and Playlist rows, in their colour). Samples
   and recordings arrive in the background; parts made with plugins you do not have play as stand-ins.
4. **Session chat...**, **People and permissions...** (host) and **Follow the host's transport** (guests) are in the
   same menu.

### When something goes wrong

- **The connection drops**: LYNX SHARE reconnects by itself for up to 15 minutes; keep working, your edits wait
  and are sent when the line is back.
- **The host's LYNX SHARE closes or crashes**: guests see *Waiting for the host...*. When the host starts LYNX SHARE
  again it asks whether to resume the session; everyone carries on.
- **A guest's LYNX SHARE closes or crashes**: for the others that guest is just away, and their place, part, colour
  and permissions are kept. At the next start LYNX SHARE asks **Go back in?** — *Go back in* returns you as
  yourself with everything you had changed, also what had not reached the host yet; *Leave the session* gives the
  place up. To leave on purpose use **Leave live session**. The question is offered for 12 hours.
- **Away for long in a full session**: when someone new knocks and every place is taken, the guest who has been away
  the longest loses their place (their part stays in the project).

### Limits

- **Audio files**: one guest may send 1 GB into a session. Beyond that the host is asked once whether to
  **Raise the limit** for that guest; until then the new files stay on the guest's computer (notes, mixer and
  everything else still go through). A whole session may move 8 GB; after that no new audio files travel. For a
  big project, send it first with **Send project...** and start the live session from the same project.
- **The relay**: a session may stay open for 3 days and is closed after 12 hours without any activity; the relay
  also limits how much one session and one connection may move per day. LYNX SHARE tells you when the relay
  closed a session for that reason.
