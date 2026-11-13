---
source: 094cbefd18
---
![La fenêtre d'enregistrement demande quoi enregistrer : audio, notes et automation, tout, ou audio dans l'éditeur audio](/assets/img/wiki/record-dialog.webp)

Le bouton d'enregistrement du transport, ou la touche `R`, active l'enregistrement. La première fois, une fenêtre demande ce que vous voulez enregistrer. L'audio va dans la [Playlist](/wiki/playlist/) sous forme de clips audio. Les notes vont dans le pattern actuel. Les commandes que vous bougez deviennent de l'automation.

## Enregistrer un micro ou un instrument

1. Ouvrez `Options › Réglages audio...` et choisissez un périphérique d'entrée.
2. Ouvrez le [Mixer](/wiki/mixer/) (`F9`) et sélectionnez un insert vide.
3. Cliquez sur la liste d'entrée au-dessus des emplacements d'effets de l'insert et choisissez une entrée. Les paires stéréo viennent en premier, les canaux seuls sont marqués `(mono)`.
4. Cliquez sur le bouton d'armement d'enregistrement à côté de la liste d'entrée.
5. Appuyez sur `R` et choisissez `Audio, dans la Playlist sous forme de clip audio`.
6. Appuyez sur lecture si la lecture n'a pas démarré toute seule, et jouez.
7. Appuyez sur stop. La prise est un clip audio dans la Playlist.

`Shift+R` fait les étapes 4 à 6 en une touche : il arme l'insert sélectionné, enregistre l'audio seulement et lance la lecture. Appuyez dessus de nouveau pour arrêter.

Quand vous choisissez une entrée pour un insert qui enregistre plus que cette entrée (les autres canaux qui y envoient sont enregistrés aussi), LYNX SHARE demande, comme FL Studio, s'il faut passer sur `Entrée externe seule`. Répondez `Oui` pour enregistrer l'entrée seule sur cet insert.

## Enregistrement audio en un clic

`Outils › Enregistrement audio en un clic` lance une prise en une seule étape, comme dans FL Studio. Il agit sur l'insert du Mixer sélectionné. Avec le Master ou rien de sélectionné, il prend le premier insert qui a une entrée, ou sinon un insert libre.

| Commande | Ce qu'elle fait |
|---|---|
| `Dans l'éditeur/enregistreur audio Edison` | Place Edison sur l'insert, réglé pour enregistrer au démarrage de la lecture, et lance la lecture. Voir [Mixer](/wiki/mixer/) |
| `Dans la playlist comme clip audio` | Passe en mode Song, arme l'insert, demande son entrée s'il n'en a pas, et lance l'enregistrement et la lecture. Les inserts déjà armés enregistrent tels quels |

## Ce que propose la fenêtre d'enregistrement

| Choix | Ce qu'il fait |
|---|---|
| `Audio, dans la Playlist sous forme de clip audio` | Arme l'insert du Mixer sélectionné et demande son entrée s'il n'en a pas |
| `Notes et automation` | Enregistre les notes d'un clavier MIDI ou du clavier de saisie, et les commandes que vous bougez |
| `Tout` | Les notes, l'automation, et l'audio des inserts armés |
| `Audio, dans l'éditeur audio` | Enregistre l'entrée dans un nouvel éditeur audio pendant la lecture du morceau |

Cochez `Ne plus demander` pour garder votre choix. Ensuite, faites un clic droit sur le bouton d'enregistrement pour le filtre d'enregistrement : `Automation`, `Notes`, `Audio`, `Clips`, et `Demander quoi enregistrer au début de l'enregistrement` pour faire revenir la fenêtre.

## La liste d'entrée d'un insert

![Le Mixer avec l'insert 3 armé et sa liste d'entrée réglée sur les entrées 1 et 2](/assets/img/wiki/mixer-input.webp)

