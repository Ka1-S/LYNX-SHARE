---
source: 200df26182
---
![Le Channel Rack avec ses lignes de steps](/assets/img/rack.webp)

Le Channel Rack liste chaque instrument (canal) du projet, un par ligne, et affiche les steps du pattern actuel. Appuyez sur `F6` pour l'afficher ou le masquer. Un clic droit sur le bouton Pattern du transport l'affiche aussi.

## Faire un pattern de batterie

1. Appuyez sur `F6` pour afficher le Channel Rack.
2. Cliquez sur la case `+ ajouter un canal` sous la dernière ligne et choisissez un instrument dans le menu. Vous pouvez aussi double-cliquer sur un sample dans le [Browser](/wiki/browser/) pour obtenir un nouveau canal de sampler.
3. Cliquez sur des steps dans la ligne du canal pour les activer. Glissez pour dessiner une série de steps.
4. Faites un clic droit sur un step pour l'effacer.
5. Appuyez sur `Space` pour lancer la lecture. En mode Pattern, le pattern boucle.
6. Glissez le potard de volume de la ligne pour régler le niveau.

## Programmer les steps

- Cliquez sur un step pour l'activer. Un glissé dessine une série de steps. Si le glissé commence sur un step allumé, il efface à la place.
- Un clic droit ou un glissé avec le bouton droit efface.
- Un clic après la fin du pattern ajoute un step et allonge le pattern.
- Une ligne dont les notes ne sont pas de simples steps (autres touches, notes plus longues, slides) affiche une miniature des notes. Un clic dessus les ouvre dans le [Piano Roll](/wiki/piano-roll/).
- Vous pouvez déposer des fichiers audio sur le rack pour créer des canaux.

Le menu du canal (clic droit sur le nom du canal) contient `Remplir tous les 2 steps`, `Remplir tous les 4 steps`, `Remplir tous les 8 steps`, `Remplissage avancé`, ainsi que `Rotation à gauche` et `Rotation à droite`.

## Le couloir de graphe

