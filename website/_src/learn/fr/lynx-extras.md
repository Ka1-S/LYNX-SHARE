---
source: dd7b0185df
---
## Ce que sont les extras

![La page LYNX extras, groupée par domaine](/assets/img/learn/lynx-extras-1.webp)

1. Ouvrez `Options › Extras LYNX...` : un interrupteur par extra, groupés par domaine (Mixer, Playlist, Piano roll, édition et plus).
2. Chaque interrupteur dit ce qu'il fait et où vous le trouvez aussi (souvent dans le menu de la fenêtre elle-même).

## Activez-en un

![Le loudness-mètre à côté du Master](/assets/img/learn/lynx-extras-2.webp)

1. Appuyez sur `Ctrl+F` et tapez une partie de son nom, ici "loudness", puis cliquez sur son interrupteur.
2. Le changement est immédiat : le Mixer affiche maintenant le loudness du morceau à côté du Master.

## Comme FL ou Recommandé

![FL-exact et Recommended en haut de la page](/assets/img/learn/lynx-extras-3.webp)

1. `Comme FL` désactive tous les extras, et LYNX fonctionne exactement comme FL Studio.
2. `Recommandé` remet chaque extra à sa valeur par défaut. La plupart de ceux activés par défaut changent l'apparence ou ajoutent quelque chose que FL Studio n'a pas, et aucun ne change ce que font les touches et les clics de FL Studio. Quatre d'entre eux changent le son ou vos fichiers, et leurs lignes le disent.

- `Les clips de pattern étirés bouclent leur pattern (LYNX)` rejoue le pattern là où FL Studio reste silencieux après lui.
- `Les boucles déposées prennent le tempo et la tonalité trouvés dans leur audio`, dans la disposition Simple, change la façon dont une boucle déposée est jouée.
- `Les fichiers MIDI déposés sur la Playlist deviennent des clips de pattern` ajoute un pattern au morceau quand vous déposez un fichier (FL Studio refuse le fichier).
- `Une ligne par plugin installé dans plusieurs formats` choisit le format dans lequel un nouveau plugin se charge, et le projet garde ce format.

## Encore un qui change le son

`Compenser aussi les automations de fader et de routage (LYNX)`, dans `Compensation de latence des plugins` du menu d'un insert du mixeur, est activé dans chaque nouveau projet. Il n'est pas sur la page des extras LYNX, donc `Comme FL` le laisse activé.

1. Avec des plugins qui ajoutent de la latence (un égaliseur à phase linéaire, un limiteur à anticipation), il joue l'automation des faders, panoramiques, envois et faders VCA aussi tard que l'audio sur lequel elle agit, et un fondu tombe donc là où vous l'entendez.
2. FL Studio ne retarde que l'automation des boutons des plugins, donc là-bas le même mouvement de fader s'entend un peu plus tôt. Les projets ouverts depuis FL Studio et les projets faits avant l'ont désactivé.
3. Pour le timing de FL Studio, désactivez-le dans ce menu : c'est un réglage du projet entier.

Voir aussi [Réglages](/wiki/settings/) et [Visite des Réglages](/learn/settings-tour/).
