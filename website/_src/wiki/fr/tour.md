---
source: edf15b371e
---
![La fenêtre principale avec le projet de démonstration : barre d'outils en haut, Browser à gauche, Playlist derrière et Channel Rack flottant par-dessus](/assets/img/wiki/desktop.webp)

LYNX SHARE n'a qu'une fenêtre principale. Une barre d'outils court le long du haut. En dessous se trouve un bureau qui contient cinq fenêtres : le Browser, la Playlist, le Channel Rack, le Piano Roll et le Mixer. Au premier démarrage, le Browser est à gauche, la Playlist remplit le reste et le Channel Rack flotte par-dessus. Le Piano Roll et le Mixer sont masqués jusqu'à ce que vous les appeliez.

## S'y retrouver

1. Pointez la souris sur n'importe quel contrôle. La barre d'aide sous les menus dit ce qu'il fait et quels boutons de souris et quelles touches il accepte.
2. Appuyez sur `F6` pour masquer le Channel Rack, et de nouveau sur `F6` pour l'afficher.
3. Appuyez sur `F9` pour afficher le Mixer, puis sur `F7` pour le Piano Roll.
4. Glissez une fenêtre par sa bande de titre pour la déplacer. Double-cliquez sur le titre pour l'agrandir, et de nouveau pour la restaurer.
5. Choisissez `Vue › Réinitialiser la disposition` pour tout remettre en place.

## La barre d'outils

![La barre d'outils : menus et barre d'aide, transport, potards master, options d'enregistrement, magnétisme et pattern, boutons de fenêtres, boutons de fichiers, affichage des accords, vumètres et moniteur de sortie](/assets/img/wiki/toolbar-1920.webp)

De gauche à droite, avec la disposition par défaut :

| Groupe | Contenu |
|---|---|
| Menu et barre d'aide | Les menus `Fichier`, `Éditer`, `Ajouter`, `Patterns`, `Vue`, `Options`, `Outils` et `Aide`, avec la barre d'aide en dessous. Toujours en premier |
| Transport | Les boutons de mode `Pattern` et `Song`, Lecture, Arrêt et Enregistrement, le tempo en BPM et la position en mesure, temps et step |
| Volume et hauteur master | Le potard de volume `MASTER` et le potard `HAUTEUR`, en cents |
| Options d'enregistrement | Métronome, décompte, enregistrement en boucle, édition step et les autres interrupteurs d'enregistrement |
| Magnétisme et pattern | Le magnétisme principal (`Ligne (suit le zoom)` par défaut), le pattern actuel, un bouton de nouveau pattern et le menu des patterns |
| Raccourcis des fenêtres | Un bouton par fenêtre, et le sélecteur de plugins |
| Fichier et annulation | Nouveau, Ouvrir, Enregistrer, Exporter, Annuler et Rétablir |
| Détection d'accords | Nomme l'accord joué sur un périphérique MIDI ou au clavier de saisie |
| CPU, mémoire et polyphonie | Charge CPU audio, mémoire utilisée et nombre de voix |
| Moniteur de sortie | Un oscilloscope de la sortie. Un clic parcourt ses autres modes |

Il existe aussi un groupe nommé `Multilien vers les contrôleurs`, masqué par défaut.

`Options › Barre d'outils` modifie la barre d'outils : `Modifier la barre d'outils (glissez les panneaux pour les déplacer, les ajouter ou les retirer)`, `Séparateurs`, `Masquer les panneaux de la barre d'outils`, `Presets`, un interrupteur par groupe, et `Taille des icônes` (petite, moyenne ou grande).

## Transport

| Contrôle | Touche | Effet |
|---|---|---|
| Mode Pattern | `L` | Joue le pattern actuel en boucle. Un clic droit sur le bouton affiche le Channel Rack |
| Mode Song | `L` | Joue l'arrangement de la Playlist. Un clic droit sur le bouton affiche la Playlist |
| Lecture | `Space` | Lance la lecture. Pressé de nouveau, il arrête et revient au point de départ. `Ctrl+Space` met en pause |
| Arrêt | | Arrête et revient au début. Un double-clic envoie aussi all notes off |
| Enregistrement | `R` | Active ou désactive l'enregistrement. Voir [Enregistrement](/wiki/recording/) |

## La barre d'aide

Au repos, la barre d'aide affiche `Point at anything for help`, avec `F1` pour la fiche des raccourcis, `F5` à `F9` pour les fenêtres et `Space` pour lecture et arrêt. Au-dessus d'un contrôle, elle le nomme et liste ce que font le glissé, `Ctrl`, `Alt` et le clic droit à cet endroit. Les longues tâches comme l'analyse des plugins en arrière-plan y indiquent leur progression.

## Les cinq fenêtres

| Fenêtre | Touche | À quoi elle sert |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Arranger des clips de pattern, d'audio et d'automation en un morceau |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Une ligne par instrument, avec les steps du pattern actuel |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Les notes d'un canal dans le pattern actuel |
| [Mixer](/wiki/mixer/) | `F9` | Des inserts avec faders, effets et routage |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, presets, le contenu du projet et la base de plugins |

Chaque touche affiche la fenêtre et lui donne le focus, ou la masque si elle l'a déjà. `F8` seul ouvre le sélecteur de plugins, qui ne fait pas partie des cinq. Les boutons de fenêtres de la barre d'outils font la même chose que les touches. Un clic droit sur l'un d'eux affiche cette fenêtre agrandie. `Enter` agrandit ou restaure la Playlist.

## Comment les fenêtres bougent et s'ancrent

Chaque fenêtre a une bande de titre. À sa gauche se trouve un bouton de menu avec les options propres à la fenêtre, puis `Détaché (fenêtre propre)`, `Agrandi` et `Fermer`. À sa droite se trouvent les boutons d'agrandissement et de fermeture.

- Glissez la bande de titre pour déplacer une fenêtre. Elle s'aimante aux autres fenêtres et aux bords. Maintenez `Alt` pour déplacer sans aimantation.
- Glissez un bord pour redimensionner.
- `Détaché (fenêtre propre)` transforme la fenêtre en fenêtre système séparée, pour un second écran. Une fenêtre détachée a un bouton `Ancrer` qui la remet en place. `Vue › Panneaux détachés (fenêtre propre)` liste les cinq.
- `Vue › Ramener les panneaux hors écran` récupère une fenêtre qui s'est retrouvée hors du bureau.
- `Vue › Enregistrer la disposition sous...` enregistre la disposition sous un nom, et `Vue › Charger une disposition` en ramène un.

Le Browser est l'arrière-plan du bureau : il reste ancré sur le côté et les autres fenêtres restent toujours au-dessus de lui.

## À savoir

- Tant que vous ne déplacez, ne redimensionnez ni ne détachez rien, les fenêtres reprennent leurs places par défaut chaque fois que la fenêtre principale change de taille. Ensuite, votre disposition est mémorisé et suit la taille de la fenêtre.
- `F12` ferme les fenêtres de plugins, de réglages de canal et d'outils. Il ne ferme pas les cinq fenêtres principales.
- Tant que le piano du clavier de saisie est activé (`Ctrl+T`), les touches de lettres jouent des notes et la plupart des raccourcis à une lettre sont suspendus.
- `Vue › Style de disposition` passe du flux par patterns (par défaut) au flux par lancement de clips, qui ajoute une grille de clips à côté de la Playlist.

## Voir aussi

- [Votre premier morceau](/wiki/first-song/)
- [Dispositions](/wiki/layouts/)
- [Raccourcis](/wiki/shortcuts/)
- [Réglages](/wiki/settings/)
