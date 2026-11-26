---
title: Save, versions and backups
summary: Save your song as a LYNX project, keep each idea as a new version, and get an older state back from the backups LYNX writes while you work.
---
## Save as

![The project saved as My beat](/assets/img/learn/save-backups-1.webp)

1. Choose `File › Save as...` and give the project a name. A LYNX project (.lynx) keeps the patterns, the Mixer, the plugins and their settings, and points to its samples and recordings, which stay files of their own.
2. From then on `File › Save` (`Ctrl+S`) saves over it. The file it replaces goes to the backups.

## Save new version

![Saved as a new version](/assets/img/learn/save-backups-2.webp)

1. Trying an idea? `File › Save new version` (`Ctrl+N` in the Pattern layout, as in FL Studio; `Ctrl+Alt+S` in every layout) saves the project as My beat_2, next to the first, and keeps working in it.
2. LYNX also writes backups while you work, so a crash or a mistake costs little.

## Versions and backups

![The Versions window](/assets/img/learn/save-backups-3.webp)

1. `File › Versions...` lists the saved versions and the backups of this project, newest first.
2. Choose one and click `Open as a copy`: it opens as a new, unsaved project, and the files stay as they are. `Label...` adds a note to it.
3. `File › Revert to last backup...` replaces the open project with its newest backup. The project file itself only changes when you save.

## Move a project to another computer

1. Samples and recordings are files beside the `.lynx`, not inside it. `File › Collect all and save` copies the ones from outside the project's folder into it and saves; tick `File › Always collect and save` to do it on every save.
2. `File › Export › Zipped loop package...` writes one `.zip` with the project and every file it uses: copy that file to the other computer.
3. `File › Open the project's folder` shows where the project and its files are.

See also [Undo and history](/wiki/undo-history/).
