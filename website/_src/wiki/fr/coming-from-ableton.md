---
source: d5cc625221
---
![La disposition Clip-launch : une grille de clips avec des scènes à côté de la Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE démarre dans le flux par patterns : un Channel Rack, des patterns et une Playlist. Le flux par lancement de clips est facultatif et ajoute une grille de clips avec des scènes à côté de la Playlist. LYNX SHARE lit et écrit les sets `.als`. Il n'est pas affilié à Ableton.

## Ouvrir un set Live

1. Choisissez `Fichier › Ouvrir...` (`Ctrl+O`) et sélectionnez le `.als`. Les sets enregistrés par Live 11 et Live 12 sont lus.
2. Choisissez `Vue › Style de disposition › Flux par lancement de clips` pour travailler avec la grille de clips, où vont les clips Session et les scènes du set.
3. Utilisez le sélecteur `Grid / Both / Song` dans la barre de titre de la Playlist pour afficher la grille, la Playlist, ou les deux.
4. Choisissez `Fichier › Enregistrer`. Il demande où écrire un fichier `.lynx`. Le `.als` n'est pas modifié.

`Vue › Style de disposition › Flux par patterns (par défaut)` revient en arrière. Le même choix se trouve dans `Options › Réglages généraux...`. Les patterns et le Channel Rack restent disponibles dans les deux dispositions.

## Correspondances

| Dans Live | Dans LYNX SHARE |
|---|---|
| Vue Session | La grille de clips du flux par lancement de clips |
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
| Une piste MIDI par canal, avec ses notes en clips MIDI | Les instruments et effets propres à LYNX SHARE. Leurs pistes et leurs notes sont conservées |
| Des pistes audio avec étirement, transposition, gain, fondus et mode de warp | L'automation des paramètres d'effets |
| Groupes, retours et envois | Les routages d'insert à insert |
| Plugins VST2 et VST3 avec leur état | |
| Marqueurs, et automation du volume, du pan et du tempo | |
| La grille de clips en clips Session et scènes, racks et Drum Racks | |

Un rapport après l'export liste ce qui a été laissé de côté.

## À savoir

- Beaucoup d'appareils propres à Live arrivent sous forme d'appareils LYNX SHARE, avec leurs réglages et leur automation : EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift et Operator (sans FM). Les autres sont abandonnés et listés dans le rapport d'import.
- Les Instrument Racks et les Audio Effect Racks arrivent avec leurs chaînes, leurs zones et leurs macros, et un Drum Rack devient le Drum Rack de LYNX SHARE. Les MIDI Effect Racks de Live ne sont pas encore convertis et sont listés dans le rapport d'import.
- Le rapport d'import liste ce qui a été laissé de côté.
- Live rogne les clips audio qui se chevauchent dans un set exporté.
- Les marqueurs de warp d'un set Live sont conservés à l'import et réécrits à l'export.

## Voir aussi

- [Playlist](/wiki/playlist/)
- [Dispositions](/wiki/layouts/)
- [Importer des projets](/wiki/importing-projects/)
- [Formats de fichiers](/wiki/file-formats/)
- [Deux flux de travail dans un seul programme](/why/#workflows)
