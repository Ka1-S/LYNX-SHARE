---
source: a06504f3a0
---
![La page MIDI des réglages : périphériques d'entrée, périphériques de sortie avec synchro maître, et synchro externe](/assets/img/wiki/settings-midi.webp)

LYNX SHARE reçoit les notes et les mouvements de contrôleurs de n'importe quel périphérique d'entrée MIDI. Les périphériques se règlent sur la page MIDI des réglages : `Options › Réglages MIDI...`, ou `F10`. Sans matériel, le clavier de saisie et le clavier tactile jouent aussi des notes.

## Connecter un clavier

1. Branchez le clavier et ouvrez `Options › Réglages MIDI...` (`F10`).
2. Trouvez-le sous `Périphériques d'entrée` et cochez-le. S'il n'est pas listé, cliquez sur `Actualiser la liste des périphériques`.
3. Laissez les deux listes sur `Tout canal MIDI` et `Joue les canaux sélectionnés`.
4. Fermez les réglages et sélectionnez un canal dans le [Channel Rack](/wiki/channel-rack/).
5. Jouez. Le canal sélectionné sonne.

## Périphériques d'entrée

| Réglage | Ce qu'il fait |
|---|---|
| Case à cocher | Active ou désactive le périphérique |
| `Tout canal MIDI` | Écoute tous les canaux MIDI, ou un seul canal. `Contrôleur MPE` se trouve à la fin de cette liste |
| `Joue les canaux sélectionnés` | Les notes vont aux canaux sélectionnés dans le Channel Rack, ou toujours vers un canal fixe |
| `Vélocité` | `Comme joué`, `Douce (un jeu léger sonne plus fort)`, `Dure (demande un jeu plus appuyé)` ou `Fixe (chaque note à pleine vélocité)` |
| `Canal du mode performance` | Les notes de ce canal MIDI lancent des clips de la Playlist quand le mode Performance est actif |
| `Saut vers un marqueur du song` | Les notes de ce canal MIDI déplacent le morceau vers un marqueur au lieu d'être jouées |
| `Type de contrôleur` | `Détecter automatiquement`, `Contrôleur générique` ou un profil de contrôleur |
| `La pédale contrôle la fin des notes` | Actif par défaut. Une pédale de sustain (CC 64) ou de sostenuto (CC 66) retient les notes. Désactivé, la pédale va aux plugins comme un contrôleur |

## Lier un bouton à une commande

1. Faites un clic droit sur le bouton ou le fader dans LYNX SHARE.
2. Choisissez `Lier à un contrôleur MIDI (bougez un bouton du contrôleur)`.
3. Bougez le bouton ou le fader de votre matériel. Le lien est créé.

Le lien est enregistré avec le projet. Pour le retirer, faites de nouveau un clic droit sur la commande et choisissez `Délier le contrôleur`. L'entrée de menu affiche le numéro de CC utilisé.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| `Override global link...` | aucune | Crée un lien valable dans tous les projets. Il est conservé dans les réglages, pas dans le projet |
| `Retirer le lien global` | aucune | Le retire |
| `Multilien vers les contrôleurs` | `Ctrl+J` | Tant qu'il est actif, les commandes que vous bougez sont liées, dans l'ordre, aux commandes du matériel que vous bougez ensuite. Il se trouve dans le menu `Options` |
| `Reprise (mode takeover)` | aucune | Sur la page MIDI. Un contrôleur ne déplace un paramètre qu'après avoir atteint sa valeur actuelle, de sorte que rien ne saute |

La page MIDI liste les liens sous `Liens (ce projet, puis globaux)`. Un périphérique dont le `Type de contrôleur` est `Contrôleur générique` ne fait que ce que vous liez à la main.

Les paramètres d'un plugin tiers sont accessibles par `Automatiser un paramètre du plugin` dans le menu du canal ou le menu de l'emplacement d'effet. Chaque paramètre y a les mêmes entrées de lien. Voir [Automation](/wiki/automation/).

## Profils de contrôleur

Un profil de contrôleur configure les boutons de transport, faders, boutons rotatifs, pads, voyants et l'écran d'un appareil, sans rien à lier.

1. Ouvrez `Options › Réglages MIDI...` (`F10`) et cochez l'appareil sous `Périphériques d'entrée`.
2. Laissez son `Type de contrôleur` sur `Détecter automatiquement`. Le nom entre parenthèses est le profil trouvé d'après le nom de port de l'appareil.
3. S'il indique `Contrôleur générique`, choisissez vous-même le profil dans la liste.
4. Lisez la ligne à côté de la liste. `Feedback to` nomme le port de sortie qui reçoit les voyants, les faders motorisés et le texte de l'écran.

| Profil | Ce qu'il mappe |
|---|---|
| `Mackie Control Universal` | Faders vers les volumes des inserts et le Master. V-Pots vers le pan, vers les paramètres du plugin du canal sélectionné ou vers les volumes du Channel Rack. Arm, solo, mute et select par tranche, banques de faders, play, stop, record, rewind, forward, mode Pattern ou Song, métronome, annulation et molette jog. L'écran affiche les noms et les valeurs |
| `Mackie Control Extender (XT)` | La même chose pour 8 tranches de plus. Les unités partagent une seule banque, dans l'ordre de la liste `Périphériques d'entrée` |
| `Novation Launchkey MK3 (DAW mode)` | Boutons rotatifs vers le pan, le volume ou les paramètres de plugin, faders vers le volume, boutons de fader pour sélectionner un insert. Boutons de banque, play, stop, record, boucle, métronome et annulation. Les pads Session sont les 16 steps du canal sélectionné. À utiliser sur le second port du clavier |
| `Akai APC Key 25 mk2` | Boutons vers le volume, le pan ou les paramètres de plugin. Les deux rangées de pads du haut sont les 16 steps du canal sélectionné et les trois du dessous jouent des notes. Les boutons de piste sélectionnent un insert. Stop, play et record. À utiliser sur le second port |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | La carte Mackie Control, sur le port MCU/HUI du clavier |
| `Korg nanoKONTROL2 (Mackie Control mode)` | La carte Mackie Control. Il n'est pas détecté : démarrez l'appareil dans un mode Mackie Control et choisissez ce type à la main |

Les boutons et faders qui envoient une position utilisent une reprise douce : ils ne déplacent un contrôle qu'après avoir atteint sa valeur. Les contrôles que vous avez liés à la main gardent leurs liens. `Dossier des profils de contrôleur` ouvre le dossier de vos propres profils (`*.lynxctl`), et `Actualiser la liste des périphériques` les relit.

Les profils intégrés ont été écrits d'après la documentation de chaque fabricant. Ils n'ont pas encore été essayés sur le matériel.

## Contrôleurs MPE

Le MPE en est à un premier stade. Un contrôleur MPE joue chaque note avec son propre pitch bend, son propre timbre (CC 74) et sa propre pression.

1. Sur la page MIDI, ouvrez la liste de l'appareil qui indique `Tout canal MIDI` et choisissez `Contrôleur MPE`.
2. Dans la liste qui apparaît à côté, choisissez les canaux membres, de `Canal 2` à `Channels 2-16`.
3. Jouez. Pour enregistrer, voir [Enregistrement](/wiki/recording/).

- Une note enregistrée garde une valeur de chacun : son bend comme hauteur fine, son timbre comme Mod X et sa pression la plus forte comme Mod Y. Ce n'est pas une courbe.
- Parmi les instruments intégrés, le Sampler et le synthé intégré suivent ces valeurs.
- Un plugin tiers reçoit le MPE quand `MPE` est activé sous `MIDI` dans les `Réglages du wrapper` de la fenêtre du plugin. Chaque note part alors sur son propre canal MIDI, de 2 à 16.
- Il n'a pas encore été essayé avec un vrai contrôleur MPE.

## Le clavier de saisie

`Ctrl+T`, ou `Options › Clavier de saisie vers piano`, transforme le clavier de l'ordinateur en piano. Les rangées de `Z` à `M` et de `Q` à `P` jouent les canaux sélectionnés.

- Tant qu'il est actif, les touches de lettres et de chiffres sont des notes. Les touches d'outils à une seule lettre du Piano Roll et de la Playlist ne fonctionnent que lorsqu'il est désactivé.
- Il joue toujours à la vélocité du Piano Roll. Le réglage `Vélocité` de la page MIDI concerne les périphériques MIDI.
- Le même interrupteur se trouve dans `Options › Réglages généraux...`.

## Le clavier tactile

![Le clavier tactile : boutons d'octave, taille des touches, mode de vélocité et une rangée de touches de piano à partir de do 4](/assets/img/wiki/touch-keyboard.webp)

`Vue › Clavier tactile` (`Alt+F7`) ouvre un piano que vous jouez à la souris ou sur un écran tactile. Il joue le canal sélectionné dans le Channel Rack, et affiche son nom à droite.

| Réglage | Ce qu'il fait |
|---|---|
| `-` et `+` | Une octave plus bas ou plus haut |
| Taille des touches | `Petites touches`, `Touches moyennes` ou `Grandes touches` |
| `Vélocité : selon l'endroit touché` | Près du haut d'une touche, le son est doux ; près du bas, il est fort |
| `Vélocité : fixe` | Toutes les notes à la vélocité réglée avec le curseur |
| `Noms des notes` | Affiche les noms sur les touches |

## Synchro MIDI

`Périphériques de sortie` sur la page MIDI envoie le transport vers du matériel et d'autres logiciels. Cochez `Envoyer la synchro maître` pour un périphérique et choisissez `MIDI clock` (avec Start, Continue, Stop et Song Position Pointer) ou MIDI Time Code. `Décalage (ms)` envoie la synchro plus tard avec une valeur positive et plus tôt avec une valeur négative. `Envoyer all notes off` envoie ce message au périphérique à l'initialisation et après un export.

`Synchro sur clock externe` fonctionne dans l'autre sens : LYNX SHARE suit l'horloge MIDI de l'entrée que vous choisissez, avec son tempo, Start, Continue, Stop et Song Position Pointer. `Décalage de la synchro externe (millisecondes)` fait jouer LYNX SHARE plus tard avec une valeur positive et plus tôt avec une valeur négative. `État` indique si une horloge tourne.

## Bon à savoir

- Choisir une entrée pour `Synchro sur clock externe` active ce périphérique sous `Périphériques d'entrée`.
- Un tempo déplacé par un contrôleur MIDI lié ne redimensionne pas les clips audio. Saisissez ou réglez plutôt le tempo final.
- Pour enregistrer ce que vous jouez, voir [Enregistrement](/wiki/recording/). Les notes entrantes peuvent être calées sur une gamme dans le [Piano Roll](/wiki/piano-roll/).
- Les CC MIDI, l'aftertouch de canal et le pitch bend envoyés à un plugin peuvent être automatisés avec `Automatiser un contrôleur MIDI (CC, aftertouch, pitch bend)`.
- Une note maintenue par une pédale est enregistrée jusqu'au moment où la pédale est relevée.
- Un clip d'automation ne reprend pas un contrôle tant que vous le déplacez depuis un contrôleur. Le contrôle suit de nouveau son automation au prochain démarrage de la lecture.

## Voir aussi

- [Enregistrement](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Réglages](/wiki/settings/)
- [Raccourcis](/wiki/shortcuts/)
