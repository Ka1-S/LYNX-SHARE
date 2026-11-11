---
source: 1c353ba51c
---
![Le flux par lancement de clips : une grille de clips et de scènes à gauche, la Pipette et la Playlist à droite](/assets/img/cliplaunch.webp)

`Vue › Style de disposition` propose deux entrées dans la version 0.1.25 : `Flux par patterns (par défaut)` et `Flux par lancement de clips`. Le même choix existe dans `Options › Réglages généraux`, sous Flux de travail. Le choix est conservé quand vous fermez le programme.

Le flux par patterns, c'est le Channel Rack, les patterns et la Playlist. Le flux par lancement de clips ajoute une grille de clips avec des scènes à côté de la Playlist. Les patterns et le Channel Rack restent disponibles.

## Lancer des clips dans la grille

1. Choisissez `Vue › Style de disposition › Flux par lancement de clips`. La grille de clips apparaît à côté de la Playlist.
2. Faites un clic droit sur un emplacement vide, choisissez `Placer un pattern ici` et sélectionnez un pattern. Un double-clic sur un emplacement vide y place le pattern actuel.
3. Cliquez sur le triangle à gauche du clip pour le lancer. Si le transport est arrêté, il démarre.
4. Cliquez sur le triangle d'une scène, dans la colonne `Scènes`, pour lancer tous les clips de cette ligne.
5. Cliquez sur `Arrêter tous les clips` pour les arrêter.
6. Cliquez sur `Retour à la Playlist` pour que les pistes rejouent leurs clips de Playlist.

## Ce qui change à l'écran

- La grille de clips s'affiche à côté de la Playlist. Les pistes de la Playlist sont les colonnes et les scènes sont les lignes.
- La barre de titre de la Playlist gagne un sélecteur `Les deux`, `Grille`, `Song`. `Grille` n'affiche que la grille, `Song` que la Playlist.
- La première fois, le Channel Rack est masqué. `F6` ou son bouton de la barre d'outils l'affiche.
- Les couloirs d'automation de la Playlist sont activés.
- La disposition des fenêtres est mémorisée par style, de sorte que chaque style revient comme vous l'avez laissé.

## La grille

Un clip lancé ne démarre pas tout de suite. Il attend le prochain point de quantification de lancement, et son triangle clignote jusque-là. La liste en bas de la grille règle la quantification de lancement globale, utilisée par tous les clips réglés sur `Global`. Un arrêt attend de la même façon.

Faites un clic droit sur un clip pour ses réglages.

| Réglage | Ce qu'il fait |
|---|---|
| `Quantification de lancement` | Quand ce clip démarre : `Global`, `Aucun`, de `8 mesures` jusqu'à `1/32` |
| `Mode de lancement` | `Déclencheur`, `Gate`, `Bascule` ou `Répétitions` |
| `Legato (garder la position)` | Le clip reprend la position de lecture du clip joué avant lui sur cette piste |
| `Boucle` | Actif : le clip se répète. Désactivé : il joue une fois, puis la piste s'arrête |
| `Action suivante` | Ce qui se passe après le clip : deux actions avec une probabilité chacune, et le délai après lequel elles agissent |
| `Delete` | Vide l'emplacement |

Faites un clic droit sur un emplacement vide pour `Bouton d'arrêt (une scène arrête cette piste ici)`. Un clic sur le carré à gauche d'un emplacement vide arrête cette piste. Faites un clic droit sur une scène pour `Insérer une scène`, `Ajouter une scène en dessous`, `Supprimer la scène`, `Renommer la scène...` et le tempo de la scène.

Le menu de l'emplacement a aussi deux options pour toute la grille : `Rendre les pistes à la Playlist à l'arrêt du transport` et `Actions suivantes sur toute la grille`.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| Se déplacer | Flèches | Sélectionne un autre emplacement |
| Lancer un clip | `Enter` | Lance le clip sélectionné |
| Lancer une scène | `Ctrl+Enter` | Lance la scène sélectionnée |
| Supprimer un clip | `Del` | Vide l'emplacement sélectionné |

## Sets Ableton Live

Quand vous ouvrez un set Live, ses clips de session et ses scènes vont dans la grille de clips, avec leurs réglages de lancement. `Fichier › Exporter › Set Ableton Live (.als)` réécrit la grille sous forme de clips de session. `Options › Réglages généraux` a `Clips de session des sets Live`, où `Playlist, après l'arrangement` les place plutôt dans la Playlist.

## Revenir en arrière

Choisissez `Vue › Style de disposition › Flux par patterns (par défaut)`. La disposition des fenêtres que vous aviez dans le flux par patterns revient, et les couloirs d'automation retrouvent leur réglage d'avant. Les clips de la grille restent dans le projet.

## Bon à savoir

- Une piste qui a lancé un clip reste muette dans la Playlist jusqu'à ce que vous appuyiez sur `Retour à la Playlist`. L'option `Rendre les pistes à la Playlist à l'arrêt du transport` le fait pour vous à chaque arrêt.
- Dans la version 0.1.25, un emplacement se remplit depuis son menu ou par double-clic. Vous ne pouvez pas glisser un clip ou un fichier dans un emplacement.
- Les actions suivantes existent pour les clips, pas pour les scènes.
- Les couloirs d'automation activés à la main dans le flux par lancement de clips ne sont pas conservés quand vous revenez en arrière.
- Avec le flux par patterns sélectionné, rien de la grille n'est chargé.

## Voir aussi

- [Playlist](/wiki/playlist/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
- [Importer des projets](/wiki/importing-projects/)
- [Ce que seul LYNX fait : les flux de travail](/why/#workflows)

Une disposition simplifiée pour débutants n'existe pas dans la version 0.1.25. Voir [Disposition simple](/wiki/simple-layout/).
