---
source: 7ca5e689db
---
![La palette d'ajout rapide : les instruments à gauche, les effets à droite, chacun marqué intégré ou moteur FL intégré](/assets/img/wiki/quickadd.webp)

LYNX SHARE fournit ses propres instruments et effets : un projet joue donc sans aucun plugin tiers. Il y en a trois sortes : les originaux de LYNX SHARE (un synthé, un sampler, un lecteur de SoundFont, des batteries, des égaliseurs, des compresseurs, des limiteurs, des réverbs, des délais), des moteurs écrits pour jouer les réglages des plugins natifs de FL Studio, et des effets qui remplacent des plugins tiers connus.

Chacun a sa propre page dans le [catalogue](/instruments-and-effects/). `Aide › Plugins intégrés (liste en ligne)...` ouvre la liste avec l'état de chaque moteur.

## Ajouter un instrument et un effet

1. Appuyez sur `F6` pour afficher le Channel Rack.
2. Cliquez sur la case `+ ajouter un canal` sous la dernière ligne et choisissez un instrument. Le menu les groupe par catégorie.
3. Appuyez sur `F9` pour afficher le Mixer et sélectionnez l'insert par lequel passe le canal.
4. Cliquez sur un slot vide du panneau d'effets et choisissez un effet. Sa fenêtre s'ouvre.
5. Cliquez sur le nom du slot pour masquer ou réafficher cette fenêtre.

## Le sélecteur de plugins

Appuyez sur `F8` pour le sélecteur de plugins. Il couvre l'écran et montre chaque générateur et chaque effet sous la forme d'une image de son éditeur, avec les catégories en onglets en bas.

| Commande | Touche | Effet |
|---|---|---|
| Recherche | Taper | Assombrit les plugins qui ne correspondent pas |
| Parcourir les plugins | `Up` / `Down` | Déplace la surbrillance |
| Ajouter | `Enter`, double-clic ou clic droit | Un générateur devient un nouveau canal. Un effet va dans l'insert du Mixer sélectionné |
| Glisser une image | | Sur le Channel Rack pour un nouveau canal, sur un canal pour remplacer son instrument, ou sur un insert ou un slot du Mixer |
| Fermer | `Esc` ou `F8` | Ferme le sélecteur sans rien ajouter |

Les images des plugins propres à LYNX SHARE sont dessinées par LYNX SHARE en arrière-plan. Un plugin tiers affiche une simple tuile avec le nom de son éditeur tant que son interface n'a pas été ouverte une fois.

`Éditer › Palette d'ajout rapide...` ouvre à la place une liste simple : les instruments à gauche, les effets à droite, filtrés pendant la frappe. `Tab` change de liste et `Enter` ajoute l'élément en surbrillance. La base de plugins du [Browser](/wiki/browser/) (`Shift+F8`) liste les mêmes plugins sous forme d'arbre.

![La base de plugins du Browser avec le dossier des moteurs FL ouvert](/assets/img/wiki/browser-plugins.webp)

## Presets

La fenêtre d'un effet intégré a une barre de presets dans son en-tête.

| Contrôle | Effet |
|---|---|
| Flèches | Preset précédent et suivant |
| Nom du preset | Ouvre le menu : `Presets LYNX SHARE`, `Vos presets`, `Presets FL Studio` là où ils existent, `Enregistrer le preset...` et `Afficher le dossier des presets` |
| `A` / `B` | Deux réglages à comparer. Le bouton à côté copie celui que vous entendez vers l'autre |

Le nom du preset, ainsi que les réglages A et B, sont enregistrés avec le projet.

## Ce que veut dire « ouvre les réglages de FL »

Les plugins natifs de FL Studio ne sont jamais chargés. LYNX SHARE a son propre moteur pour chacun d'eux, et ce moteur lit les réglages que le plugin FL a enregistrés. Cela marche à deux endroits :

- Un projet `.flp`. Chaque générateur et chaque effet FL s'ouvre sur son moteur LYNX SHARE avec les réglages enregistrés, et ses contrôles restent modifiables et automatisables.
- Un preset `.fst`. Double-cliquez sur un preset de synthé dans le Browser pour obtenir un nouveau canal, ou glissez un preset d'effet sur un insert du Mixer. Un clic droit sur un slot d'effet rempli donne `Presets FL`, qui liste les presets trouvés dans votre installation de FL Studio.

La fidélité de chaque moteur à l'original est indiquée moteur par moteur, d'Exact à Placeholder. Les niveaux sont expliqués dans [Venir de FL Studio](/wiki/coming-from-fl-studio/).

## Moteurs qui demandent votre installation de FL Studio

LYNX SHARE ne contient ni code, ni audio, ni données d'onde d'Image-Line. Certains moteurs ont besoin des ondes, samples, impulsions ou tables qui appartiennent au plugin original, et les lisent dans votre propre installation de FL Studio pendant qu'ils tournent.

| Type | Moteurs |
|---|---|
| Instruments | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Effets | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Si les fichiers sont introuvables, l'ajout d'un tel moteur peut échouer avec un message indiquant que les fichiers de données d'une installation de FL Studio sont introuvables ou illisibles.

## Remplaçants des plugins tiers

Quand un projet utilise un effet tiers qui n'est pas installé, un effet LYNX SHARE de même rôle prend sa place dans le slot : une réverb pour une réverb, un délai pour un délai.

- Le slot affiche le nom du plugin d'origine avec un badge LYNX SHARE.
- Quand l'état enregistré est lisible, ses réglages sont traduits. Sinon, le remplaçant part d'un preset par défaut.
- Le plugin d'origine et ses réglages restent dans le projet. Une fois le plugin installé, le projet s'ouvre de nouveau avec lui.
- Faites un clic droit sur un slot remplaçant et choisissez `Contourner tous les substituts LYNX SHARE de ce projet` pour écouter le projet sans eux.

L'interrupteur est `Remplacer les plugins manquants par des équivalents LYNX SHARE` dans `Options › Réglages généraux...`, activé par défaut. Dans un nouveau projet, les mêmes effets se trouvent dans le menu du slot vide, sous `Équivalents LYNX SHARE`.

## À savoir

- Les instruments manquants ne sont pas encore remplacés. Un plugin d'instrument manquant devient un synthé intégré choisi d'après le nom du canal.
- L'automation des paramètres du plugin d'origine est conservée mais ne pilote pas le remplaçant.
- Les packs FLEX sont chiffrés et ne sont jamais ouverts. Un canal FLEX reçoit un patch de synthé choisi d'après le nom de son preset.
- Un insert a 10 slots d'effet.

## Voir aussi

- [Catalogue des instruments et effets](/instruments-and-effects/)
- [Plugins tiers](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