![Le couloir de graphe ouvert sous le canal Kick, avec une barre de vélocité par step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` affiche le couloir de graphe sous le canal sélectionné. Les boutons à sa gauche choisissent ce qu'il édite : Vélocité, Pan, Release, Hauteur fine, Mod X ou Mod Y. Un glissé règle une valeur par step, un glissé avec le bouton droit dessine une rampe, et `Alt`+clic réinitialise un step.

## Les éléments d'une ligne

| Élément | Effet |
|---|---|
| Voyant Muet | Un clic coupe le son. `Ctrl`+clic ou clic droit met en solo. |
| Potards de pan et de volume | Glissez ou utilisez la molette. |
| Case de routage | Un glissé ou la molette choisit l'insert du Mixer. Un double-clic affiche cet insert dans le Mixer. Un clic droit prend le prochain insert libre. |
| Bouton de nom | Un clic ouvre ou ferme l'éditeur du canal. `Alt`+clic garde les autres éditeurs ouverts. `Ctrl`+clic fait écouter le canal. Un glissé vers le haut ou le bas réordonne. Un clic droit ouvre le menu du canal. |
| Sélecteur | Un clic sélectionne un seul canal. `Ctrl`+clic ou clic droit ajoute ou retire. `Shift`+clic sélectionne une plage. Un double-clic sélectionne tout. |

## Patterns

Le rack édite toujours le pattern actuel. Le sélecteur de pattern du rack affiche son nom : cliquez sur le nom pour la liste, ou utilisez les flèches. `Shift`+molette sur le titre du rack change aussi de pattern.

La case de longueur de pattern du rack règle la longueur en mesures (glissez-la ou utilisez la molette). Double-cliquez dessus pour une longueur automatique tirée des notes.

Le potard de swing de la barre d'outils principale s'applique à tous les canaux. `Mix du swing` dans le menu du canal règle à quel point un canal le suit.

| Commande | Touche | Effet |
|---|---|---|
| Pattern suivant | `Numpad +` | Fait du pattern suivant le pattern actuel |
| Pattern précédent | `Numpad -` | Fait du pattern précédent le pattern actuel |
| Pattern vide suivant | `F4` | Va au pattern vide suivant et demande un nom |
| Renommer et colorer | `F2` | Renomme et colore le pattern actuel |

## Jouer des notes au clavier du PC

`Ctrl+T` transforme le clavier de saisie en piano : `Z` à `M` et `Q` à `P` jouent les canaux sélectionnés. Tant qu'il est activé, les raccourcis à une seule lettre des éditeurs sont suspendus.

## Touches

Elles fonctionnent quand le Channel Rack a le focus.

| Commande | Touche | Effet |
|---|---|---|
| Sélectionner un canal | `Up` / `Down` | Sélectionne le canal au-dessus ou en dessous |
| Étendre la sélection | `Shift+Up` / `Shift+Down` | Ajoute le canal au-dessus ou en dessous à la sélection |
| Déplacer des canaux | `Alt+Up` / `Alt+Down` | Monte ou descend les canaux sélectionnés |
| Tout sélectionner | `Ctrl+A` | Sélectionne tous les canaux |
| Couper, copier, coller | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Coupe, copie ou colle les steps des canaux sélectionnés |
| Décaler les steps | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Décale en rotation les steps des canaux sélectionnés |
| Cloner | `Alt+C` | Clone les canaux sélectionnés |
| Supprimer | `Alt+Del` | Supprime les canaux sélectionnés |
| Grouper | `Alt+G` | Met les canaux sélectionnés dans un nouveau groupe de filtre |
| Router vers le Mixer | `Ctrl+L` | Envoie les canaux sélectionnés vers des inserts du Mixer libres |
| Muet | `1` to `0` | Coupe ou rétablit le son des canaux 1 à 10 |
| Solo | `Ctrl+1` to `Ctrl+0` | Met en solo les canaux 1 à 10 |
| Couloir de graphe | `Ctrl+K` | Affiche ou masque le couloir de graphe |

## Figer un canal

Figer rend un canal en audio et joue cet audio à sa place. L'instrument est déchargé, même un plugin tiers, si bien qu'il n'utilise ni CPU ni mémoire.

1. Faites un clic droit sur le canal et choisissez `Figer`. Avec plusieurs canaux sélectionnés, l'élément indique `Geler les canaux sélectionnés`.
2. La ligne reçoit une étiquette en flocon de neige. Le volume et le pan du canal et les effets de son insert fonctionnent comme avant.
3. Pour modifier de nouveau les notes ou l'instrument, faites un clic droit sur le canal et choisissez `Dégeler`. L'instrument revient avec les réglages qu'il avait.

Un canal figé ne peut pas être modifié : ses steps sont verrouillés et la fenêtre de son instrument ne s'ouvre pas. Si les notes, le tempo ou l'arrangement changent après le gel, l'étiquette change de couleur et l'audio fait avant continue de jouer jusqu'à ce que vous choisissiez `Re-geler`. Si les fichiers audio rendus sont introuvables, c'est l'instrument qui joue.

Figer ne fonctionne que sur les canaux. Un insert du Mixer ne peut pas être figé.

## Autres éléments du menu du canal

`Piano Roll`, `Renommer et colorer`, `Remplacer` (échanger l'instrument), `Cloner`, `Delete`, `Arpégiateur`, `Point de boucle`, `Graver le MIDI dans`, `Trier les canaux`, `Sélectionner les canaux inutilisés` et `Réglages du canal`.

## À savoir

- Seule la case `+ ajouter un canal` ouvre le menu d'ajout.
- Le rack n'a pas de pattern à lui. Changer le pattern actuel n'importe où change ce que le rack affiche.
- Une ligne qui affiche une miniature de notes ne peut pas être éditée en steps. Utilisez le Piano Roll pour elle.
- Les touches numériques n'atteignent que les dix premiers canaux.
- Les touches du tableau demandent que le rack ait le focus. Cliquez d'abord sur le rack.

## Voir aussi

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Raccourcis](/wiki/shortcuts/)
