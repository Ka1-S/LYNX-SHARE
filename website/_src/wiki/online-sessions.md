---
title: Online sessions
summary: Work on one project with up to three other people, by sending packages or live.
status: full
---
There are two ways to work on a project with other people, both under `File › Online session`. Packages need no network, account or server. Live sessions go over the internet through the LYNX SHARE relay. Up to 4 people take part in one project, you included.

## Packages: send a project and get a part back

The host sends the project:

1. Open the project and choose `File › Online session › Send project...`.
2. Type your name and your friend's name.
3. Choose what your friend keeps when the session ends: `Only their own part (recommended)` or `A full copy of the project`. This is a courtesy between musicians, not copy protection: the package is a normal zip.
4. Save the `.lynxpack` file and send it any way you like (e-mail, a shared folder, a USB stick).

The package holds the project, every sample and recording it uses, and audio renders ("stand-ins") of the parts that use a third-party plugin, so your friend hears them without owning your plugins.

The friend records a part:

1. Open the `.lynxpack` with `File › Open...`. A summary shows who sent it and which mixer inserts and Playlist tracks are yours. Click `Open`.
2. Add your own channels, patterns and audio on your inserts and Playlist tracks. The host's material is left as it is.
3. Choose `File › Online session › Send my part back...` and send the new package to the host.

Each guest gets a colour, 20 mixer inserts and 16 Playlist tracks.

The host merges the part:

1. Open the project that was sent and choose `File › Online session › Merge a part...`.
2. The friend's channels, patterns, notes, clips and inserts are added in their colour. Nothing of yours is changed.
3. Merging is one undo step. A newer part from the same friend replaces the earlier one.

A channel or insert whose plugin is not installed plays its stand-in and shows an amber `R` badge in the Channel rack and the Mixer. Stand-ins follow clips you move, but not edits to the notes of a plugin you do not have: ask its owner for a new package. Both sides should use the same LYNX SHARE version. The summary warns when they differ.

## Live sessions

The host starts the session:

1. Open the project and choose `File › Online session › Start live session...`.
2. Type your name. Choose what guests may edit (`Only their own channels, patterns, inserts and rows (recommended)` or `Everything`) and what they keep when the session ends.
3. Click `Start`, then copy the invite code and send it to up to 3 people.

A guest joins:

1. Choose `File › Online session › Join live session...`.
2. Type your name, paste the invite code and click `Join`. The host lets you in, the host's project opens, and every change anyone makes reaches everyone.

During a session, the same menu has:

- `Session chat...`.
- `Follow the host's transport`, for guests.
- `People and permissions...`, for the host, to change what each person may edit.
- `Show invite code...`.
- `End live session` for the host, `Leave live session` for a guest. The code stops working when the host ends the session.

A guest whose connection drops reconnects by itself and keeps their edits. If the host's LYNX SHARE closes during a session, the next start offers to resume it. Parts that use a plugin someone does not have are rendered by the person who has it and streamed as audio.

## What goes over the network

Packages are files: nothing is uploaded anywhere.

A live session connects only while you are in one. Everything goes through the relay `relay.lynx-share.com`, end-to-end encrypted with a key that comes from the invite code. The relay passes the data on without being able to read it. It sees your IP address, when you were connected and how many bytes went through. The other people in the session see your name, your edits, the samples your part uses and whether you are playing or recording. They do not see your IP address. Details are in the [privacy policy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).
