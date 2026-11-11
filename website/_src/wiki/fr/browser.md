---
source: 8df354b49e
---
![Le Browser à gauche avec ses trois onglets, la zone de recherche, l'arborescence des dossiers et la bande d'aperçu](/assets/img/wiki/browser-tree.webp)

Le Browser liste vos dossiers de samples, le contenu du projet en cours et les plugins que LYNX SHARE connaît. Appuyez sur `Alt+F8` pour l'afficher ou le masquer. `F8` seul ouvre le sélecteur de plugins, une vue séparée en plein écran qui montre tous les instruments et tous les effets.

## Charger un sample dans le projet

1. Appuyez sur `Alt+F8` pour afficher le Browser.
2. Dans l'onglet `TOUT`, cliquez sur un dossier pour l'ouvrir.
3. Cliquez sur un fichier audio pour l'écouter. `Up` et `Down` parcourent les fichiers et lisent un aperçu de chacun.
4. Double-cliquez sur le fichier pour obtenir un nouveau canal avec le sample dans le [Channel Rack](/wiki/channel-rack/).
5. Ou glissez le fichier sur la [Playlist](/wiki/playlist/) pour obtenir un clip audio.

## Onglets

| Onglet | Contenu |
|---|---|
| ALL | Le projet en cours, la base de plugins et tous les dossiers de samples |
| PROJECT | Uniquement ce que le projet en cours utilise : patterns, effets, générateurs, automation, fichiers audio |
| STARRED | Les fichiers et dossiers que vous avez marqués d'une étoile (clic droit, puis `Favori`) |

## Parcourir et écouter

- Cliquez sur un dossier pour l'ouvrir ou le fermer. Un clic droit ouvre un menu (tout replier, afficher dans le gestionnaire de fichiers).
- Cliquez sur un fichier audio pour l'écouter. La bande d'aperçu sous la liste affiche sa forme d'onde. Cliquez sur la bande pour réécouter le fichier, ou glissez-en depuis elle comme s'il s'agissait du fichier.
- Le panneau au-dessus de la bande d'aperçu indique la date, la taille et le chemin du fichier sélectionné.

## Utiliser un sample

| Action | Effet |
|---|---|
| Double-clic | Nouveau canal avec le sample |
| Glisser vers le Channel Rack | Nouveau canal, ou déposez sur une ligne pour le charger à cet endroit |
| Glisser vers la Playlist | Clip audio |
| `Enter` | Charge le fichier dans le canal sélectionné |
| `Shift+Up` / `Shift+Down` | Passe au fichier précédent ou suivant et le charge dans le canal sélectionné |

La dernière action permet d'essayer différents samples sur place pendant que le pattern joue.

## Autres types de fichiers

- **Projet** : un double-clic l'ouvre.
- **Fichier MIDI** : un double-clic l'ouvre comme un projet.
- **SoundFont ou SFZ** : un clic en donne un aperçu, un double-clic crée un canal LYNX Soundfont (ou le charge dans celui qui est sélectionné).
- **Preset de Mixer** : un double-clic le charge sur les inserts du Mixer sélectionnés, ou glissez-le sur un insert.
- **Preset d'effet** : glissez-le sur un insert du Mixer.

## Base de plugins

L'entrée Base de plugins liste les instruments et effets intégrés de LYNX SHARE ainsi que les plugins installés. `Shift+F8` y saute directement.

- Double-cliquez sur un instrument pour l'ajouter comme nouveau canal, ou glissez-le sur le Channel Rack.
- Double-cliquez sur un effet pour l'ajouter à l'insert du [Mixer](/wiki/mixer/) sélectionné, ou glissez-le sur un insert ou sur un slot d'effet.
- `F2` renomme le plugin sélectionné.
- Faites un clic droit sur un plugin et choisissez `Déplacer vers le dossier` pour ranger les plugins dans des dossiers. Le menu des slots du Mixer et le menu d'ajout du Channel Rack listent alors chaque dossier de premier niveau.

## L'onglet PROJECT

Les patterns listés ici se sélectionnent d'un clic pour en faire le pattern actuel, s'ouvrent d'un double-clic dans le [Piano Roll](/wiki/piano-roll/) ou se glissent dans la Playlist. La section du projet en cours liste aussi l'historique des annulations : cliquez sur une étape pour y revenir.

## Recherche

Appuyez sur `Ctrl+F` pour placer le curseur dans la zone de recherche. Elle cherche dans le dossier sélectionné (ou dans tous les dossiers) et en dessous. Dans l'onglet PROJECT, elle cherche dans les éléments du projet.

Tous les mots doivent correspondre. La zone accepte aussi `OR`, un signe moins pour exclure un mot et les jokers `*` :

```
kick -loop
snare OR clap
hat*.wav
```

Les dossiers sont indexés en arrière-plan. `Actualiser l'index de recherche` dans le menu du Browser reconstruit l'index.

## Vos propres dossiers

Utilisez `Ajouter un dossier` dans la liste des dossiers du Browser pour ajouter un dossier de samples, de projets ou de SoundFonts. `Options › Réglages des fichiers (dossiers de recherche)` contient les dossiers de recherche.

## Touches

Les flèches, `Enter` et `F2` demandent que le Browser ait le focus.

| Commande | Touche | Effet |
|---|---|---|
| Afficher le Browser | `Alt+F8` | Affiche ou masque le Browser |
| Sélecteur de plugins | `F8` | Ouvre le sélecteur de plugins en plein écran |
| Base de plugins | `Shift+F8` | Saute à l'entrée Base de plugins |
| Recherche | `Ctrl+F` | Place le curseur dans la zone de recherche |
| Parcourir et écouter | `Up` / `Down` | Sélectionne le fichier précédent ou suivant et le lit |
| Fermer un dossier | `Left` | Ferme un dossier ou remonte à son parent |
| Ouvrir le dossier | `Right` | Ouvre un dossier |
| Charger | `Enter` | Charge le fichier dans le canal sélectionné |
| Parcourir et charger | `Shift+Up` / `Shift+Down` | Passe au fichier précédent ou suivant et le charge dans le canal sélectionné |
| Renommer le plugin | `F2` | Renomme le plugin sélectionné dans la Base de plugins |

## À savoir

- Retirer un dossier de la liste ne le supprime pas du disque.
- `F8` et `Alt+F8` sont deux choses différentes. `F8` ouvre le sélecteur de plugins, `Alt+F8` le Browser.
- Les résultats de recherche viennent d'un index construit en arrière-plan. `Actualiser l'index de recherche` dans le menu du Browser le reconstruit.
- `Enter` charge le fichier dans le canal sélectionné. Un double-clic crée un nouveau canal.

## Voir aussi

- [Channel Rack](/wiki/channel-rack/)
- [Plugins tiers](/wiki/plugins/)
- [Formats de fichiers](/wiki/file-formats/)
- [Historique des annulations](/wiki/undo-history/)
- [Instruments et effets](/instruments-and-effects/)
