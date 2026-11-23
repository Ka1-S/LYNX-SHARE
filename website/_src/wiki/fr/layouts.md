---
source: 54ebedc064
---
![La disposition par lancement de clips : une grille de clips et de scènes à gauche, la Pipette et la Playlist à droite](/assets/img/cliplaunch.webp)

`Vue › Style de disposition` propose trois entrées : `Disposition Pattern (par défaut)`, `Disposition par lancement de clips` et `Disposition simple`. Le même choix existe dans `Options › Réglages généraux`, sous Disposition. Le choix est conservé quand vous fermez le programme. Au premier démarrage de LYNX SHARE, une carte de bienvenue demande d'où vous venez et choisit la disposition qui convient.

La disposition Pattern, celle par défaut, fonctionne comme FL Studio : le Channel Rack, les patterns, le Piano Roll, la Playlist et le Mixer. La disposition par lancement de clips, dans le style d'Ableton Live, ajoute une grille de clips avec des scènes à côté de la Playlist, une table de mixage de session et les vues clip et appareil ; les patterns et le Channel Rack restent disponibles. La disposition simple mêle le style accessible en une seule fenêtre de BandLab à la façon de travailler de FL Studio Mobile : des pistes et une timeline, des boucles, des pads et l'enregistrement rapide. Changer de disposition ne déplace que les fenêtres : le projet reste le même.

## Lancer des clips dans la grille

1. Choisissez `Vue › Style de disposition › Disposition par lancement de clips`. La grille de clips apparaît à côté de la Playlist.
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
- Sous la grille, le bouton `Mixer` affiche ou masque la table de mixage de session, comme dans Ableton Live : `Entrée/Sortie` (`Audio depuis`, `Écoute`, `Audio vers`), `Sends`, `Délai de piste`, et pour chaque piste son volume avec son activateur, son solo, son bouton d'armement et son vumètre, plus la bande `Principal`.
- `Grooves`, sous la grille, affiche le Groove Pool. `Appliquer le groove aux notes` écrit un groove dans les notes d'un clip MIDI.
- Les boutons `MIDI` et `Tonalité` lancent le MIDI Map Mode et le Key Map Mode. Voir [Contrôleurs MIDI](/wiki/midi-controllers/).

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

Les clips peuvent être coupés, copiés, collés et dupliqués (`Ctrl+D`) depuis leur menu, et glissés entre les emplacements et vers ou depuis la Playlist. Un pattern ou un sample glissé depuis le Browser, ou des fichiers audio déposés depuis l'extérieur, remplissent l'emplacement où ils arrivent. Le menu d'une scène propose aussi `Dupliquer` et `Capturer et insérer une scène`, qui place les clips en cours de lecture dans une nouvelle scène. `Vue › Tempo et signature rythmique des scènes` affiche un tempo et une signature rythmique pour chaque scène.

Le menu de l'emplacement a aussi deux options pour toute la grille : `Rendre les pistes à la Playlist à l'arrêt du transport` et `Actions suivantes sur toute la grille`.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| Se déplacer | Flèches | Sélectionne un autre emplacement |
| Lancer un clip | `Enter` | Lance le clip sélectionné |
| Lancer une scène | `Ctrl+Enter` | Lance la scène sélectionnée |
| Supprimer un clip | `Del` | Vide l'emplacement sélectionné |
| Draw Mode | `B` | Un glissement dans un couloir d'automation dessine des paliers aussi larges que la grille, ou à main levée avec le magnétisme désactivé ou `Alt` maintenu, comme dans Ableton Live. Aussi dans `Options` |

## Sets Ableton Live

Quand vous ouvrez un set Live, ses clips de session et ses scènes vont dans la grille de clips, avec leurs réglages de lancement. `Fichier › Exporter › Set Ableton Live (.als)` réécrit la grille sous forme de clips de session. `Options › Réglages généraux` a `Clips de session des sets Live`, où `Playlist, après l'arrangement` les place plutôt dans la Playlist.

## La disposition simple

`Vue › Style de disposition › Disposition simple` est une fenêtre unique avec les pistes et la timeline, dans le style de BandLab et de FL Studio Mobile, et convient à un premier morceau. `Vue › Panneau de la piste` affiche sous la timeline un dock pour la piste sélectionnée, avec les onglets `Effets`, `Instrument`, `Clavier` et `Éditeur`. Dans cette disposition, le menu `Aide` propose `Tour de la première chanson` et `Écran d'accueil...`, qui offre un premier morceau en sept étapes, un morceau de démonstration ou un projet vide. Voir [Disposition simple](/wiki/simple-layout/).

`Paroles et notes` et `Correspondance de référence...` du menu `Vue`, et `Voix claire` dans le menu d'un insert du Mixer, existent dans toutes les dispositions. Dans la disposition simple, `Voix claire` est aussi dans le menu d'une piste.

## Revenir en arrière

Choisissez `Vue › Style de disposition › Disposition Pattern (par défaut)`. La disposition des fenêtres que vous aviez dans la disposition Pattern revient, et les couloirs d'automation retrouvent leur réglage d'avant. Les clips de la grille restent dans le projet.

## Bon à savoir

- Une piste qui a lancé un clip reste muette dans la Playlist jusqu'à ce que vous appuyiez sur `Retour à la Playlist`. L'option `Rendre les pistes à la Playlist à l'arrêt du transport` le fait pour vous à chaque arrêt.
- Les nouvelles touches de la grille (`B`, `Q`, `Ctrl+M`, `Ctrl+K` et d'autres) n'agissent ainsi que dans la disposition par lancement de clips. Dans la disposition Pattern, elles gardent le sens qu'elles ont dans FL Studio.
- Une scène en 6/8 joue une mesure de trois temps, parce que la métrique compte des noires.
- Les actions suivantes existent pour les clips, pas pour les scènes.
- Les couloirs d'automation activés à la main dans la disposition par lancement de clips ne sont pas conservés quand vous revenez en arrière.
- Avec la disposition Pattern sélectionnée, rien de la grille n'est chargé.

## Voir aussi

- [Playlist](/wiki/playlist/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
- [Importer des projets](/wiki/importing-projects/)
- [Ce que seul LYNX fait : les dispositions](/why/#workflows)

La troisième entrée est une fenêtre pour un premier morceau. Voir [Disposition simple](/wiki/simple-layout/).
