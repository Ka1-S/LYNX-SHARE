---
source: be0fc22373
---
![La partie basse du Gestionnaire de plugins, avec sa note disant que chaque fichier de plugin est examiné dans un processus séparé](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE héberge des plugins d'instruments et d'effets d'autres éditeurs. Il les trouve en analysant les dossiers de plugins, et `Options › Gestionnaire de plugins...` montre ce qu'il a trouvé. Les instruments et effets propres à LYNX SHARE sont décrits dans [Instruments et effets intégrés](/wiki/built-in/).

## Analyser et ajouter un plugin

1. Choisissez `Options › Rechercher des plugins`. La progression s'affiche dans la barre d'aide et vous pouvez continuer à travailler.
2. Si le plugin se trouve dans un dossier à part, ouvrez `Options › Gestionnaire de plugins...`, cliquez sur `Dossiers de plugins...`, ajoutez le dossier, puis cliquez sur `Rechercher de nouveaux plugins`.
3. Appuyez sur `F8` pour le sélecteur de plugins et tapez une partie du nom du plugin.
4. Double-cliquez dessus ou appuyez sur `Enter`. Un instrument devient un nouveau canal. Un effet va dans l'insert du Mixer sélectionné.

Au premier démarrage, l'assistant de configuration liste les dossiers de plugins et les analyse en arrière-plan. `Options › Assistant de configuration (langue, audio, dossiers de plugins)...` le rouvre. LYNX SHARE cherche aussi de nouveaux plugins quelques secondes après chaque démarrage.

## Formats par système

| Système | Formats hébergés |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

Le VST2 n'est pas hébergé sous macOS. Sous Linux, les plugins Windows fonctionnent grâce à [yabridge](https://github.com/robbert-vdh/yabridge) et Wine, que vous installez vous-même : ajoutez le dossier que yabridge synchronise comme dossier de plugins.

## Le Gestionnaire de plugins

| Contrôle | Effet |
|---|---|
| Zone de recherche | Filtre par nom, éditeur, format ou fichier |
| `Tous`, `Instruments`, `Effets`, `Favoris`, `En échec` | Choisissent quels plugins sont listés |
| Colonne étoile | Marque un favori |
| Colonne `Bits` | 32, 64 ou 32+64 (Windows) |
| Colonne `État` | `OK`, `Introuvable` ou `Échec` |
| `Rechercher de nouveaux plugins` | Cherche les fichiers qui ne sont pas encore dans la liste |
| `Tout réanalyser...` | Oublie les fichiers en échec et examine de nouveau chaque fichier |
| `Dossiers de plugins...` | Modifie les dossiers analysés |
| `Ajouter un fichier de plugin...` | Ajoute un fichier de plugin |

Chaque fichier de plugin est examiné dans un processus séparé. Un fichier qui plante ou se bloque pendant 30 secondes est marqué comme en échec et ignoré ensuite. Sous Windows, les fichiers en échec sont listés avec la raison dans `%APPDATA%\LYNX\scan-failures.log`.

Les dossiers standard sont analysés, plus ceux que vous ajoutez :

- Windows : `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` et `Steinberg\VSTPlugins` sous Program Files, et les dossiers que FL Studio utilise s'il est installé.
- macOS : `VST3`, `Components` et `CLAP` dans `~/Library/Audio/Plug-Ins` et `/Library/Audio/Plug-Ins`.
- Linux : `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` et les mêmes dossiers sous `/usr/lib` et `/usr/local/lib`.

## Fenêtres de plugin ancrées et détachées

L'éditeur d'un plugin s'ouvre dans la fenêtre principale, comme les autres panneaux. `Détaché` dans le menu de la fenêtre du plugin donne à ce plugin sa propre fenêtre, et `Épingler au premier plan` dans le même menu garde une fenêtre détachée au-dessus des autres fenêtres de LYNX SHARE.

`Détacher tous les nouveaux plugins` dans `Options › Réglages généraux...` ouvre en mode détaché chaque plugin que vous ajoutez. `Outils › Basculer tous les éditeurs de plugins en` règle `Ancré` ou `Détaché` pour tous les plugins du projet.

## Exécuter les plugins dans un processus séparé

Par défaut, un plugin tourne à l'intérieur de LYNX SHARE : un plugin qui plante peut donc faire tomber le programme. En option, chaque plugin tourne dans son propre processus.

1. Ouvrez `Options › Réglages généraux...`.
2. Activez `Exécuter les plugins dans un processus séparé (protège LYNX SHARE des plantages)`.
3. Rouvrez le projet pour l'appliquer aux plugins déjà ouverts.

Si un tel plugin plante ou cesse de répondre, il est coupé, un message apparaît et LYNX SHARE continue de tourner. `Recharger` dans ce message relance le plugin avec les réglages de la dernière sauvegarde, sans rouvrir le projet. L'éditeur du plugin s'ouvre dans sa propre fenêtre et ne peut être ni ancré ni épinglé.

Pour ne le faire que pour un seul plugin, utilisez `Exécuter dans un processus séparé` ou `Always run <plugin> in a separate process` dans le menu d'enveloppe de la fenêtre du plugin. Quand l'éditeur d'un plugin plante, le message qui apparaît propose aussi le second.

Cela fonctionne sous Windows et Linux, et depuis la 0.1.25 sous macOS.

Un plugin qui plante dans LYNX SHARE est désactivé, et son message propose aussi `Recharger`. Plus tard, utilisez `Recharger le plugin qui a planté` dans le menu du canal ou dans le menu de l'emplacement d'effet.

## Plugins 32 bits (Windows uniquement)

Sous Windows, les plugins VST2 et VST3 32 bits passent par un pont (`LYNX-bridge32.exe`). Le pont n'ajoute aucune latence. Un plugin qui plante dedans s'arrête tout seul pendant que LYNX SHARE continue de jouer. L'éditeur d'un plugin ponté s'ouvre dans sa propre fenêtre et ne peut être ni ancré ni épinglé. Quand un plugin est installé à la fois en 32 et en 64 bits, `Taille en bits` dans les `Réglages du wrapper` de la fenêtre du plugin choisit lequel est chargé.

## Moniteur de performances des plugins

![Le moniteur de performances des plugins : une ligne par instrument et effet avec sa part du temps de traitement](/assets/img/wiki/plugin-monitor.webp)

`Outils › Moniteur de performances des plugins...` liste chaque instrument et effet avec le temps de traitement qu'il prend : maintenant, en moyenne et à son pic. 100 % signifie un cœur de processeur occupé pendant toute la durée de l'audio.

| Réglage | Effet |
|---|---|
| `Filtre` | Filtre par plugin, canal ou insert |
| `Plugins uniquement` | Masque les instruments et effets propres à LYNX SHARE |
| `Figer` | Arrête la mise à jour des chiffres |
| `Réinitialiser à la lecture` | Repart de zéro pour les moyennes et les pics à chaque début de lecture |
| `Réinitialiser` | Efface maintenant les moyennes et les pics |

## Envoyer les sorties d'un instrument vers des inserts séparés

Un plugin instrument à plusieurs sorties peut jouer chacune d'elles sur son propre insert du Mixer. Dans la fenêtre du plugin, cliquez sur `Réglages du wrapper`, puis sur l'onglet `Traitement`. Sous `Connexions`, chaque sortie a un voyant qui l'active ou la désactive et un numéro : sur combien d'inserts après l'insert du canal elle joue. `Assigner les sorties automatiquement` donne à chaque sortie l'insert suivant, et `Réinitialiser` revient aux valeurs par défaut.

## Le dossier Plugin database

Les dossiers de la Plugin database du [Browser](/wiki/browser/), du sélecteur de plugins et des menus de plugins sont un dossier sur le disque : `LYNX/Plugin database` dans votre dossier Documents. Pour l'ouvrir, faites un clic droit sur `Base de plugins` dans le Browser et choisissez `Ouvrir`.

- `Effets` et `Générateurs` contiennent un fichier `.lynxplugin` par plugin. Créez des dossiers et des sous-dossiers (jusqu'à 4 niveaux) et déplacez-y les fichiers.
- Renommez un fichier pour renommer le plugin dans les menus. Supprimez un fichier pour retirer le plugin des menus. Il n'est pas désinstallé.
- `Installés` contient une entrée pour chaque plugin. Copiez-en une dans `Effets` ou `Générateurs` pour récupérer un plugin.

## Ouvrir un projet sans ses plugins

`Fichier › Ouvrir sans plugins tiers...` ouvre un projet LYNX SHARE avec tous les plugins tiers désactivés, pour un projet qui ne s'ouvre pas ou qui plante au chargement. Les réglages enregistrés des plugins sont conservés. `Fichier › Réactiver les plugins...` les réactive.

## Quand un plugin n'est pas installé

LYNX SHARE cherche d'abord le même plugin par son identifiant, puis par son fichier, puis par son nom. Un projet enregistré avec le VST2 s'ouvre avec le VST3 du même produit, et inversement, mais le plugin joue alors avec ses réglages par défaut.

Si un effet n'est pas du tout installé, un effet LYNX SHARE de même rôle prend son emplacement et l'original reste dans le projet. Quand les réglages enregistrés du plugin sont lisibles, le remplaçant démarre avec des réglages équivalents. L'interrupteur est `Remplacer les plugins manquants par des équivalents LYNX SHARE` dans `Options › Réglages généraux...`. Voir [Instruments et effets intégrés](/wiki/built-in/). `Aide › Fichiers manquants...` liste les plugins qui ont été remplacés.

## À savoir

- La fenêtre d'un plugin hébergé a une barre de presets : les programmes propres au plugin, vos presets, `Enregistrer le preset...`, `Charger un fichier de preset...` et `Exporter un fichier de preset...`. Les fichiers `.fxp`, `.fxb` et `.vstpreset` ne se chargent que dans un plugin qui tourne à l'intérieur de LYNX SHARE, pas dans un processus séparé.
- Le moniteur ne mesure que lorsque sa fenêtre est ouverte.
- Quand la fenêtre d'un plugin a le focus, une touche que le plugin n'utilise pas joue le clavier de saisie en piano. Sous Windows, si le bouton de clavier de l'en-tête de la fenêtre est désactivé, LYNX SHARE traite les touches avant le plugin, raccourcis compris.
- Le menu de paramètres propre à un plugin VST3 commence par les éléments de LYNX SHARE, comme `Créer un clip d'automation`. Voir [Automation](/wiki/automation/).
- `MPE` sous `MIDI` dans les `Réglages du wrapper` d'un plugin envoie chaque note sur son propre canal MIDI. Voir [Contrôleurs MIDI](/wiki/midi-controllers/).
- Les instruments manquants ne sont pas encore remplacés. Un plugin d'instrument manquant devient un synthé intégré choisi d'après le nom du canal.

## Voir aussi

- [Instruments et effets intégrés](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Dépannage](/wiki/troubleshooting/)
