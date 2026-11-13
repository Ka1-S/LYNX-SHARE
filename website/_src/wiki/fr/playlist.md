---
source: 1b54c6493f
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
- Le menu d'un clip audio suit la disposition de FL Studio sur deux colonnes. La colonne de gauche a la section `Clip audio` (`Pré-écoute`, `Muet`, `Renommer et colorer...`, `Changer la couleur...`, `Couleur aléatoire`, `Changer l'icône...`, `Sélectionner le canal source`, `Réglages du canal`, `Rendre unique`, `Sélectionner tous les clips similaires`, `Delete`, `Propriétés...`) et la section `Région` (`Sélectionner une région`, `Hacher`). La colonne de droite a `Sample` (`Rendre unique comme échantillon`, `Éditer l'échantillon`, `Extraire les stems du sample`, `Détecter le tempo`, `Caler sur le tempo`), `Automation` (`Automatiser › Panoramique` ou `Volume`, `Fondu enchaîné avec`) et `Fondus` (`Réinitialiser les fondus`). Les éléments propres à LYNX SHARE, comme `Scinder ici`, `Inverser`, `Normaliser` et `Convertir en MIDI`, viennent en dernier, sous une ligne.
- `Automatiser › Panoramique` ou `Volume` crée un clip d'automation aussi long que le clip audio, sur sa piste, pour le pan ou le volume du réglage Levels du canal.
- Maintenez `Alt` en glissant pour ignorer le magnétisme.
- Les clips audio ont des poignées de fondu et un gain de clip que vous glissez vers le haut ou le bas. Faites un clic droit sur une poignée de fondu pour son menu, avec `Créer un fondu enchaîné`. Faites un clic droit sur la poignée de gain pour `Réinitialiser le gain` et `Normaliser`.
- `Shift+M` active l'étirement : redimensionner un clip audio étire alors l'audio au lieu de le couper.
- `Shift+Left` et `Shift+Right` déplacent les clips sélectionnés d'un pas de magnétisme, `Shift+Up` et `Shift+Down` d'une piste. `Alt+Left` et `Alt+Right` les déplacent d'un pixel d'écran au zoom actuel, sans magnétisme. Sans clip sélectionné, tous les clips bougent.
- `Rendre unique comme échantillon` dans le menu d'un clip audio donne au clip un nouveau fichier qui contient ce qu'il jouait, avec sa hauteur, son étirement et son inversion déjà dans l'audio.
- Un clip audio qui a des marqueurs de warp les affiche sous forme de petits drapeaux sur son bord supérieur. Glissez un drapeau sur le côté pour déplacer le marqueur.

## Menus Edit et Select

Le menu de la Playlist a un sous-menu `Éditer` et un sous-menu `Sélectionner`. `Éditer` contient les commandes de clips : couper, copier et coller, `Décaler à gauche`, `Décaler à droite`, `Décaler vers le haut`, `Décaler vers le bas`, `Rendre unique`, `Rendre unique comme échantillon`, `Insérer de l'espace`, `Supprimer l'espace`, les commandes de fusion et les options de fondu. `Sélectionner` propose `Sélectionner par la source sélectionnée` (`Shift+C`), `Inverser la sélection` (`Shift+I`), `Désélectionner` (`Ctrl+D`), les clips muets et les clips qui se chevauchent, et `Lasso magique`. Avec `Lasso magique` activé, déplacez la souris en boucle autour des clips, sans appuyer sur un bouton, pour les sélectionner.

## Pistes

Dans l'en-tête d'une piste, un clic sélectionne, `Ctrl`+clic ajoute ou retire, `Shift`+clic sélectionne une plage et un clic droit ouvre le menu de la piste. `Shift`+molette déplace la piste vers le haut ou le bas. Le voyant de la piste la coupe (clic) ou la met en solo (`Ctrl`+clic ou clic droit). Glissez la séparation entre deux pistes pour en redimensionner une, ou `Shift`+glissé pour toutes les redimensionner.

