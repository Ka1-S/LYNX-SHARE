---
source: 3bf7e0c0ab
---
LYNX SHARE garde le flux de travail que vous connaissez : un Channel Rack, des patterns, une Playlist et un Mixer, avec les mêmes touches. Il ouvre directement les projets `.flp` et peut en réécrire un.

LYNX SHARE est un programme indépendant. Il n'est ni affilié à Image-Line, ni approuvé ou parrainé par Image-Line, et il ne contient aucun code d'Image-Line. FL Studio est une marque déposée d'Image-Line Software.

## Ouvrir un projet .flp

1. Choisissez `Fichier › Ouvrir...` (`Ctrl+O`) et sélectionnez le `.flp`.
2. LYNX SHARE retrouve les plugins et les samples du projet, puis l'ouvre. Le titre affiche « (imported from FL Studio) ».
3. Choisissez `Aide › Last FL Studio import report` pour lire ce qui a été approximé, laissé de côté ou introuvable.
4. Choisissez `Aide › Fichiers manquants...` pour voir les samples et les plugins manquants.
5. Choisissez `Fichier › Enregistrer`. Le programme demande où écrire un fichier `.lynx`. Le `.flp` lui-même n'est jamais écrit.

## Équivalences

| Dans FL Studio | Dans LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Mêmes noms, sur `F5`, `F6`, `F7` et `F9` |
| Sélecteur de plugins | `F8` |
| Browser | `Alt+F8` |
| Mode Pattern / Song | `L` |
| Annuler | `Ctrl+Z` annule, ou rétablit après une annulation. `Ctrl+Alt+Z` annule en arrière |
| Enregistrer une nouvelle version | `Ctrl+Alt+S` |
| Exporter | `Ctrl+R`, MP3 sur `Ctrl+Shift+R`, MIDI sur `Ctrl+Shift+M` |
| Inserts, envois et sidechain du Mixer | Identiques, avec 10 emplacements d'effets par insert |
| Fruity Wrapper | Les plugins tiers sont hébergés directement, voir [Plugins tiers](/wiki/plugins/) |
| Générateurs et effets natifs | Joués par des moteurs que LYNX SHARE a recréés, voir plus bas |
| Fichier de projet `.flp` | Fichier de projet `.lynx` |

`Aide › Raccourcis` liste toutes les touches.

## Ce qui est importé

- Les canaux, les samples, les patterns, les notes slide et porta, tous les arrangements, les marqueurs, les changements de signature rythmique, l'automation et les liens de contrôleurs.
- Le Mixer : routage, niveaux, sidechains, EQ de piste et effets natifs avec leurs réglages enregistrés.
- Les plugins tiers VST3, VST2 et CLAP dans Fruity Wrapper : remplacés par le même plugin installé sur votre ordinateur, avec son état enregistré.

## Plugins natifs de FL Studio

Les générateurs et effets propres à FL Studio ne sont pas chargés. LYNX SHARE les joue avec des moteurs qu'il a recréés, et chacun a un niveau de qualité :

| Niveau | Signification |
|---|---|
| Exact | Identique bit à bit à l'original sur tous les états testés |
| Quasi exact | Identique bit à bit sur la plupart des états, le reste diffère sous environ -100 dB |
| Proche | Le même algorithme, à -60 dB de l'original sur presque tous les états testés |
| Approximatif | Les réglages enregistrés sont reportés sur un synthé ou un effet intégré de LYNX SHARE |
| Substitut | L'état est conservé et le plugin est muet ou laisse passer l'audio |

Le niveau de chaque moteur, et ce qui lui manque encore, se trouve dans [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Voir aussi [Instruments et effets intégrés](/wiki/built-in/).

## Ce qui demande votre propre installation de FL Studio

Ouvrir un `.flp` fonctionne sans FL Studio installé. Une installation sur le même ordinateur ajoute deux choses :

- Les samples d'usine de FL Studio et sa base de données de plugins, de sorte que samples et plugins sont retrouvés sans analyse.
- Les formes d'onde, samples, réponses impulsionnelles et tables intégrés dont certains moteurs ont besoin (FL Keys, Soundgoodizer, Maximus et les autres marqués d'une croix dans PLUGINS.md). Ils sont lus depuis votre installation à l'exécution et ne sont pas fournis avec LYNX SHARE.

Sous Linux, LYNX SHARE cherche un FL Studio Windows sous Wine. Sous macOS, il cherche le bundle de l'app, mais ce n'est pas testé.

## Réexporter en .flp

1. Choisissez `Fichier › Exporter › Projet FL Studio...`.
2. Sélectionnez `Projet FL Studio (.flp)` ou `Paquet zippé (.zip, avec tous les samples)`.
3. Choisissez ce qu'il advient de ce que FL Studio n'a pas : les rendre en audio (même son), ou utiliser le plugin FL le plus proche (reste modifiable, peut sonner différemment, et les instruments propres à LYNX sont laissés de côté).

Le fichier est un projet FL Studio 2026. Les samplers, clips audio, générateurs et effets propres à FL, plugins tiers (dans Fruity Wrapper), patterns, la Playlist, le Mixer et l'automation passent avec leurs réglages. Un rapport liste ce qui est passé à l'identique et ce qui a été rendu, remplacé ou laissé de côté.

## FL Studio Mobile (.flm)

`Fichier › Ouvrir...` accepte aussi les projets `.flm`. L'import est partiel.

- Importés : le tempo, la signature rythmique, un insert du Mixer par canal FLM, une piste de Playlist par couloir, les patterns (chaque passage de boucle en clip), les clips audio avec décalage et hauteur, les kits de batterie, les programmes DW Sampler et `.dwp` et le Slicer (sur les moteurs propres à FLM), les instruments MiniSynth, Transistor Bass, 3x Osc, GMSynth et SuperSaw, les effets de FLM (en effets Pocket), l'automation du niveau, du pan, du mute, du solo, du pitch bend, des envois et des paramètres d'instrument, ainsi que les jeux d'accords et le verrou de tonalité.
- Non importés : les autres instruments de FLM, qui jouent le synthé intégré.
- Les samples sont cherchés à côté du `.flm` et dans la copie par FL Studio des ressources FLM. Un `.dwp` introuvable joue un remplaçant.

## Bon à savoir

- Les packs FLEX sont chiffrés et ne sont jamais ouverts. Un canal FLEX reçoit un patch de synthé choisi d'après le nom de son preset.
- Les modulateurs sont laissés de côté dans un export `.flp`, et le rapport d'export le dit.
- Le rapport d'import liste ce que le synthé intégré a approximé, ce qui n'avait pas d'équivalent, quels plugins ont été trouvés ou ne sont pas installés, quels effets ont été laissés de côté ou reproduits en partie, et combien de samples manquent.
- Un effet tiers non installé est remplacé par un effet LYNX SHARE de même rôle. L'original reste dans le projet.

## Voir aussi

- [Instruments et effets intégrés](/wiki/built-in/)
- [Plugins tiers](/wiki/plugins/)
- [Formats de fichiers](/wiki/file-formats/)
- [Raccourcis](/wiki/shortcuts/)
- [Ce que seul LYNX SHARE fait avec les réglages FL](/why/#fl-settings)