| Réglage | Ce qu'il fait |
|---|---|
| `Source d'entrée audio` | L'entrée du périphérique que cet insert joue et enregistre, ou `(aucun)` |
| `Enregistrer` | Où la prise est captée : `Entrée externe seule`, `Entrée externe et du mixeur`, `Après les effets`, `Après l'EQ`, `Après le niveau et le panoramique` ou `Après la piste` |
| `Enregistrer aussi l'entrée sèche en seconde prise (LYNX)` | Garde l'entrée non traitée à côté d'une prise traitée |
| `Monitoring de l'entrée externe` | `Désactivé`, `Si armé`, `Auto (si armé, pas pendant la lecture)` ou `Actif` |
| `Protection d'enregistrement (ne peut pas être armé)` | Protège l'insert contre un armement accidentel |

## Enregistrer des notes

Les notes viennent d'un clavier MIDI, du clavier de saisie (`Ctrl+T`) ou du clavier tactile, et vont aux canaux sélectionnés dans le pattern actuel. Voir [Contrôleurs MIDI](/wiki/midi-controllers/).

- `Mélanger les notes enregistrées` (`Ctrl+B`) choisit entre superposer et remplacer. Actif, les nouvelles notes s'ajoutent à celles du pattern. Désactivé, les anciennes notes d'un canal enregistré sont effacées.
- `Quantifier les notes enregistrées sur le magnétisme global` se trouve dans les options d'enregistrement.
- `Saisie pas à pas` (`Ctrl+E`) fonctionne à l'arrêt : chaque note jouée entre dans le pattern, un pas de magnétisme après l'autre.
- `Capturer le MIDI` (`Ctrl+Shift+Alt+R`) place dans le pattern les notes que vous venez de jouer, même si l'enregistrement était désactivé.
- La pédale de sustain (CC 64) et la pédale de sostenuto (CC 66) retiennent les notes que vous jouez, et une note enregistrée dure jusqu'au relâchement de la pédale. Un canal Layer transmet la pédale à ses canaux. Avec `La pédale contrôle la fin des notes` désactivé dans `Options › Réglages MIDI...`, la pédale va aux plugins comme un contrôleur.
- En mode Song, les notes enregistrées dans un clip de pattern plus long que son pattern restent là où vous les avez jouées, et le pattern s'allonge. La prise n'est pas repliée sur les premières mesures du pattern. Avec `Les clips de pattern étirés bouclent leur pattern (LYNX)` activé, seules les notes jouées sur des mesures qui avaient déjà des notes vont dans la boucle.

Les commandes que vous bougez pendant l'enregistrement sont écrites en clips d'automation en mode Song et en événements de pattern en mode Pattern. Voir [Automation](/wiki/automation/).

## Options d'enregistrement

Les options se trouvent dans `Options › Enregistrement` et dans le menu du clic droit du bouton d'enregistrement.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| `Décompte` | `Ctrl+P` | Off, 1 mesure, 2 mesures ou 4 mesures de métronome avant le début de l'enregistrement. `Ctrl+P` bascule entre désactivé et la durée choisie en dernier. Le choix est conservé quand vous fermez le programme ; une nouvelle installation démarre à 1 mesure, comme FL Studio |
| `Métronome` | `Ctrl+M` | Clic pendant la lecture |
| `Attendre la première note pour démarrer` | `Ctrl+I` | L'enregistrement attend que vous jouiez |
| `Enregistrement en boucle` | aucune | L'enregistrement boucle au lieu de continuer |
| `Audio : punch in/out sur la région de boucle` | aucune | Joue à travers la région de boucle et ne garde que l'audio qu'elle contient |
| `Audio : pre-roll` et `post-roll` | aucune | Désactivé, 1, 2 ou 4 mesures jouées avant le punch-in et après le punch-out |
| `L'enregistrement lance la lecture` | aucune | Activer l'enregistrement appuie aussi sur lecture |
| `Désarmer les inserts à l'arrêt de l'enregistrement` | aucune | Les inserts armés sont libérés après chaque prise |

`Faible latence à l'enregistrement` se trouve dans le menu du clic droit du bouton d'enregistrement et dans le menu d'insert du Mixer sous `Enregistrement sur disque`. Il est désactivé par défaut. Tant qu'un insert avec une entrée est armé, ou que son monitoring est sur `Actif`, les effets qui ajoutent de la latence sur son chemin vers le Master sont contournés. Cela ne change que ce que vous entendez : un insert garde tous ses effets quand la prise est captée après eux, par exemple avec `Après les effets`. Les effets reviennent quand l'insert est désarmé.