`Vue` dans le menu de la Playlist choisit ce qu'affichent les en-têtes de piste : `Afficher les contrôles sur les pistes audio`, `Afficher les niveaux sur les pistes audio` et `Afficher les niveaux sur les pistes d'instrument`.

`Mode de piste`, dans le menu de la piste, fait d'une piste une piste `Non assignée`, une `Piste audio` ou une `Piste d'instrument`, comme dans FL Studio. `Piste audio` liste d'abord `Nouvelle(s) piste(s) de mixage` puis chaque insert du Mixer par numéro et par nom. Le choix s'applique à toutes les pistes sélectionnées. `Piste d'instrument` propose `Utiliser un canal existant`, `Plus de plugins...` et la liste des instruments du Channel rack.

Le `+` en haut à gauche des en-têtes de piste ouvre les mêmes choix pour une nouvelle piste : `Ajouter une piste audio`, `Plus de plugins...` et les instruments. Un clic droit sur `+` ajoute tout de suite une piste audio.

## Blocs de pattern

Avec `Blocs de pattern (projet actuel)` activé dans le menu de la Playlist, une section sous les pistes a une ligne par pattern. Là, le Crayon place un bloc et le fait glisser le long de sa ligne, le Pinceau peint des blocs et la Gomme les supprime. Le clic droit efface avec n'importe quel outil.

## Règle, boucle et marqueurs

- Cliquez sur la règle pour fixer la position du morceau.
- Un glissé avec le bouton droit, `Ctrl`+glissé ou un double-clic-glissé sur la règle crée une sélection de temps, qui est aussi la boucle. `Shift`+glissé la déplace.
- `Ctrl+Ins` insère la longueur de la sélection de temps sur toutes les pistes, `Ctrl+Del` la retire.
- `Alt+T` ajoute un marqueur. Cliquez sur un marqueur pour y sauter.
- `Ctrl`+molette zoome, `Alt`+molette change la hauteur des pistes.

## Automation

Faites un clic droit sur un potard ou un fader et choisissez `Créer un clip d'automation`. Le clip apparaît dans la Playlist. Avec le Crayon, glissez ses points et cliquez sur la courbe pour en ajouter un. Les couloirs d'automation sous les pistes sont facultatifs et désactivés par défaut. Voir [Automation](/wiki/automation/).

Faites un clic droit sur un point pour son menu. Sa liste `Mode` propose les 13 modes de courbe de FL Studio pour le segment qui se termine à ce point : `Lisser`, `Hold`, trois courbes simples, trois courbes doubles, `Demi-sinus`, `Escalier`, `Escalier doux`, `Impulsion` et `Onde`. Un nouveau point prend le dernier mode utilisé. Les modes sont lus depuis les fichiers .flp et écrits dedans.

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
| Décaler des clips | `Shift+Left` / `Shift+Right` | Déplace les clips sélectionnés d'un pas de magnétisme |
| Décaler vers une autre piste | `Shift+Up` / `Shift+Down` | Déplace les clips sélectionnés d'une piste |
| Ajuster des clips | `Alt+Left` / `Alt+Right` | Déplace les clips sélectionnés d'un pixel d'écran, sans magnétisme |
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
- Comme dans FL Studio, un pattern ne joue que depuis un clip à la fois. Quand un clip suivant du même pattern démarre, sur n'importe quelle piste, le clip précédent ne lance plus de notes à partir de là. Les clips de patterns différents jouent ensemble. Cela vaut dans les nouveaux projets et les projets FL Studio. Un projet .lynx enregistré avant la 0.1.27 continue de jouer chaque clip en entier.
- Un tempo déplacé par un contrôleur MIDI lié ne redimensionne pas les clips audio.
- Sous Linux, la vidéo demande FFmpeg.

## Voir aussi

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Enregistrement](/wiki/recording/)
- [Exporter](/wiki/exporting/)
- [Dispositions](/wiki/layouts/)
