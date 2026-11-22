---
source: b05790879f
---
![La fenêtre d'export avec WAV sélectionné : étendue, queue et fichiers à gauche, format à droite, options de mixage en dessous](/assets/img/wiki/export.webp)

Tout ce qui sort de LYNX SHARE passe par `Fichier › Exporter`. Les formats audio (WAV, MP3, OGG, FLAC, AIFF et M4A) partagent une seule fenêtre d'export. Le MIDI, les projets FL Studio, les sets Ableton Live, les fichiers DAWproject et les projets zippés ont leurs propres entrées dans le même menu.

## Exporter le morceau en WAV

1. Choisissez `Fichier › Exporter › Fichier Wave...`.
2. Laissez `Étendue` sur `Morceau (Playlist)` et `Fichiers` sur `Un fichier (le mixage)`.
3. Choisissez une `Résolution` : 16 bits, 24 bits ou 32 bits (flottant).
4. Réglez `Queue` sur `Laisser le reste` pour que les réverbs et les release s'éteignent après la dernière mesure.
5. Cliquez sur `Exporter...`, choisissez un nom et un dossier, puis attendez le rendu.
6. Une fois terminé, LYNX SHARE indique où est allé le fichier et son niveau de crête.

`Ctrl+R` ouvre la fenêtre avec le dernier format exporté. `Ctrl+Shift+R` l'ouvre sur MP3.

## Que faut-il rendre

| Réglage | Ce qu'il fait |
|---|---|
| `Étendue` | `Morceau (Playlist)`, `Pattern actuel` ou `Sélection de la Playlist` |
| `Lecture` | Combien de fois l'étendue est jouée dans le fichier, de `Une fois` à 16 fois |
| `Queue` | `Couper le reste` s'arrête exactement à la fin. `Laisser le reste` laisse le son s'éteindre. `Replier le reste` mixe la queue au début, pour les boucles |
| `Queue maximale` | 3, 6, 12 ou 30 secondes. La queue s'arrête plus tôt quand le son s'est éteint |
| `Fichiers` | Un fichier, ou des stems (voir plus bas) |

## Format de sortie

Les boutons choisissent le format. Le M4A n'est proposé que là où le système a un encodeur AAC : sous Windows et macOS, pas sous Linux. `Enregistrer aussi` écrit d'autres formats à partir du même rendu, avec le même nom.

| Format | Options |
|---|---|
| WAV | `Résolution` : 16 bits, 24 bits ou 32 bits (flottant). `Dither` : `Désactivé`, `TPDF` ou `TPDF + noise shaping` |
| MP3 | `Débit`, constant |
| OGG | Débit |
| FLAC | `Résolution`, `Compression` de `0 (la plus rapide)` à `8 (le plus petit)`, `Dither`. Sans perte à tous les niveaux |
| AIFF | `Résolution` : 16 bits ou 24 bits. `Dither` |
| M4A | `Débit` : 96, 128, 160 ou 192 kbps. AAC, écrit par l'encodeur du système |

`Fréq. d'échantillonnage` et `Canaux` valent pour tous les formats. `Canaux` vaut `Stéréo`, `Mono (deux côtés mixés)`, `Mono (gauche seul)` ou `Mono (droite seul)`.

## Options de mixage

| Réglage | Ce qu'il fait |
|---|---|
| `Normaliser (crête à 0 dB)` | Monte ou baisse tout le fichier pour que sa crête la plus forte atteigne la pleine échelle |
| `Effets du Master` | Désactivé : les effets de l'insert Master sont contournés |
| `Effets des pistes` | Désactivé : les effets des autres inserts sont contournés |
| `HQ pour tous les plugins` | Les plugins utilisent leur qualité de rendu. Désactivé : l'export utilise la qualité de lecture |
| `Préchauffage des plugins` | Les plugins tiers traitent un instant de silence avant le début de l'export |
| `Désactiver la polyphonie max.` | Lève la limite de voix des canaux, pour qu'aucune note ne soit coupée |
| `Retirer le silence de compensation` | Retire le silence que la compensation de latence des plugins ajoute au début |
| `Informations de tempo`, `Marqueurs de boucle`, `Marqueurs de la playlist` | WAV uniquement : stockent dans le fichier le tempo, la boucle et les marqueurs de la Playlist |
| `Afficher le(s) fichier(s) à la fin` | Ouvre le gestionnaire de fichiers sur le résultat |

## Stems et pistes

![La fenêtre d'export réglée sur FLAC avec un fichier par insert du Mixer, neuf fichiers au total](/assets/img/wiki/export-flac-stems.webp)

`Fichiers` a trois modes de stems :

- `Un par insert (stems)` : un fichier pour chaque insert qui reçoit du son.
- `Un par canal (stems)` : un fichier pour chaque canal qui joue.
- `Un par canal, instruments seuls (sans envois ni bus)`.

Cochez `Et le mixage complet` pour obtenir le mixage à côté des stems. La ligne en bas indique combien de fichiers seront écrits.

Quand le morceau a des sections dans la piste des sections, `Un fichier par section` le découpe aux sections et nomme chaque fichier d'après sa section. Avec les stems, chaque section a son propre jeu.

`Fichier › Exporter › Toutes les pistes de la Playlist` et `Tous les groupes de pistes de la Playlist` écrivent un fichier par piste ou par groupe de la Playlist. Dans le menu du Mixer, `Enregistrement sur disque › Rendre les pistes armées en fichier(s) WAV...` (`Alt+R`) ouvre la fenêtre d'export pour les seuls inserts armés.

## Autres entrées du menu Export

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| `Fichier MIDI...` | `Ctrl+Shift+M` | Écrit les notes dans un fichier MIDI standard (`.mid`) |
| `Masteriser...` | `Ctrl+Alt+M` | Rend le projet et le masterise vers une cible de loudness en LUFS, sur votre ordinateur |
| `Paquet de loops zippé...` | aucune | Le projet et tous les samples qu'il utilise dans un seul `.zip`, comme le paquet de boucles zippé de FL Studio |
| `Fichiers de données du projet...` | aucune | Copie tous les fichiers utilisés par le projet dans un dossier que vous choisissez ; le projet continue d'utiliser ses propres fichiers |
| `Projet FL Studio...` | aucune | Un `.flp`, ou un paquet de boucles zippé avec tous les samples |
| `Set Ableton Live (.als)...` | aucune | Un set pour Live 12 ou Live 11 |
| `DAWproject (.dawproject)...` | aucune | Un projet pour Bitwig Studio, Studio One, Cubase et les autres applications qui lisent DAWproject |
| `Pour d'autres applications...` | aucune | Un dossier avec les stems, des fichiers MIDI, un set Live, un projet FL Studio, le mixdown et un README, zippé si vous le voulez |
| `Os du projet...` | aucune | Presets et partitions des canaux et des effets, dans un dossier |

## Exporter vers FL Studio, Ableton Live et DAWproject

Le `.flp` est un projet FL Studio 2026. Les samplers, clips audio, générateurs et effets propres à FL Studio, plugins tiers, patterns, la Playlist, le Mixer et l'automation passent avec leurs réglages. Pour ce que FL Studio n'a pas, vous choisissez entre `Les rendre en audio (même son)` et `Utiliser le plugin FL le plus proche (reste modifiable, peut sonner différemment)`. Avec le second choix, les instruments propres à LYNX sont laissés de côté. Les modulateurs qui suivent la position du morceau deviennent des clips d'automation : un LFO synchronisé devient le LFO de clip de FL Studio, et les pas, les sources aléatoires et les macros deviennent des points. Les suiveurs, les enveloppes, le keytrack et la vélocité sont laissés de côté. Un rapport liste ce qui a été rendu, remplacé ou laissé de côté. Les détails sont dans [Venir de FL Studio](/wiki/coming-from-fl-studio/).

Le `.als` contient une piste MIDI par canal, des pistes audio, des groupes, des retours, des envois, des racks et des Drum Racks, des plugins VST2 et VST3 avec leur état, des marqueurs, et l'automation du volume, du pan et du tempo. Les instruments et effets propres à LYNX SHARE ne sont pas convertis, mais leurs pistes et leurs notes sont conservées. Helix est l'exception : il devient le Wavetable de Live. L'automation des paramètres d'effets et les routages d'insert à insert sont abandonnés. Les détails sont dans [Venir d'Ableton Live](/wiki/coming-from-ableton/).

Le `.dawproject` contient les pistes, les dossiers, les clips, les notes, l'audio avec ses marqueurs de warp, l'automation, le tempo, les marqueurs, le mixer avec ses envois, et les plugins VST3, VST2 et CLAP avec leur état. Les instruments et effets propres à LYNX SHARE sont écrits de façon que LYNX SHARE puisse les rouvrir. La grille de clips, les autres arrangements et certains détails par note sont laissés de côté, et un rapport les liste.

`Fichier › Enregistrer une copie comme projet FL Studio Mobile (.flm)...` écrit un `.flm` pour FL Studio Mobile et copie les samples dans un dossier `<name> Samples` à côté. Avant d'enregistrer, une fenêtre liste ce que la copie perd, comme pour `Fichier › Enregistrer une copie pour un ancien LYNX...`. Le `.flm` garde les notes et les patterns (FL Studio Mobile a un pattern par piste), le tempo, les temps par mesure, la tonalité et le jeu d'accords, les niveaux, le panoramique, le mute et le solo du mixer et les envois vers des bus, les instruments propres à FL Studio Mobile (MiniSynth, SuperSaw, GMSynth et Transistor Bass, ainsi que le MiniSynth et le Transistor Bass de FL Studio), les samplers de batterie sous forme de drum kit, les Layers sous forme de rack à plusieurs instruments, chaque effet Pocket (FLM) avec son mix, et les clips audio avec leur niveau, panoramique, hauteur et inversion. Il écrit aussi le DW Sampler (avec ses zones) et 3x Osc, ainsi que des clips d'automation du niveau et du panoramique du mixer, des effets Pocket (FLM) et des valeurs propres au DW Sampler et à GMSynth. Les autres instruments (pour l'instant aussi le Slicer et le SoundFont Player de FL Studio Mobile) gardent leurs notes sur le MiniSynth de FL Studio Mobile, et les effets sans module FLM ainsi que l'automation d'autres cibles, comme les boutons propres aux synthés et les plugins tiers, sont laissés de côté ; tout est listé.

## Bon à savoir

- `Dither` est grisé pour le MP3, l'OGG, le M4A et le WAV 32 bits, et les options de tempo et de marqueurs sont grisées pour tous les formats sauf WAV.
- Avec `Sélection de la Playlist` et aucune sélection dans la Playlist, tout le morceau est rendu. La fenêtre vous prévient.
- Si l'insert Master est muet, la fenêtre prévient que le fichier sera silencieux.
- `Écrire aussi une vidéo avec ce mixage` n'apparaît que si le projet a une piste vidéo, et il faut FFmpeg.
- `Rendre via le matériel en temps réel` n'apparaît que si le projet a un insert matériel actif. Cet export dure aussi longtemps que le morceau.
- Render to audio, qui convertit un insert en clip audio, joue environ 5 dB plus fort que l'insert qu'il remplace dans cette version.

## Voir aussi

- [Formats de fichiers](/wiki/file-formats/)
- [Ligne de commande](/wiki/command-line/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
