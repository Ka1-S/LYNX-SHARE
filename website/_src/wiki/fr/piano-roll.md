---
source: a73763934f
---
![Le Piano Roll](/assets/img/pianoroll.webp)

Le Piano Roll affiche les notes d'un canal dans le pattern actuel, avec un clavier à gauche et un couloir de valeurs par note en bas. Appuyez sur `F7` pour l'afficher ou le masquer, ou choisissez `Piano Roll` dans le menu d'un canal du [Channel Rack](/wiki/channel-rack/).

La liste des canaux dans la barre d'outils choisit le canal que vous éditez. Les canaux qui ont des notes dans ce pattern sont marqués. La molette sur la liste parcourt les canaux qui ont des notes, `Ctrl`+molette les parcourt tous.

## Écrire quelques notes

1. Dans le Channel Rack, faites un clic droit sur le nom d'un canal et choisissez `Piano Roll`.
2. Appuyez sur `P` pour le Crayon.
3. Cliquez dans la grille pour ajouter une note. Gardez le bouton enfoncé et glissez pour la placer.
4. Faites glisser la fin de la note pour l'allonger ou la raccourcir.
5. Faites un clic droit sur une note pour la supprimer.
6. Appuyez sur `Space` pour écouter le pattern.

## Outils

![La barre d'outils du Piano Roll avec la liste des canaux, les outils et la liste du magnétisme](/assets/img/wiki/pianoroll-tools.webp)

Chaque outil a une touche-lettre. Les lettres fonctionnent quand le clavier-piano de saisie (`Ctrl+T`) est désactivé.

| Outil | Touche | Ce qu'il fait |
|---|---|---|
| Crayon | `P` | Un clic ajoute une note, glisser la place |
| Pinceau | `B` | Glisser peint une série de notes. Avec `Shift`, une à la fois |
| Pinceau de batterie | `N` | Un coup par cellule de la grille sur la touche sous la souris |
| Gomme | `D` | Cliquer ou glisser supprime |
| Muet | `T` | Cliquer ou glisser rend muettes ou réactive des notes |
| Couteau | `C` | Tracez une ligne pour couper à travers des notes, un clic coupe à cet endroit |
| Lasso | `E` | Glisser dessine un cadre de sélection. `Shift` ajoute |
| Zoom | `Z` | Un clic zoome, glisser une plage zoome dessus, le clic droit dézoome |
| Écoute | `Y` | Cliquer ou glisser joue les notes sous le curseur |

## Travailler avec les notes

- Faites glisser une note pour la déplacer. `Shift`+glisser en fait une copie.
- Faites glisser la fin d'une note pour la redimensionner. Le début peut aussi être glissé, la fin restant en place.
- Le clic droit supprime une note, avec n'importe quel outil.
- Double-cliquez sur une note pour ouvrir ses propriétés.
- `Alt`+molette sur une note change sa vélocité.
- `Ctrl`+glisser sélectionne. `Ctrl`+clic ajoute une note à la sélection ou l'en retire.
- Maintenez `Alt` en glissant pour ignorer le magnétisme. `Backspace` désactive et réactive le magnétisme.
- Avec plusieurs notes sélectionnées, la poignée d'étirement les étire ou les comprime dans le temps.

## Clavier et règle

Cliquez sur une touche du clavier pour l'entendre. Plus à droite sur la touche, plus c'est fort. `Ctrl`+clic sur une touche sélectionne ses notes. Glisser avec le bouton du milieu sur le clavier agrandit ou réduit la hauteur des touches. La molette de zoom vertical fait de même : c'est le petit carré au bout de la barre de défilement du temps. Glissez-la vers le haut ou le bas, ou utilisez la molette de la souris dessus.

`Vue › Style du clavier` dans le menu du Piano Roll permet de choisir entre `Touches plates` et `Touches de piano (lignes entre les touches blanches)`, qui dessine un clavier traditionnel.

Cliquez sur la règle pour fixer la position. `Ctrl`+glisser, glisser avec le clic droit ou double-clic puis glisser sur la règle sélectionne les notes d'une plage de temps. `Ctrl`+molette zoome et `Shift`+molette fait défiler.

## Barre d'outils

| Réglage | Ce qu'il fait |
|---|---|
| Magnétisme | « Principal » suit le magnétisme de la barre d'outils principale, ou choisissez-en un pour cette fenêtre. |
| Accord | Un clic crée l'accord choisi, avec la note cliquée comme fondamentale. `Shift+N` revient aux notes seules. La même liste est `Tampon d'accord` dans le menu du Piano Roll. |
| Gamme | Met en évidence les notes d'une gamme. Un bouton séparé cale les notes nouvelles et déplacées, ainsi que le MIDI entrant, sur la gamme choisie. Les notes déplacées avancent alors par degrés de la gamme, si bien qu'un accord reste dans la gamme, et `Up` et `Down` déplacent d'un degré. |
| Slide (`S`) | Les nouvelles notes sont des notes slide. La note qui sonne glisse vers elles (glide 808). |
| Notes fantômes (`Alt+V`) | Affiche les notes des autres canaux derrière les vôtres. |
| Couleur de note | Les nouvelles notes prennent cette couleur. Il y en a 16. |
| Suivre | La vue défile avec la tête de lecture pendant la lecture du pattern. |
| Liste du couloir | Choisit ce que le couloir du bas édite : Vélocité, Pan, Hauteur fine, Release, Mod X, Mod Y, Chance ou Plage de vélocité. |

## Chance et plage de vélocité

Chaque note a une Chance et une Plage de vélocité. La Chance indique la probabilité que la note joue à chaque passage. La Plage de vélocité laisse sa vélocité varier entre la vélocité de la note et cette vélocité plus la plage. Choisissez `Probabilité` ou `Plage de vélocité (LYNX)` dans la liste du couloir pour les éditer. `Outils › Probabilité (LYNX)` dans le menu du Piano Roll regroupe les notes sélectionnées de façon que toutes jouent ou qu'une seule joue, et propose `Réinitialiser probabilité et plage de vélocité`.

## Outils de notes

Le menu `Outils` du Piano roll ouvre les outils de notes de FL Studio sous forme de boîtes de dialogue. Ils agissent sur les notes sélectionnées, ou sur toutes les notes quand aucune n'est sélectionnée. Quelques-uns :

| Outil | Touche | Ce qu'il fait |
|---|---|---|
| Limit | `Alt+K` | Déplace les notes par octaves dans une plage de touches, puis les limite. Sous `Restriction de tonalité / gamme`, il garde les notes sur une touche de base et l'un des accords ou l'une des gammes de FL Studio, avec `Magnétisme` réglé sur `Au-dessus`, `En dessous` ou `Alterner`. `Replier vers le haut / le bas` et `Transposer` règlent où vont les notes hors de la plage |
| Flip | `Alt+Y` | `Inverser horizontalement` met les notes en miroir dans le temps. Avec `Conserver les temps de début`, activé au départ, les temps de début sont mis en miroir et les notes gardent leur durée. `Inverser verticalement` met les touches en miroir autour du milieu de leur plage |
| Randomise | `Alt+R` | Changements aléatoires de vélocité, de pan, de hauteur fine, de timing et de durée, et de Mod X, Mod Y et release |
| Scale levels | `Alt+X` | Met à l'échelle un niveau de note du début à la fin de la sélection, avec `Décalage`, `Centre` (la valeur autour de laquelle il met à l'échelle) et `Tension` |
| LFO | `Alt+O` | Écrit une forme de LFO dans un niveau de note. Faites un clic droit sur un curseur `Période` pour une liste de 1 pas à 512 pas |

## Outils MIDI et scripts

`Outils › Outils MIDI...` dans le menu du Piano Roll ouvre un panneau avec Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp et Velocity Shaper.

La colonne `Scripts` du menu `Outils` liste les scripts du Piano Roll : ceux de LYNX SHARE et les fichiers `.pyscript`, y compris ceux des dossiers de scripts de FL Studio si FL Studio est installé. `Ouvrir le dossier des scripts...` ouvre le dossier de vos propres scripts, et `Relancer le dernier script` répète le dernier.

## Touches

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| Tout sélectionner | `Ctrl+A` | Sélectionne toutes les notes |
| Désélectionner | `Ctrl+D` ou `Esc` | Efface la sélection |
| Copier, couper, coller | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copie, coupe ou colle des notes |
| Dupliquer | `Ctrl+B` | Duplique la sélection |
| Supprimer | `Del` | Supprime la sélection |
| Transposer | `Up` / `Down` | Déplace la sélection d'un demi-ton |
| Transposer d'une octave | `Ctrl+Up` / `Ctrl+Down` | Déplace la sélection d'une octave |
| Décaler | `Left` / `Right` | Déplace la sélection d'un pas de grille |
| Quantification rapide | `Ctrl+Q` | Quantifie les débuts et les durées |
| Quantifier les débuts | `Shift+Q` | Quantifie seulement les débuts |
| Hacher | `Ctrl+U` | Hache les notes sur la grille |
| Coller ensemble | `Ctrl+G` | Joint les notes qui se touchent |
| Legato | `Ctrl+L` | Fait durer chaque note jusqu'à la suivante |
| Muet, réactiver | `Alt+M` / `Shift+Alt+M` | Rend muette ou réactive la sélection |
| Importer un MIDI | `Ctrl+M` | Importe un fichier MIDI dans ce canal |
| Exporter en MIDI | `Ctrl+Shift+M` | Exporte le pattern en fichier MIDI |
| Zoom | `PageUp` / `PageDown` | Zoome ou dézoome |

Les outils de notes (Arpégiateur, Strum, Retourner, Rendre aléatoire, Quantifier, Hacher et d'autres) ouvrent des boîtes de dialogue. Leurs touches sont sur la page [Raccourcis](/wiki/shortcuts/).

## Bon à savoir

- Le Piano Roll montre un canal dans un pattern. Changez le pattern actuel et il montre les notes de ce pattern.
- Les lettres d'outils ne font rien tant que le clavier-piano de saisie (`Ctrl+T`) est actif.
- Le clic droit supprime avec tous les outils, inutile donc de passer à la Gomme pour une seule note.
- `S` est ici l'interrupteur Slide. Dans la Playlist, la même lettre est l'outil Glissement.
- Des notes qui ne sont pas de simples steps (autres touches, notes plus longues, slides) font afficher à la ligne du canal, dans le Channel Rack, une miniature à la place des boutons de step.

## Voir aussi

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Raccourcis](/wiki/shortcuts/)
- [Formats de fichiers](/wiki/file-formats/)
