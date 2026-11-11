---
title: Online sessions
summary: Work on one project with up to three other people, by package or live.
status: full
---
There are two ways to work on a project with other people, and both are under `File › Online session`. A package is a file you send to a friend, who sends a part back. It needs no network, account or server. A live session connects everyone over the internet through the LYNX SHARE relay, and every change reaches everyone. Up to 4 people take part in one project, you included.

## Send a project to a friend

![Send project](/assets/img/wiki/session-send.webp)

1. Open the project and choose `File › Online session › Send project...`.
2. Type your name and your friend's name.
3. Choose what your friend keeps when the session ends: `Only their own part (recommended)` or `A full copy of the project`.
4. Save the `.lynxpack` file.
5. Send the file any way you like: e-mail, a shared folder, a USB stick.

The package holds the project, every sample and recording it uses, and audio renders ("stand-ins") of the parts that use a third-party plugin. Your friend hears those parts without owning your plugins.

## Record a part and send it back

The friend does this:

1. Open the `.lynxpack` with `File › Open...`. A summary shows who sent it and which mixer inserts and Playlist tracks are yours. Click `Open`.
2. Add your own channels, patterns and audio on your inserts and Playlist tracks. The host's material is left as it is.
3. Choose `File › Online session › Send my part back...` and send the new package to the host.

Each guest gets a colour, 20 mixer inserts and 16 Playlist tracks.

## Merge a part

The host does this:

1. Open the project that was sent.
2. Choose `File › Online session › Merge a part...` and pick the package your friend sent back.
3. The friend's channels, patterns, notes, clips and inserts are added in their colour. Nothing of yours is changed.

Merging is one undo step. A newer part from the same friend replaces the earlier one.

## Start or join a live session

![Start live session](/assets/img/wiki/session-start.webp)

The host starts the session:

1. Open the project and choose `File › Online session › Start live session...`.
2. Type your name.
3. Under `Guests may edit`, choose `Only their own channels, patterns, inserts and rows (recommended)` or `Everything`. Under `When the session ends, guests keep`, choose their own part or a full copy.
4. Click `Start`, then copy the invite code and send it to up to 3 people.
5. When someone asks to join, click `Let them in`.

A guest joins:

1. Choose `File › Online session › Join live session...`.
2. Type your name, paste the invite code and click `Join`.
3. Wait for the host to let you in. The host's project opens.

## During a live session

![Session chat](/assets/img/wiki/session-chat.webp)

The `File › Online session` menu has these commands while a session runs:

| Command | What it does |
|---|---|
| `Session chat...` | Opens the chat. The menu item shows the number of unread messages |
| `Follow the host's transport` | For guests: play, stop and position follow the host |
| `People and permissions...` | For the host: what each person may change, and what each keeps at the end |
| `Show invite code...` | For the host: shows the code again, with a `Copy` button |
| `End live session` | For the host. Everyone leaves, and the code stops working |
| `Leave live session` | For a guest. Your part of the project stays with you |

In `People and permissions...`, each person gets one of four roles: `Their own channels, patterns, inserts and rows`, `Their own parts + tempo and project settings`, `Everything` or `Listen only (no edits)`. Turning a permission off never deletes anything.

## Plugins the others do not have

A channel or insert whose plugin is not installed on your computer plays a stand-in, and shows an amber `R` badge in the Channel rack and the Mixer.

- In a package, the stand-ins are audio renders made when the package is saved. They follow clips you move, but not edits to the notes of a plugin you do not have. Ask its owner for a new package.
- In a live session, parts that use a plugin someone does not have are rendered by the person who has it and streamed to the others as audio.

## What goes over the network

Packages are files. Nothing is uploaded anywhere.

A live session connects only while you are in one. Everything goes through the relay `relay.lynx-share.com`, end-to-end encrypted with a key that comes from the invite code. The relay passes the data on without being able to read it. It sees your IP address, when you were connected and how many bytes went through. The other people in the session see your name, your edits, the samples your part uses and whether you are playing or recording. They do not see your IP address. Details are in the [privacy policy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Good to know

- `Only their own part` is a courtesy between musicians, not copy protection. The package is a normal zip.
- Both sides should use the same LYNX SHARE version. The package summary warns when they differ.
- A guest whose connection drops reconnects by itself and keeps their edits.
- If the host's LYNX SHARE closes during a session, the next start offers to resume it, and the guests reconnect by themselves.
- In a live session, undo and redo bring back only your own edits.
- Anyone with the invite code can ask to join, but only after the host lets them in.

## Related

- [What only LYNX SHARE does: online sessions](/why/#online)
- [File formats](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Undo and history](/wiki/undo-history/)
- [Troubleshooting](/wiki/troubleshooting/)
