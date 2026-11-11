---
source: e767f4482b
---
![La Playlist avec des clips sur des pistes](/assets/img/playlist.webp)

La Playlist est l'endroit où les patterns deviennent un morceau. Elle contient trois sortes de clips sur ses pistes : des clips de pattern, des clips audio et des clips d'automation. Appuyez sur `F5` pour l'afficher ou la masquer, et sur `Enter` pour l'agrandir ou la restaurer.

Le transport a deux modes. Le mode Pattern joue le pattern actuel en boucle. Le mode Song joue la Playlist. Appuyez sur `L` pour passer de l'un à l'autre.

## Placer un pattern dans le morceau

1. Appuyez sur `F5` pour afficher la Playlist.
2. Dans le panneau Pipette, cliquez sur un pattern. La Pipette liste les patterns, l'audio et les automations du projet, et celui sur lequel vous cliquez devient la source des clips.
3. Appuyez sur `P` pour le Crayon.
4. Cliquez sur une piste pour placer le clip. Gardez le bouton enfoncé et glissez pour le positionner.
5. Appuyez sur `B` pour le Pinceau et glissez le long d'une piste pour peindre des clips bout à bout.
6. Appuyez sur `L` pour le mode Song, puis sur `Space` pour lancer la lecture.

Les fichiers audio peuvent être glissés du [Browser](/wiki/browser/) vers la Playlist.

## Outils

![La barre d'outils de la Playlist avec la case d'arrangement, les outils et la case de magnétisme](/assets/img/wiki/playlist-tools.webp)

Les lettres fonctionnent tant que le piano du clavier de saisie (`Ctrl+T`) est désactivé.

| Outil | Touche | Effet |
|---|---|---|
| Crayon | `P` | Un clic place le clip de la Pipette |
| Pinceau | `B` | Un glissé peint des clips bout à bout |
| Gomme | `D` | Un clic ou un glissé supprime |
| Muet | `T` | Un clic ou un glissé coupe ou rétablit le son des clips |
| Couteau | `C` | Glissez une ligne pour couper à travers des clips, un clic coupe à ce point |
| Lasso | `E` | Un glissé dessine un cadre de sélection. `Shift` ajoute |
| Zoom | `Z` | Un clic zoome, glisser une plage zoome dessus, un clic droit dézoome |
| Écouter | `Y` | Un clic ou un glissé lit à partir de ce point |
| Glissement | `S` | Un glissé fait glisser le contenu du clip à l'intérieur du clip |

## Travailler avec les clips

- Glissez un clip pour le déplacer. `Shift`+glissé en fait une copie.
- Glissez la fin d'un clip pour le redimensionner. Glisser le début le rogne pendant que le contenu reste en place.
- Un clic droit supprime un clip. `Ctrl`+glissé sélectionne.
- Double-cliquez sur un clip pour l'ouvrir.
- Le bouton en haut à gauche d'un clip ouvre le menu du clip (rendre unique, renommer et colorer, découper, et pour l'audio : hauteur, adapter au tempo, inverser).
- Maintenez `Alt` en glissant pour ignorer le magnétisme.
- Les clips audio ont des poignées de fondu et un gain de clip que vous glissez vers le haut ou le bas.
- `Shift+M` active l'étirement : redimensionner un clip audio étire alors l'audio au lieu de le couper.

## Pistes

Dans l'en-tête d'une piste, un clic sélectionne, `Ctrl`+clic ajoute ou retire, `Shift`+clic sélectionne une plage et un clic droit ouvre le menu de la piste. `Shift`+molette déplace la piste vers le haut ou le bas. Le voyant de la piste la coupe (clic) ou la met en solo (`Ctrl`+clic ou clic droit). Glissez la séparation entre deux pistes pour en redimensionner une, ou `Shift`+glissé pour toutes les redimensionner.

## Règle, boucle et marqueurs

- Cliquez sur la règle pour fixer la position du morceau.
- Un glissé avec le bouton droit, `Ctrl`+glissé ou un double-clic-glissé sur la règle crée une sélection de temps, qui est aussi la boucle. `Shift`+glissé la déplace.
- `Ctrl+Ins` insère la longueur de la sélection de temps sur toutes les pistes, `Ctrl+Del` la retire.
- `Alt+T` ajoute un marqueur. Cliquez sur un marqueur pour y sauter.
- `Ctrl`+molette zoome, `Alt`+molette change la hauteur des pistes.

## Automation

Faites un clic droit sur un potard ou un fader et choisissez `Créer un clip d'automation`. Le clip apparaît dans la Playlist. Avec le Crayon, glissez ses points et cliquez sur la courbe pour en ajouter un. Les couloirs d'automation sous les pistes sont facultatifs et désactivés par défaut. Voir [Automation](/wiki/automation/).

## Arrangements

La case d'arrangement de la barre d'outils permet de passer d'un arrangement à l'autre. Un projet peut en avoir plusieurs.

## Touches

| Commande | Touche | Effet |
|---|---|---|
| Tout sélectionner | `Ctrl+A` | Sélectionne tous les clips |
| Copier, couper, coller | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copie, coupe ou colle des clips |
| Dupliquer | `Ctrl+B` | Duplique la sélection |
| Supprimer | `Del` | Supprime les clips sélectionnés |
| Suppression avec ripple | `Shift+Del` | Supprime les clips sélectionnés et referme les trous |
| Édition ripple | `Shift+S` | Active ou désactive l'édition ripple |
| Couper le son, rétablir | `Alt+M` / `Shift+Alt+M` | Coupe ou rétablit le son des clips sélectionnés |
| Rendre unique | `Ctrl+Shift+V` | Rend uniques les clips sélectionnés |
| Fusionner | `Ctrl+G` | Fusionne des clips de pattern |
| Étirement | `Shift+M` | Active l'étirement pour redimensionner les clips audio |
| Insérer du temps | `Ctrl+Ins` | Insère la longueur de la sélection de temps sur toutes les pistes |
| Supprimer du temps | `Ctrl+Del` | Retire la sélection de temps sur toutes les pistes |
| Ajouter un marqueur | `Alt+T` | Ajoute un marqueur |
| Zoom | `PageUp` / `PageDown` | Zoome ou dézoome |
| Zoom pour tout voir | `Shift+4` | Zoome pour tout afficher |
| Agrandir | `Enter` | Agrandit ou restaure la Playlist |

## À savoir

- La Playlist ne s'entend qu'en mode Song. En mode Pattern, `Space` joue le pattern actuel, quoi qu'il y ait sur les pistes.
- `Vue › Style de disposition › Flux par lancement de clips` ajoute une grille de clips et de scènes à côté de la Playlist. C'est facultatif. Voir [Dispositions](/wiki/layouts/).
- Avec plusieurs clips d'automation qui se chevauchent sur un même contrôle, le choix de celui qui joue peut différer de celui de FL Studio (notes de version 0.1.25).
- Un tempo déplacé par un contrôleur MIDI lié ne redimensionne pas les clips audio.
- Sous Linux, la vidéo demande FFmpeg.

## Voir aussi

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Enregistrement](/wiki/recording/)
- [Exporter](/wiki/exporting/)
- [Dispositions](/wiki/layouts/)
