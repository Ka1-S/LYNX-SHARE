---
source: a79e43f031
---
LYNX SHARE enregistre les projets en fichiers `.lynx`. Il ouvre aussi les projets d'autres programmes, charge les fichiers audio usuels et exporte de l'audio, du MIDI et des projets pour d'autres programmes. Le tableau liste tous les formats de la version 0.1.26 alpha.

Un projet ouvert depuis le format d'un autre programme est un import. `Fichier › Enregistrer` demande alors où écrire un fichier `.lynx`, et le fichier d'origine n'est jamais modifié.

## Ouvrir un projet d'un autre programme

1. Choisissez `Fichier › Ouvrir...` (`Ctrl+O`).
2. Sélectionnez un fichier `.flp`, `.als`, `.flm`, `.mid` ou `.zip`.
3. Pour un `.flp`, choisissez `Aide › Last FL Studio import report` afin de lire ce qui a été approximé ou laissé de côté.
4. Choisissez `Fichier › Enregistrer` et indiquez où écrire le fichier `.lynx`.

## Formats

| Format | Ouvrir | Enregistrer / Exporter | Remarques |
|---|---|---|---|
| `.lynx` | `Fichier › Ouvrir...` | `Fichier › Enregistrer`, avec sauvegarde automatique | Le format de projet propre à LYNX SHARE (JSON) |
| `.zip` | `Fichier › Ouvrir...` l'extrait dans un dossier et ouvre le projet qui s'y trouve | `Fichier › Exporter › Projet avec samples (.zip)...` | Ouvre aussi le paquet de boucles zippé de FL Studio |
| `.lynxpack` | S'ouvre après un résumé. Une partie est fusionnée dans le projet de l'hôte | `Fichier › Session en ligne › Envoyer le projet...` | Un projet ou une partie envoyé dans une [session en ligne](/wiki/online-sessions/). Les parties à plugins tiers voyagent sous forme de rendus audio avec les réglages du plugin |
| `.flp` | `Fichier › Ouvrir...` | `Fichier › Exporter › Projet FL Studio...`, un projet FL Studio 2026 ou un paquet de boucles zippé | Les plugins natifs jouent sur des moteurs recréés, plus ou moins exacts. Les packs FLEX ne sont jamais ouverts. À l'export, les instruments et effets propres à LYNX sont rendus en audio ou remplacés par le plugin FL le plus proche, et les modulateurs sont laissés de côté. Voir [Venir de FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Fichier › Ouvrir...` | Non | Projet FL Studio Mobile. Partiel : les instruments autres que MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, le Slicer et DirectWave jouent le synthé intégré |
| `.als` | `Fichier › Ouvrir...`, sets de Live 11 et 12 | `Fichier › Exporter › Set Ableton Live (.als)...`, pour Live 12 ou Live 11 | À l'import, les appareils de Live sans équivalent LYNX SHARE sont abandonnés. À l'export, les instruments et effets propres à LYNX SHARE ne sont pas convertis, et l'automation des paramètres d'effets et les routages d'insert à insert sont abandonnés. Voir [Venir d'Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Fichier › Ouvrir...` crée un nouveau projet. `Fichier › Importer › Fichier MIDI...` et le Piano Roll importent dans le projet actuel | `Fichier › Exporter › Fichier MIDI...` (`Ctrl+Shift+M`) | |
| `.wav` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier Wave...`, 16 bits, 24 bits ou 32 bits flottant | Dither, informations de tempo, marqueurs de boucle et marqueurs de la Playlist en option |
| `.mp3` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier MP3...` | Débit constant |
| `.ogg` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier OGG...` | |
| `.flac` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier FLAC...`, compression de 0 à 8 | Sans perte à tous les niveaux |
| `.aif`, `.aiff` | Comme sample ou comme clip audio | Non | |
| `.wv` | Comme sample | Non | Samples WavPack, tels que livrés dans les packs de FL Studio |
| `.sf2`, `.sf3` | Joués par LYNX Soundfont | Non | SoundFonts |
| `.fst` | Depuis le Browser | Non | Preset d'un plugin natif de FL Studio. Seulement pour les plugins dont LYNX SHARE a un moteur |
| `.fsc` | Piano Roll et éditeur d'événements | Piano Roll et éditeur d'événements | Fichier de partition et d'automation de FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Charger un fichier de preset...` dans le menu de presets d'un plugin hébergé | `Enregistrer le preset...` et `Exporter un fichier de preset...` | Presets de plugins tiers. `.fxp` et `.fxb` concernent les plugins VST2 et `.vstpreset` les plugins VST3, et seulement quand le plugin tourne dans LYNX SHARE |
| Fichier vidéo | `Fichier › Importer › Fichier vidéo...` place une vidéo dans la Playlist | `Écrire aussi une vidéo avec ce mixage` dans la fenêtre d'export | L'import utilise les décodeurs du système sous Windows et macOS, et demande FFmpeg sous Linux. L'export demande FFmpeg |

## Autres exports

| Commande | Ce qu'elle fait |
|---|---|
| Option `Fichiers` de la fenêtre d'export | Stems : un fichier par insert du Mixer, par canal, ou par canal avec les instruments seuls |
| `Fichier › Exporter › Toutes les pistes de la Playlist` | Un fichier par piste de la Playlist |
| `Fichier › Exporter › Tous les groupes de pistes de la Playlist` | Un fichier par groupe de pistes |
| `Fichier › Exporter › Masteriser...` | Rend le projet et le masterise vers une cible de loudness, sur votre ordinateur |
| `Fichier › Exporter › Os du projet...` | Écrit dans un dossier les presets et partitions des canaux et des effets |

La fenêtre d'export rend le morceau, le pattern actuel ou la sélection de la Playlist.

## En ligne de commande

Sous Windows, le programme est `LYNX.exe`. Sous macOS et Linux, c'est `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` et `--export-flp` acceptent aussi un `.als` en entrée.

## Bon à savoir

- Les fichiers audio de plus de 30 secondes sont lus en flux depuis un cache disque.
- Un `.flp`, `.als` ou `.flm` n'est jamais écrit quand vous enregistrez. Les commandes d'export écrivent de nouveaux fichiers.
- Les fichiers `.wv` sont en lecture seule.
- Un `.zip` est d'abord extrait dans un dossier, puis le projet qu'il contient est ouvert.

## Voir aussi

- [Importer des projets](/wiki/importing-projects/)
- [Export](/wiki/exporting/)
- [Ligne de commande](/wiki/command-line/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
