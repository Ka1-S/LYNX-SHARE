---
source: b96330a448
---
![L'éditeur d'automation avec une courbe qui monte sur six mesures](/assets/img/wiki/automation.webp)

Un clip d'automation est une courbe dans la [Playlist](/wiki/playlist/) qui fait bouger une commande pendant la lecture du morceau : le volume d'un canal, un fader du Mixer, un paramètre de plugin, le tempo. Le clip a sa propre fenêtre d'édition, l'éditeur d'automation.

Il y a trois autres façons de faire bouger une commande. Les événements sont des valeurs stockées dans un pattern. Un lien fait piloter la commande par un bouton MIDI ou un contrôleur interne. Un modulateur fait osciller un bouton autour de sa propre valeur.

## Automatiser un bouton

1. Placez la tête de lecture là où l'automation doit commencer, ou sélectionnez une plage de temps dans la Playlist.
2. Faites un clic droit sur le bouton ou le fader et choisissez `Créer un clip d'automation`.
3. Le clip se place sur la première piste vide de la Playlist et l'éditeur d'automation s'ouvre. La courbe commence par une ligne plate à la valeur actuelle de la commande.
4. Cliquez dans l'éditeur pour ajouter un point, et faites-le glisser à sa place.
5. Maintenez `Alt` et faites glisser un segment vers le haut ou le bas pour le courber.
6. Lancez le morceau en mode Song. La commande suit la courbe.

D'autres façons d'obtenir le même clip :

- `Ajouter › Automation du dernier paramètre modifié` crée un clip pour la commande que vous avez bougée en dernier.
- L'affichage du dernier paramètre modifié, et `Outils › Dernier modifié`, ouvrent le même menu qu'un clic droit sur la commande.
- Pour un plugin tiers, ouvrez le menu du canal dans le Channel Rack ou le menu de l'emplacement d'effet dans le Mixer, puis utilisez `Automatiser un paramètre du plugin`. `Automatiser un contrôleur MIDI (CC, aftertouch, pitch bend)` se trouve à côté.

