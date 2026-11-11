---
source: b2de12e24d
---
![Le Mixer](/assets/img/mixer.webp)

Chaque canal passe par un insert du Mixer. Chaque insert a un fader, un pan, un bouton Muet et un solo, un vumètre de crête, dix emplacements d'effets avec chacun un taux dry/wet, et un EQ intégré. Tous les inserts finissent dans le Master, sauf si vous les routez ailleurs. Appuyez sur `F9` pour afficher ou masquer le Mixer.

Un projet peut avoir jusqu'à 127 inserts. Le Mixer a une bande d'ajout pour en créer un nouveau. La liste dans la barre de titre du Mixer choisit l'une des huit dispositions de bandes, de `Compacte` à `Très grand`.

## Mettre un effet sur un canal

1. Dans le [Channel Rack](/wiki/channel-rack/), sélectionnez le canal et appuyez sur `Ctrl+L`. Le canal va sur un insert libre, qui prend son nom et sa couleur.
2. Appuyez sur `F9` pour afficher le Mixer et cliquez sur la bande de cet insert.
3. Dans le panneau d'effets, cliquez sur un emplacement vide et choisissez un effet.
4. Cliquez sur le nom de l'emplacement pour afficher ou masquer la fenêtre de l'effet.
5. Cliquez sur le voyant de l'emplacement pour contourner l'effet et comparer.

## Envoyer un canal vers un insert

Dans le Channel Rack, faites glisser la case de routage du canal (ou utilisez la molette dessus) pour choisir le numéro d'insert. Un double-clic sur la case affiche cet insert dans le Mixer.

`Ctrl+L` dans le Channel Rack agit sur plusieurs canaux sélectionnés à la fois. Chacun va sur son propre insert libre.

Quand le Mixer a le focus, `Ctrl+L` lie les canaux sélectionnés à l'insert sélectionné, et `Ctrl+Shift+L` les lie à des inserts consécutifs à partir de celui-ci.

## Sélectionner et déplacer des inserts

- Cliquez sur une bande pour la sélectionner.
- `Ctrl`+clic, ou glisser sur plusieurs bandes, sélectionne une plage. `Ctrl+Shift`+clic en ajoute ou en retire une.
- Faites glisser une bande latéralement, ou appuyez sur `Alt+Left` et `Alt+Right`, pour déplacer les inserts sélectionnés.
- Faites un clic droit sur une bande pour le menu de l'insert. `F2` renomme et colore l'insert sélectionné.

Déplacer, insérer ou supprimer des inserts garde les envois, les sidechains et l'automation sur leur insert.

## Muet et solo

Cliquez sur le voyant Muet pour rendre un insert muet. Faites un clic droit ou `Ctrl`+clic dessus pour le solo. `S` met en solo les inserts sélectionnés, et `Alt+S` les met en solo avec les inserts qui les alimentent.

## Effets

Le panneau d'effets montre les dix emplacements de l'insert sélectionné.

- Faites glisser un emplacement, ou utilisez la molette dessus, pour le monter ou le descendre.
- Faites un clic droit sur un emplacement pour ses options (remplacer, déplacer, supprimer).
- `Del` supprime le plugin de l'emplacement sélectionné.
- L'interrupteur dans l'en-tête du panneau active ou désactive tous les effets de l'insert.
- Vous pouvez faire glisser un effet depuis la Base de plugins du [Browser](/wiki/browser/) sur un insert ou un emplacement d'effet.

## EQ de l'insert

Chaque insert a son propre EQ. Faites glisser une bande pour régler sa fréquence et son niveau, utilisez la molette pour la largeur, et double-cliquez sur une bande pour la remettre à 0 dB. Faites un clic droit sur le titre de l'EQ pour le contourner. Si vous voulez un Mixer sans EQ intégré, les nouveaux projets peuvent être réglés pour démarrer avec un mixeur vierge.

## Routage, envois et sidechain

![Le Mixer avec l'insert Kick sélectionné, ses envois dessinés en câbles sous les bandes et le panneau d'effets à droite](/assets/img/wiki/mixer-sends.webp)

Sélectionnez l'insert que vous voulez router. Chaque autre bande a alors un interrupteur de routage :

- Cliquez sur l'interrupteur d'une autre bande pour y envoyer l'insert sélectionné. Glisser après l'activation règle le niveau d'envoi.
- `Ctrl`+clic route vers cet insert uniquement.
- `Shift`+clic crée un envoi sidechain. `Ctrl+Shift`+clic le crée en sidechain seul.
- Le clic droit ouvre les mêmes choix sous forme de menu.

Les effets avec une entrée latérale, comme un compresseur, ont un sélecteur `Sidechain depuis` qui liste les inserts à écouter. En choisir un crée le routage pour vous.

## Groupes et faders VCA

Les groupes du Mixer se replient avec leur chevron (ou `Alt`+clic pour tous les groupes). Un fader VCA déplace le niveau de ses inserts membres, qui gardent leurs propres faders. Créez-en un depuis le menu de l'insert : sous Groupe, choisissez `VCA › Nouveau VCA avec la sélection`.

## Enregistrement

Chaque insert a un bouton d'armement d'enregistrement. Avec un insert armé, Rec et Play enregistrent son entrée audio dans un clip de Playlist. `Shift+R` arme l'insert sélectionné et lance l'enregistrement en une seule étape. Voir [Enregistrement](/wiki/recording/).

## Touches

`Ctrl+L` et `Ctrl+Shift+L` n'ont ce sens que lorsque le Mixer a le focus.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| Lier des canaux | `Ctrl+L` | Lie les canaux sélectionnés à l'insert sélectionné |
| Lier à des inserts consécutifs | `Ctrl+Shift+L` | Lie les canaux sélectionnés à des inserts à partir de celui qui est sélectionné |
| Déplacer des inserts | `Alt+Left` / `Alt+Right` | Déplace les inserts sélectionnés |
| Renommer et colorer | `F2` | Renomme et colore l'insert sélectionné |
| Solo | `S` | Met en solo les inserts sélectionnés |
| Solo avec les sources | `Alt+S` | Met en solo les inserts sélectionnés et les inserts qui les alimentent |
| Supprimer un effet | `Del` | Supprime le plugin de l'emplacement sélectionné |
| Armer et enregistrer | `Shift+R` | Arme l'insert sélectionné et lance l'enregistrement |

## Bon à savoir

- `Rendre en audio` (un insert converti en clip audio) joue environ 5 dB plus fort que l'insert qu'il remplace. C'est un problème connu des notes de version 0.1.25.
- `Ctrl+L` ne fait pas la même chose dans le Channel Rack et dans le Mixer. Dans le Channel Rack, il choisit des inserts libres. Dans le Mixer, il utilise l'insert sélectionné.
- Les interrupteurs de routage apparaissent sur les autres bandes, pas sur celle qui est sélectionnée.
- La limite est de 127 inserts par projet et de dix emplacements d'effets par insert.

## Voir aussi

- [Channel Rack](/wiki/channel-rack/)
- [Enregistrement](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Plugins tiers](/wiki/plugins/)
- [Instruments et effets](/instruments-and-effects/)