`Compensation de latence` est dans le même menu `Enregistrement sur disque` et est activé par défaut, comme dans FL Studio. Elle recule chaque prise de l'aller-retour du périphérique audio, de la valeur `Ajustement fin (échantillons)` des réglages audio et de la compensation de délai des plugins. Désactivez-la pour garder les prises là où elles ont été enregistrées.

`Outils › Annuler l'enregistrement` jette ce que la passe d'enregistrement en cours a écrit (ses notes, son automation et ses prises audio) et désactive l'enregistrement. La lecture continue et les inserts armés restent armés.

## Enregistrer dans des emplacements de clips

Dans le [flux par lancement de clips](/wiki/layouts/), les prises peuvent aller dans la grille de clips au lieu de la Playlist, comme dans Ableton Live.

- Armez une piste avec son bouton d'armement dans la table de mixage de session. Ses emplacements vides affichent alors un bouton d'enregistrement rond à la place du carré d'arrêt. Cliquez sur l'un d'eux pour enregistrer dans cet emplacement.
- `Enregistrement de session`, sous la grille, enregistre les pistes armées dans la scène sélectionnée. Pendant que leurs clips jouent, les nouvelles notes y sont ajoutées (overdub).
- `Durée d'enregistrement fixe`, dans le menu d'un emplacement vide, termine un enregistrement dans un emplacement après 1 mesure ou plus, et le clip continue de jouer. `Désactivé` enregistre jusqu'à l'arrêt.

## Enregistrement en boucle et prises

Avec `Enregistrement en boucle` actif, l'enregistrement boucle sur la région de boucle en mode Song, ou sur le pattern en mode Pattern. Désactivé, l'enregistrement continue tout droit.

Chaque passage d'un enregistrement audio en boucle tombe sur la piste de Playlist libre suivante, et les pistes forment un groupe. `Mélanger les notes enregistrées` décide de ce qui joue après l'arrêt : désactivé, chaque passage est rendu muet ; actif, ils jouent tous. Faites un clic droit sur une piste du groupe et choisissez `Créer un comp avec ces prises` pour construire une prise unique à partir des passages. `Après l'enregistrement en boucle, seul le dernier passage est lu`, dans les options d'enregistrement, revient à une seule piste.

## Où les enregistrements sont sauvegardés

- Un projet enregistré garde ses prises dans `Audio/Recorded`, à côté du fichier de projet.
- Un projet non enregistré enregistre dans `LYNX/Recordings` de votre dossier Musique.
- Les prises sont des fichiers WAV à la fréquence d'échantillonnage du périphérique audio, en 16 bits, 24 bits ou 32 bits flottant. Le format (`Format de fichier`) et les noms des prises se règlent sous `Enregistrement` dans `Options › Réglages audio...`.
- `Ouvrir le dossier des enregistrements` se trouve dans le menu du clic droit du bouton d'enregistrement. `Fichier › Supprimer les enregistrements inutilisés et enregistrer...` supprime les prises que le projet n'utilise plus.

## Bon à savoir

- Un insert armé sans entrée n'enregistre rien. LYNX SHARE vous le dit et vous renvoie à la liste d'entrée.
- L'enregistrement refuse de démarrer quand le disque a moins de 30 secondes de place, et avertit en dessous de 10 minutes.
- `Options › Enregistrer l'audio de l'entrée` enregistre les deux premiers canaux d'entrée dans un nouvel insert quand rien n'est armé.
- Sous macOS, le système demande l'accès au micro la première fois que vous enregistrez.
- Si LYNX SHARE se ferme pendant l'enregistrement, le démarrage suivant propose de placer les prises récupérées dans la Playlist.

## Voir aussi

- [Configuration audio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automation](/wiki/automation/)
- [Contrôleurs MIDI](/wiki/midi-controllers/)
- [Dépannage](/wiki/troubleshooting/)
