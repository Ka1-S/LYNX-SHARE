---
source: de4ead4a7a
---
Cette page parcourt une fois tout le chemin : un pattern de batterie, une mélodie, un arrangement, un mixage et un fichier exporté. Chaque partie renvoie à la page qui couvre sa fenêtre en détail. Si vous n'entendez rien à un moment ou un autre, voyez [Configuration audio](/wiki/audio-setup/).

## Faire un pattern de batterie

1. Choisissez `Fichier › Nouveau à partir d'un modèle › Vide`. Le projet démarre avec cinq canaux, Kick, Clap, Hat, Snare et Synth, et aucun step.
2. Appuyez sur `F6` si le [Channel Rack](/wiki/channel-rack/) n'est pas affiché.
3. Cliquez sur quelques steps de la ligne Kick. Un glissé en dessine plusieurs, et un clic droit efface.
4. Appuyez sur `Space`. Le transport est en mode Pattern : le pattern joue donc en boucle. Appuyez de nouveau sur `Space` pour arrêter.
5. Donnez aux lignes Clap, Hat et Snare leurs propres steps.
6. Appuyez sur `F2` pour nommer et colorer le pattern.

Pour ajouter un son qui n'y est pas, cliquez sur la case `+ ajouter un canal` sous la dernière ligne et choisissez un instrument dans le menu. Pour utiliser un sample à vous, ouvrez le [Browser](/wiki/browser/) avec `Alt+F8`, cliquez sur un fichier audio pour l'écouter et double-cliquez dessus pour l'ajouter comme nouveau canal.

## Écrire une mélodie

1. Appuyez sur `F4`. LYNX SHARE passe au prochain pattern vide et demande son nom.
2. Faites un clic droit sur le nom du canal Synth et choisissez `Piano Roll`.
3. Avec le Crayon (`P`), cliquez dans la grille pour ajouter une note.
4. Glissez une note pour la déplacer et glissez sa fin pour changer sa longueur. Un clic droit supprime une note.
5. Appuyez sur `Space` pour écouter le pattern pendant que vous éditez.

`Ctrl+T` transforme le clavier de saisie en piano : vous pouvez donc essayer des notes avec `Z` à `M` et `Q` à `P` d'abord. Désactivez-le de nouveau avant d'utiliser des raccourcis à une lettre comme `P`.

## Arranger les patterns

1. Appuyez sur `F5` pour afficher la [Playlist](/wiki/playlist/).
2. Dans le panneau Pipette à gauche de la Playlist, cliquez sur le pattern de batterie. C'est maintenant la source des clips.
3. Avec le Crayon (`P`), cliquez sur une piste pour placer le pattern. Cliquez de nouveau plus à droite pour le placer encore, ou utilisez le Pinceau (`B`) et glissez pour peindre des clips bout à bout.
4. Cliquez sur le pattern de mélodie dans la Pipette et placez-le sur une autre piste.
5. Appuyez sur `L` pour passer en mode Song, puis sur `Space`. La Playlist joue à partir de la position du morceau. Cliquez sur la règle pour la fixer.

## Mixer et ajouter un effet

1. Appuyez sur `F9` pour afficher le [Mixer](/wiki/mixer/). Les cinq canaux de départ passent déjà par les inserts 1 à 5, et un canal ajouté depuis le menu `+ ajouter un canal` prend un insert libre. Le numéro dans la case de routage du canal, dans le Channel Rack, indique lequel.
2. Pour donner aux canaux des inserts libres bien à eux, sélectionnez-les dans le Channel Rack et appuyez sur `Ctrl+L`. Chaque insert prend le nom et la couleur du canal.
3. Glissez le fader d'un insert pour régler son niveau.
4. Cliquez sur un insert pour le sélectionner. Le panneau d'effets à droite affiche ses dix slots.
5. Cliquez sur un slot vide et choisissez un effet. Cliquez sur le nom du slot pour afficher ou masquer la fenêtre de l'effet. Le voyant du slot met l'effet en bypass.

## Exporter le morceau

![La fenêtre d'export : plage et queue à gauche, format, résolution en bits et fréquence d'échantillonnage à droite, options de mixage en dessous](/assets/img/wiki/export.webp)

1. Appuyez sur `Ctrl+S` et enregistrez d'abord le projet.
2. Choisissez `Fichier › Exporter › Fichier Wave...`. Pour d'autres formats, le même menu propose `Fichier MP3...`, `Fichier OGG...` et `Fichier FLAC...`.
3. Réglez `Étendue` sur `Morceau (Playlist)`. Les autres choix sont `Pattern actuel` et `Sélection de la Playlist`.
4. Laissez les autres options telles quelles.
5. Cliquez sur `Exporter...`, choisissez le nom du fichier et attendez le rendu.

## Ce que faisait chaque touche

| Commande | Touche | Effet |
|---|---|---|
| Channel Rack | `F6` | Affiche ou masque le Channel Rack |
| Pattern vide suivant | `F4` | Va au pattern vide suivant et le nomme |
| Piano Roll | `F7` | Affiche ou masque le Piano Roll |
| Playlist | `F5` | Affiche ou masque la Playlist |
| Mode Pattern ou Song | `L` | Change ce que joue `Space` |
| Mixer | `F9` | Affiche ou masque le Mixer |
| Envoyer vers des inserts libres | `Ctrl+L` | Donne aux canaux sélectionnés leurs propres inserts du Mixer |
| Enregistrer | `Ctrl+S` | Enregistre le projet en `.lynx` |
| Export | `Ctrl+R` | Ouvre la fenêtre d'export avec le dernier format utilisé |

## À savoir

- En mode Pattern, vous n'entendez que le pattern actuel. Si le morceau est muet après l'arrangement, vérifiez que le transport est en mode Song.
- La fenêtre d'export démarre sur `Morceau (Playlist)` quand le transport est en mode Song et que la Playlist contient des clips. Sinon, elle démarre sur `Pattern actuel`.
- Le clic du métronome ne fait jamais partie d'un export.
- Le bas de la fenêtre d'export affiche la longueur du morceau en mesures, sa durée totale et l'espace disque dont le fichier a besoin.
- Par défaut, un nouveau projet a un Limiteur sur l'insert Master. Il fait partie de ce que vous entendez et de ce que vous exportez.
- Annuler, c'est `Ctrl+Z`. Pressé de nouveau, il rétablit cette étape. `Ctrl+Alt+Z` remonte plus loin.

## Voir aussi

- [Visite de la fenêtre](/wiki/tour/)
- [Exporter](/wiki/exporting/)
- [Automation](/wiki/automation/)
- [Enregistrement](/wiki/recording/)
- [Instruments et effets](/instruments-and-effects/)