Pour rouvrir l'éditeur, double-cliquez sur le clip, ou faites un clic droit dessus et choisissez `Modifier les points...`. `Modifier le clip d'automation...` dans le menu de la commande fait la même chose.

## L'éditeur d'automation

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| Nouveau point | Clic ou clic droit, puis glisser | Ajoute un point et le déplace tant que vous maintenez le bouton |
| Déplacer un point | Glisser | `Shift` garde son temps, `Ctrl` garde sa valeur, `Alt` ignore le magnétisme |
| Courber un segment | `Alt`+glisser vers le haut ou le bas | Change la tension du segment sous la souris |
| Poignée de tension | Glisser vers le haut ou le bas | Courbe le segment. `Ctrl` est plus fin, le clic droit le rend droit |
| Point sur la courbe | `Shift`+clic droit | Ajoute un point sur la ligne |
| Menu du point | Clic droit sur un point | `Saisir une valeur...`, `Copier la valeur`, `Coller la valeur`, `Supprimer le point`, et `Courbe entière` pour mettre à l'échelle, retourner ou redresser tout |
| Zoom | `Ctrl`+molette | Zoome la vue |
| Défilement | Glisser avec le bouton du milieu | Déplace la vue |

## Mode LFO

![L'éditeur d'automation en mode LFO, avec une onde triangle et les boutons SPD, TNS, SKEW, PW et AMNT](/assets/img/wiki/automation-lfo.webp)

Cochez `LFO` en haut de l'éditeur. Une forme d'onde façonne alors la valeur du clip, et les points que vous dessinez règlent l'amplitude du LFO au lieu de la valeur elle-même.

| Réglage | Ce qu'il fait |
|---|---|
| `SPD` | La durée d'un cycle, en temps |
| `TNS` | Tension de l'onde |
| `SKEW` | Asymétrie de l'onde |
| `PW` | Largeur d'impulsion |
| `AMNT` | De combien le LFO déplace la valeur |
| `MUL` | Actif : le LFO est multiplié par le niveau de l'enveloppe. Désactivé : il est ajouté |

## Événements dans un pattern

![L'éditeur d'événements avec des événements de volume dessinés en barres](/assets/img/wiki/eventeditor.webp)

Les événements appartiennent à un pattern et sont joués partout où ce pattern joue. Faites un clic droit sur une commande et choisissez `Modifier les événements du pattern` pour ouvrir l'éditeur d'événements correspondant.

- `Dessiner` fixe des valeurs pendant que vous glissez, et un glisser avec le clic droit efface. `Ligne` trace une rampe droite. `Sélectionner` marque une plage de temps pour les outils du bas.
- `Magnétisme` va de 1/2 step à 1 mesure.
- `Gamme`, `Décalage` et `Rendre aléatoire` modifient les événements de la sélection, ou de tout le pattern.
- `LFO` écrit un sinus, un triangle ou un carré de 1 temps à 4 mesures.
- `Effacer` supprime les événements.
- `Vers une automation...` transforme les événements en clip d'automation et les retire du pattern.
- `Fichier` ouvre et enregistre les événements dans un fichier `.fsc`, et importe un fichier MIDI.

`Initialiser le morceau avec cette position`, dans le même menu du clic droit, mémorise la valeur à laquelle la commande est remise chaque fois que le morceau démarre.

## Enregistrer vos mouvements

Activez l'enregistrement avec `Automation` coché dans le filtre d'enregistrement, lancez la lecture et bougez la commande. En mode Song, les mouvements deviennent un clip d'automation. En mode Pattern, ils deviennent des événements dans le pattern actuel. Voir [Enregistrement](/wiki/recording/).

## Liens et modulateurs

- `Lier à un contrôleur MIDI (bougez un bouton du contrôleur)` est traité dans [Contrôleurs MIDI](/wiki/midi-controllers/).
- `Lier à un contrôleur interne` laisse un Envelope, Formula ou Peak Controller du projet piloter la commande. Ajoutez-en un d'abord.
- `Moduler` attache un modulateur, un LFO ou une source aléatoire qui fait osciller un bouton de LYNX SHARE autour de sa propre valeur. `Outils › Modulateurs...` les liste.

## Couloirs d'automation

Les couloirs sont désactivés par défaut. Activez `Couloirs d'automation` dans le menu de la Playlist. Chaque en-tête de piste a alors un bouton qui ajoute un couloir sous la piste, et le menu de la commande gagne `Automatiser dans un couloir`. Dans un couloir, cliquez pour ajouter un point ou faites-le glisser, faites un clic droit pour en supprimer un, et `Alt`+glisser pour courber la ligne. Le menu du couloir contient `Vers un clip d'automation`.

## Bon à savoir

- Un nouveau clip commence à la sélection de temps s'il y en a une, sinon à la tête de lecture en mode Song. Désactivez `Créer les clips d'automation à la position de lecture` dans `Options › Réglages généraux...` pour commencer au début du morceau.
- Plusieurs clips sur une même commande jouent chacun à leur place. Avant son premier clip, une commande garde sa propre valeur.
- Au début d'un clip, la commande atteint la nouvelle valeur en environ 8 ms. FL Studio y glisse progressivement, donc un projet importé peut différer légèrement à ces endroits.
- Là où des clips de la même commande se chevauchent, celui que choisit LYNX SHARE peut différer de celui que choisit FL Studio.
- Les modulateurs ne fonctionnent que sur les boutons propres à LYNX SHARE, et ils sont laissés de côté à l'export en `.flp`.

## Voir aussi

- [Playlist](/wiki/playlist/)
- [Enregistrement](/wiki/recording/)
- [Contrôleurs MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Raccourcis](/wiki/shortcuts/)
