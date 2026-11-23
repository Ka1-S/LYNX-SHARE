---
source: e1ff018baf
---
LYNX SHARE enregistre les projets en fichiers `.lynx`. Il ouvre aussi les projets d'autres programmes, charge les fichiers audio usuels et exporte de l'audio, du MIDI et des projets pour d'autres programmes. Le tableau liste tous les formats de la version 0.1.31 alpha.

Un projet ouvert depuis le format d'un autre programme est un import. `Fichier › Enregistrer` demande alors où écrire un fichier `.lynx`, et le fichier d'origine n'est jamais modifié.

## Ouvrir un projet d'un autre programme

1. Choisissez `Fichier › Ouvrir...` (`Ctrl+O`).
2. Sélectionnez un fichier `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid` ou `.zip`.
3. Choisissez `Aide › Dernier rapport d'import de projet` afin de lire ce qui a été approximé ou laissé de côté.
4. Choisissez `Fichier › Enregistrer` et indiquez où écrire le fichier `.lynx`.

## Formats

| Format | Ouvrir | Enregistrer / Exporter | Remarques |
|---|---|---|---|
| `.lynx` | `Fichier › Ouvrir...` | `Fichier › Enregistrer`, avec sauvegarde automatique | Le format de projet propre à LYNX SHARE (JSON). `Fichier › Enregistrer une copie pour un ancien LYNX...` écrit une copie que 0.1.27 ou 0.1.28 ouvre, et liste ce que cette version ne peut pas garder |
| `.zip` | `Fichier › Ouvrir...` l'extrait dans un dossier et ouvre le projet qui s'y trouve | `Fichier › Exporter › Paquet de loops zippé...` | Ouvre aussi le paquet de boucles zippé de FL Studio |
| `.lynxpack` | S'ouvre après un résumé. Une partie est fusionnée dans le projet de l'hôte | `Fichier › Session en ligne › Envoyer le projet...` | Un projet ou une partie envoyé dans une [session en ligne](/wiki/online-sessions/). Les parties à plugins tiers voyagent sous forme de rendus audio avec les réglages du plugin |
| `.flp` | `Fichier › Ouvrir...` | `Fichier › Exporter › Projet FL Studio...`, un projet FL Studio 2026 ou un paquet de boucles zippé | Les plugins natifs jouent sur des moteurs recréés, plus ou moins exacts. Les packs FLEX ne sont jamais ouverts. À l'export, les instruments et effets propres à LYNX sont rendus en audio ou remplacés par le plugin FL le plus proche, et les modulateurs sont laissés de côté. Voir [Venir de FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Fichier › Ouvrir...` | `Fichier › Enregistrer une copie comme projet FL Studio Mobile (.flm)...` | Projet FL Studio Mobile. Partiel : les instruments autres que MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, le Slicer et DirectWave jouent le synthé intégré. L'export est une copie : elle garde les notes, le tempo, le mixer, les instruments propres à FL Studio Mobile, le DW Sampler, 3x Osc, les drum kits, les effets et l'automation du niveau du mixer, du panoramique, des effets, du DW Sampler et de GMSynth, avec les samples à côté. Dans la copie, les autres instruments gardent leurs notes sur le MiniSynth, et les effets sans module FLM ainsi que l'automation d'autres cibles (les boutons propres aux synthés, les plugins tiers) sont laissés de côté ; une fenêtre dresse la liste de tout cela avant l'enregistrement. |
| `.als` | `Fichier › Ouvrir...`, sets de Live 11 et 12 | `Fichier › Exporter › Set Ableton Live (.als)...`, pour Live 12 ou Live 11 | À l'import, les appareils de Live sans équivalent LYNX SHARE sont abandonnés. À l'export, les instruments et effets propres à LYNX SHARE ne sont pas convertis (Helix devient Wavetable), et l'automation des paramètres d'effets et les routages d'insert à insert sont abandonnés. Voir [Venir d'Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `Fichier › Ouvrir...` | `Fichier › Exporter › DAWproject (.dawproject)...` | Le format d'échange ouvert de Bitwig, Studio One, Cubase et d'autres applications : pistes, dossiers, clips, notes, audio avec ses marqueurs de warp, automation, tempo, marqueurs, le mixer avec ses envois, et plugins VST3, VST2 et CLAP avec leur état. Les effets de notes et les expressions MIDI sont laissés de côté à l'import ; la grille de clips, les autres arrangements et certains détails par note à l'export. Dans les deux sens, ce qui a été laissé de côté est listé |
| `.band` | `Fichier › Ouvrir...` (le paquet, son fichier `projectData` ou un zip de celui-ci) | Non | Projet GarageBand 10 ou GarageBand pour iOS, comme pistes de la disposition simple. Partiel : les pistes d'instrument jouent le synthé et la batterie intégrés, sauf les instruments Sampler créés dans GarageBand pour iOS. Les projets de GarageBand 11 et antérieurs ne sont pas lisibles |
| `.blx` | `Fichier › Ouvrir...`, ou son dossier avec l'import `Dossier...` de la disposition simple | Non | Projet BandLab téléchargé par Cakewalk ou Sonar, comme pistes de la disposition simple avec régions, tempo, tonalité, le mixage, l'automation du volume et du panoramique et les effets courants. Partiel : les sons d'instruments de BandLab et les autres pédales sont listés dans le rapport d'import |
| `.mid`, `.midi` | `Fichier › Ouvrir...` crée un nouveau projet. `Fichier › Importer › Fichier MIDI...` et le Piano Roll importent dans le projet actuel | `Fichier › Exporter › Fichier MIDI...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `Fichier › Ouvrir...` | Non | Modules de tracker (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). Les instruments deviennent des canaux Sampler avec leurs samples, le morceau devient des clips de pattern avec ses changements de tempo, et `Vue › Tracker` édite les notes. En partie : les effets comme les slides, le vibrato et les arpèges, les enveloppes et les changements de volume sur une note qui sonne sont listés dans le rapport d'import |
| `.wav` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier Wave...`, 16 bits, 24 bits ou 32 bits flottant | Dither, informations de tempo, marqueurs de boucle et marqueurs de la Playlist en option |
| `.mp3` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier MP3...` | Débit constant |
| `.ogg` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier OGG...` | |
| `.flac` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier FLAC...`, compression de 0 à 8 | Sans perte à tous les niveaux |
| `.aif`, `.aiff` | Comme sample ou comme clip audio | `Fichier › Exporter › Fichier AIFF...`, 16 bits ou 24 bits | |
| `.m4a` | – | `Fichier › Exporter › Fichier M4A...` | Export seulement. AAC avec l'encodeur du système sous Windows et macOS ; non proposé sous Linux |
| `.wv` | Comme sample | Non | Samples WavPack, tels que livrés dans les packs de FL Studio |
| `.sf2`, `.sf3` | Joués par LYNX Soundfont | Non | SoundFonts |
| `.dwp`, `.dwb` | Double-clic dans le Browser, un dépôt sur le Channel Rack, ou `Ouvrir un programme / une bank...` de DirectWave | Enregistrement des programmes et banques de DirectWave | Programmes et banques DirectWave. Les samples sont cherchés là où DirectWave les cherche |
| `.gig` | Comme canal DirectWave | Non | Instruments GigaStudio : couches de vélocité, round robin, boucles, accordage et enveloppe d'ampli. Les samples de release, les keyswitches et les samples compressés ne sont pas encore lus |
| `.ncw` | Comme sample ou comme clip audio | Non | Samples compressés de Kontakt. Les instruments Kontakt `.nki` demandent Kontakt lui-même comme plugin ; les bibliothèques protégées ne sont jamais déchiffrées |
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
| `Fichier › Exporter › Pour d'autres applications...` | Un dossier avec les stems, des fichiers MIDI, un set Ableton Live, un projet FL Studio, le mixdown et un README, zippé si vous le voulez |
| `Fichier › Exporter › Os du projet...` | Écrit dans un dossier les presets et partitions des canaux et des effets |

La fenêtre d'export rend le morceau, le pattern actuel ou la sélection de la Playlist.

## En ligne de commande

Sous Windows, le programme est `LYNX.exe`. Sous macOS et Linux, c'est `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` et `--export-flp` acceptent aussi un `.als` en entrée.

## Bon à savoir

- Les fichiers audio de plus de 30 secondes sont lus en flux depuis un cache disque.
- Un `.flp`, `.als`, `.flm`, `.dawproject`, `.band` ou `.blx` n'est jamais écrit quand vous enregistrez. Les commandes d'export écrivent de nouveaux fichiers.
- Les fichiers `.wv` sont en lecture seule.
- Un `.zip` est d'abord extrait dans un dossier, puis le projet qu'il contient est ouvert.

## Voir aussi

- [Importer des projets](/wiki/importing-projects/)
- [Export](/wiki/exporting/)
- [Ligne de commande](/wiki/command-line/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
