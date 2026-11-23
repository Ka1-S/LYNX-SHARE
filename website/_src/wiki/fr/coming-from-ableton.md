---
source: 37920663db
---
![La disposition Clip-launch : une grille de clips avec des scènes à côté de la Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE démarre dans la disposition Pattern : un Channel Rack, des patterns et une Playlist. La disposition par lancement de clips est facultative et ajoute une grille de clips avec des scènes à côté de la Playlist. LYNX SHARE lit et écrit les sets `.als`. Il n'est pas affilié à Ableton.

Cette page convient aussi aux utilisateurs de Bitwig Studio : la grille de clips fonctionne de la même façon, et les fichiers DAWproject (`.dawproject`) de Bitwig s'ouvrent avec `Fichier › Ouvrir...` et sont écrits par `Fichier › Exporter › DAWproject (.dawproject)...`.

## Vos premiers pas

1. **Choisissez la disposition.** Au premier démarrage de LYNX SHARE, une carte de bienvenue demande d'où vous venez. Cliquez sur `Ableton Live ou Bitwig` : la disposition par lancement de clips s'ouvre. Plus tard, `Vue › Style de disposition › Disposition par lancement de clips` y bascule à tout moment.
2. **Remplissez un slot.** Glissez un sample ou un pattern du Browser dans un slot, ou faites un clic droit sur un slot vide et choisissez `Placer un pattern ici`.
3. **Lancez.** Cliquez sur le triangle à gauche d'un clip, ou sur le triangle d'une scène dans la colonne `Scènes` pour lancer toute la ligne. Ou ouvrez l'un de vos sets `.als` (ci-dessous).

Ce qui est différent de Live :

- La Playlist est l'arrangement, et ses pistes sont les colonnes de la grille. Une piste qui a lancé un clip reste sur la grille jusqu'à ce que vous appuyiez sur `Retour à la Playlist`.
- En dehors de la grille, les touches suivent FL Studio. `Ctrl+Z` annule une étape, et rétablit si vous appuyez de nouveau.
- Seuls certains appareils propres à Live sont convertis (voir Bon à savoir). Les plugins tiers sont repris avec leur état.
- La disposition Patterns, la façon de travailler de FL Studio avec un Channel Rack et des patterns, fonctionne sur le même projet.

## Ouvrir un set Live

1. Choisissez `Fichier › Ouvrir...` (`Ctrl+O`) et sélectionnez le `.als`. Les sets enregistrés par Live 11 et Live 12 sont lus.
2. Choisissez `Vue › Style de disposition › Disposition par lancement de clips` pour travailler avec la grille de clips, où vont les clips Session et les scènes du set.
3. Utilisez le sélecteur `Grid / Both / Song` dans la barre de titre de la Playlist pour afficher la grille, la Playlist, ou les deux.
4. Choisissez `Fichier › Enregistrer`. Il demande où écrire un fichier `.lynx`. Le `.als` n'est pas modifié.

`Vue › Style de disposition › Disposition Pattern (par défaut)` revient en arrière. Le même choix se trouve dans `Options › Réglages généraux...`. Les patterns et le Channel Rack restent disponibles dans les deux dispositions.

## Correspondances

| Dans Live | Dans LYNX SHARE |
|---|---|
| Vue Session | La grille de clips de la disposition par lancement de clips |
| Vue Arrangement | La Playlist |
| Pistes | Pistes de la Playlist, qui sont les colonnes de la grille de clips |
| Scènes | Scènes, les lignes de la grille de clips. Une scène peut régler le tempo quand elle est lancée |
| Quantification de lancement | `Global`, `Aucun`, de 8 mesures à 1 mesure, ou une valeur de note de 1/2 à 1/32, par clip si vous le souhaitez |
| Modes de lancement | `Déclencheur`, `Gate`, `Bascule` et `Répétitions` |
| Actions de suite | Action A et Action B, avec une probabilité pour chacune |
| Boutons d'arrêt de clip | Un slot vide peut avoir un bouton d'arrêt |
| Retour à l'Arrangement | `Retour à la Playlist` |
| Modes de warp | `Temps`, `Tons`, `Texture` et `Complexe` dans `Réglages du canal › Étirement temporel` |
| Simpler, Sampler et racks de batterie | Les samplers de LYNX SHARE |
| Set `.als` | Fichier de projet `.lynx` |

## La grille de clips

Un slot contient un clip de pattern ou un clip audio.

- Une piste ne joue qu'un clip à la fois. Un clip lancé remplace celui qui joue.
- Un lancement attend le prochain step de la quantification de lancement.
- `Legato` démarre le nouveau clip à la position qu'avait atteinte le clip remplacé. `Boucle` se règle clip par clip.
- Les scènes peuvent être insérées, supprimées et renommées.
- Une piste qui a lancé un clip reste muette dans la Playlist jusqu'à ce que vous appuyiez sur `Retour à la Playlist`. Une option rend les pistes quand le transport s'arrête.

Les actions de suite s'exécutent après un certain nombre de mesures ou de passages du clip. Les actions sont `Aucune action`, `Arrêter`, `Rejouer`, `Précédent`, `Suiv.`, `Premier`, `Dernier`, `N'importe lequel` et `Autres`. Un interrupteur active ou désactive les actions de suite pour toute la grille.

## Ce qui est importé d'un .als

- Les pistes MIDI, audio, de groupe et de retour, le tempo, la signature, les locators, la boucle, les réglages du mixeur (volume, pan, muet, solo, envois) et leur automation.
- Les plugins VST2 et VST3 avec leur état, si le même plugin est installé. Voir [Plugins tiers](/wiki/plugins/).
- Les clips Session et les scènes, dans la grille de clips, avec leur mode de lancement, leur quantification, leur legato, leur boucle, leurs actions de suite et leurs boutons d'arrêt. Pour les placer plutôt sur la Playlist, une section par scène, modifiez `Clips de session des sets Live` dans `Options › Réglages généraux...`.
- Les marqueurs de warp.

## Exporter un .als

1. Choisissez `Fichier › Exporter › Set Ableton Live (.als)...`.
2. Sous `Enregistrer pour`, choisissez `Live 12` ou `Live 11 (s'ouvre aussi dans Live 12)`.
3. Sous `Échantillons`, copiez-les dans le dossier de projet du set ou référencez-les là où ils se trouvent.
4. Choisissez un dossier. Le set est enregistré sous `<name> Project/<name>.als`.

| Écrit | Non écrit |
|---|---|
| Une piste MIDI par canal, avec ses notes en clips MIDI | Les instruments et effets propres à LYNX SHARE, sauf Helix, qui devient Wavetable. Leurs pistes et leurs notes sont conservées |
| Des pistes audio avec étirement, transposition, gain, fondus et mode de warp | L'automation des paramètres d'effets |
| Groupes, retours et envois | Les routages d'insert à insert |
| Plugins VST2 et VST3 avec leur état | |
| Marqueurs, et automation du volume, du pan et du tempo | |
| La grille de clips en clips Session et scènes, racks et Drum Racks | |

Un rapport après l'export liste ce qui a été laissé de côté.

## À savoir

- Beaucoup d'appareils propres à Live arrivent sous forme d'appareils LYNX SHARE, avec leurs réglages et leur automation : EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable et Meld. Les autres sont abandonnés et listés dans le rapport d'import.
- Les Instrument Racks et les Audio Effect Racks arrivent avec leurs chaînes, leurs zones et leurs macros, et un Drum Rack devient le Drum Rack de LYNX SHARE. L'Arpeggiator de Live devient l'arpégiateur du canal. Pitch, Velocity, Chord, Note Length et les MIDI Effect Racks sont écrits dans les notes.
- Le rapport d'import liste ce qui a été laissé de côté.
- Dans la vue des appareils, un rack affiche en ligne les appareils de ses chaînes, et le mode Macro Map (les boutons `Map` du rack) donne un paramètre d'appareil à une macro, comme dans Live.
- La table de mixage de session a l'interrupteur Solo / Cue de Live, avec une `Cue Out` pour votre casque et un `Volume de pré-écoute`.
- Live rogne les clips audio qui se chevauchent dans un set exporté.
- Les marqueurs de warp d'un set Live sont conservés à l'import et réécrits à l'export.

## Voir aussi

- [Playlist](/wiki/playlist/)
- [Dispositions](/wiki/layouts/)
- [Importer des projets](/wiki/importing-projects/)
- [Formats de fichiers](/wiki/file-formats/)
- [Deux dispositions dans un seul programme](/why/#workflows)
- [Apprendre : tutoriels](/learn/)
