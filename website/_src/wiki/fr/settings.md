---
source: ab2c2f14eb
---
![La page Général de la fenêtre Réglages, avec les onglets de page en haut](/assets/img/wiki/settings-general.webp)

La fenêtre Réglages a huit pages : Audio, MIDI, Général, Thème, Fichiers, Extras LYNX, Projet et Infos. Les six premières appartiennent au programme. Projet et Infos appartiennent au projet ouvert et sont enregistrées avec lui. Chaque page a sa propre entrée en haut du menu `Options`.

## Modifier un réglage

1. Ouvrez le menu `Options` et choisissez une page, par exemple `Réglages généraux...`.
2. Cliquez sur un autre onglet en haut pour passer à une autre page.
3. Modifiez le réglage. Il s'applique tout de suite, sauf la langue et la police de l'interface, qui s'appliquent au redémarrage de LYNX SHARE.

`F10` ouvre la page MIDI et `F11` la page Infos.

## Audio

`Options › Réglages audio...`. Pilote, périphériques, fréquence d'échantillonnage, taille du tampon et enregistrement. [Configuration audio](/wiki/audio-setup/) décrit cette page. Sous `Enregistrement`, `Format de fichier` propose maintenant aussi `WAV 16 bits (la fréquence d'échantillonnage du périphérique)`.

## MIDI

![La page MIDI : périphériques d'entrée, périphériques de sortie avec synchro maître, et synchro externe](/assets/img/wiki/settings-midi.webp)

`Options › Réglages MIDI...`

| Réglage | Ce qu'il fait |
|---|---|
| Périphériques d'entrée | Chaque périphérique peut être désactivé, n'écouter qu'un seul canal MIDI, et jouer un canal fixe du Channel Rack au lieu des canaux sélectionnés |
| `Type de contrôleur` | Pour chaque périphérique d'entrée : `Détecter automatiquement`, `Contrôleur générique` ou un profil de contrôleur, comme Mackie Control. La ligne à côté nomme le port qui reçoit les voyants et l'écran |
| `Contrôleur MPE` | À la fin de la liste des canaux MIDI d'un périphérique. Chaque note arrive avec son propre bend, son propre timbre et sa propre pression. Une seconde liste règle les canaux membres |
| `Actualiser la liste des périphériques` | Cherche de nouveau les périphériques et relit vos profils de contrôleur |
| `Dossier des profils de contrôleur` | Ouvre le dossier de vos propres profils de contrôleur (`*.lynxctl`) |
| `Envoyer la synchro maître` | Envoie l'horloge MIDI avec Song Position Pointer, ou le MIDI Time Code, à un périphérique de sortie. `Décalage (ms)` l'envoie plus tard ou plus tôt |
| `Envoyer all notes off` | Envoie all notes off au périphérique à l'initialisation et après un export |
| `Synchro sur clock externe` | LYNX SHARE suit l'horloge MIDI de l'entrée choisie : tempo, start, continue, stop et position |
| `Vélocité` | Pour les notes des périphériques MIDI : comme jouée, douce, dure ou fixe |
| `Canal du mode performance` | Les notes de ce canal lancent des clips de la Playlist en mode Performance |
| `Saut vers un marqueur du song` | Les notes de ce canal déplacent le morceau vers un marqueur au lieu d'être jouées |
| `Reprise (mode takeover)` | Un contrôleur ne déplace un paramètre qu'après avoir atteint sa valeur actuelle |
| `La pédale contrôle la fin des notes` | Actif par défaut : les pédales de sustain et de sostenuto retiennent les notes. Désactivé, la pédale va aux plugins |
| `Utiliser une manette de jeu` | Une manette de jeu pilote le transport et peut être liée comme un contrôleur MIDI. Désactivé par défaut |
| Liens | Les liens de contrôleurs de ce projet, puis les globaux, avec `Retirer le lien` et `Tout retirer` |

## Général

`Options › Réglages généraux...`

| Réglage | Ce qu'il fait |
|---|---|
| `Style de disposition` | Flux par patterns (par défaut), flux par lancement de clips ou flux simple |
| `Modèle par défaut`, `Projet au démarrage` | Le modèle qui s'ouvre avec `Fichier › Nouveau`, et ce qui s'ouvre au démarrage : `Projet vide`, `Modèle par défaut` ou `Dernier projet utilisé`. `Fichier › Enregistrer comme modèle...` ajoute les vôtres |
| `Clips de session des sets Live` | Où vont les clips de session d'un set Live importé : la grille de clips, ou la Playlist après l'arrangement |
| `Langue`, `Police de l'interface` | Appliqués au prochain démarrage |
| `Taille de l'interface`, `Densité des menus` | Échelle de toute l'interface, et menus compacts ou aérés |
| `Formes d'onde` | Épurées (la couleur du clip) ou aux couleurs spectrales, au choix avec les canaux stéréo séparés |
| `Noms des notes`, `Touches noires` | Noms anglais, germaniques ou en solfège, avec dièses ou bémols |
| `Style des plugins` | L'aspect des fenêtres des plugins intégrés : automatique, ou un seul style pour tous les plugins |
| `Animations`, `Fréquence d'animation` | Les animations de l'interface, et la fréquence de mise à jour de la tête de lecture et des vumètres |
| `Dessiner avec le GPU (Direct2D)` | À désactiver si le pilote graphique plante ou si l'écran scintille |
| Démarrage | Écran de démarrage, son de démarrage, et recherche de nouveaux plugins en arrière-plan à chaque démarrage |
| Édition | Clavier de saisie vers piano, éditeurs de canal laissés ouverts, mémorisation du temps de positionnement, bouclage des clips de pattern étirés |
| `Détacher tous les nouveaux plugins` | Actif : la fenêtre d'un plugin que vous ajoutez s'ouvre détachée, au-dessus des panneaux. Désactivé (par défaut) : elle s'ouvre ancrée dans la fenêtre principale |
| `Peinture monophonique dans le piano roll` | Actif : l'outil Peinture remplace les notes sous chaque nouvelle note, et Shift peint par-dessus. Désactivé : l'inverse |
| `Sélectionner automatiquement les modules liés` | Donner le focus à la fenêtre d'un plugin sélectionne sa piste du Mixer. Actif par défaut |
| `Désactiver par défaut la priorité du clavier de saisie` | Pour les plugins que vous ajoutez à partir de là. Désactivé par défaut |
| `Niveaux d'annulation maximum` | 25 à 1000 |
| `Gérer les messages` | Sous `Conseils et avertissements`. Les messages cochés sont affichés. Décochez-en un pour passer sa question |
| Sauvegarde | Fréquence des copies de sauvegarde, nombre conservé par projet, et `Ouvrir le dossier des sauvegardes` |
| `Exécuter les plugins dans un processus séparé` | Un plugin qui plante n'entraîne pas LYNX SHARE avec lui. Désactivé par défaut |
| `Remplacer les plugins manquants par des équivalents LYNX SHARE` | Actif par défaut. Quand un projet utilise un plugin tiers qui n'est pas installé, le remplaçant de LYNX SHARE se charge avec des réglages équivalents quand il en existe un. Le projet garde le plugin d'origine et le réutilise dès qu'il est installé |
| `Limiter le nombre d'éditeurs de plugins ouverts` | 25 fenêtres d'éditeur de plugin au plus restent ouvertes. Actif par défaut |
| `Rapports de plantage` | Demander après un plantage ou un blocage (par défaut), toujours envoyer ou ne jamais envoyer |
| `Rechercher automatiquement les mises à jour` | La recherche de mises à jour au démarrage, avec `Rechercher maintenant` |

## Thème

`Options › Thème (couleurs, arrière-plan)...`

| Réglage | Ce qu'il fait |
|---|---|
| Liste des thèmes | Choisit un preset. `Importer...` et `Exporter...` lisent et écrivent des fichiers `.lynxtheme`, `Enregistrer le preset...` conserve le thème actuel, `Par défaut` le réinitialise |
| `Couleur d'accent`, `Arrière-plan`, `Espace de travail` | Les trois couleurs d'un thème. La couleur de l'espace de travail peut suivre automatiquement l'arrière-plan |
| Image de l'espace de travail | Un PNG, JPEG ou GIF derrière les fenêtres, avec `Ajustement de l'image`, `Opacité de l'image` et `Assombrir` |
| Son de démarrage | Votre propre son de 30 secondes ou moins, ou celui de LYNX SHARE |

## Fichiers

`Options › Réglages des fichiers (dossiers de recherche)...`

| Réglage | Ce qu'il fait |
|---|---|
| Dossiers de recherche des samples manquants | La fenêtre Fichiers manquants cherche d'elle-même dans ces dossiers et leurs sous-dossiers |
| Dossiers du Browser | Affichés dans la liste des dossiers du Browser, et aussi explorés pour les samples manquants |
| Dossiers de plugins | Analysés à la recherche de plugins par `Options › Rechercher des plugins` |

## Extras LYNX

`Options › Extras LYNX...`. Les ajouts propres à LYNX SHARE réunis au même endroit. Tous sont désactivés par défaut. Chaque interrupteur est le même réglage que dans le menu ou la page que nomme sa ligne.

| Réglage | Ce qu'il fait |
|---|---|
| `Mesure de sonie à côté du Master (LYNX)` | Sonie à court terme et intégrée (LUFS) et true peak du Master, avec un bouton Reset |
| `N'afficher que les inserts utilisés` | Masque les inserts du Mixer que rien n'utilise |
| `Garder les liens vers les inserts supprimés, grisés (LYNX)` | Supprimer un insert garde les liens qui y allaient, pour pouvoir les reconnecter |
| `Les groupes du Mixer suivent la Playlist (LYNX)` | Les groupes du Mixer reprennent les groupes de pistes de la Playlist |
| `Couloirs d'automation` | L'automation peut se placer dans des couloirs sous une piste de la Playlist |
| `Règle : mesures du morceau (LYNX)` | La règle du Piano Roll affiche les numéros de mesure de l'endroit du morceau où le pattern joue |
| `Afficher la valeur d'une commande près de la souris au survol (LYNX)` | La valeur s'affiche à côté du curseur, pas seulement dans la barre d'aide |
| `Après l'enregistrement en boucle, seul le dernier passage est lu` | Les passes précédentes restent dessous, en muet |
| `W affiche ou masque la fenêtre de l'appareil sélectionné` | Une touche pour la Vue des appareils |
| `Sélectionner un appareil sélectionne son insert dans le Mixer` | Le Mixer suit l'appareil choisi dans la Vue des appareils |
| `Utiliser une manette de jeu` | Le même interrupteur que sur la page MIDI |

## Projet

![La page Projet : réglages temporels, groove et accordage, base de temps et options de la Playlist](/assets/img/wiki/settings-project.webp)

`Options › Réglages du projet (tempo, signature rythmique)...`

| Réglage | Ce qu'il fait |
|---|---|
| `Réglages temporels` | Division du temps (temps par mesure, steps par temps) ou signature rythmique classique (numérateur et dénominateur) |
| `Tempo`, `Temps par mesure` | Le tempo du projet et les temps dans une mesure |
| `Swing`, `Hauteur générale` | Le taux de swing global et la hauteur générale en cents |
| `Fondus enchaînés automatiques` | Les clips audio qui se chevauchent sur une piste font un fondu enchaîné sur le chevauchement |
| `Fondu du comp` | La durée du fondu enchaîné utilisée pour le comping de prises, en millisecondes |
| `Jouer les notes tronquées dans les clips` | Une note qui commence avant un clip de pattern est jouée depuis le début du clip |
| `Déclic rapide pour les groupes de coupure` | Rampe plus rapide quand les groupes de coupure coupent des notes |
| `Loi de panoramique` | Circulaire (centre à 0 dB, +3 dB du côté vers lequel on panote) ou Triangulaire (le côté le plus fort garde son niveau). Pour les inserts du Mixer et les canaux Sampler |

## Infos

`Options › Infos du projet...`. Titre, auteur, genre, lien web et commentaires du projet, et `Afficher cette page à l'ouverture du projet`. Le bas indique quand le projet a été créé et le temps de travail.

## Bon à savoir

- La base de temps est fixée à 960 ticks par noire. Les projets de FL Studio y sont convertis.
- Les changements de signature rythmique au fil du morceau ne sont pas sur la page Projet. Ajoutez un marqueur de signature rythmique sur la règle de la Playlist.
- `Exécuter les plugins dans un processus séparé` s'applique aux plugins ouverts à partir de là. Rouvrez le projet pour déplacer les autres.
- Les réglages de mise à jour sont absents de la version Microsoft Store, parce que le Store la met à jour.
- Les raccourcis se trouvent dans `Options › Gérer les raccourcis...`, pas sur une page des Réglages.
- Certains interrupteurs sont dans des menus, pas dans cette fenêtre. `Faible latence à l'enregistrement` est dans le menu des options d'enregistrement à côté du bouton d'enregistrement et dans le menu `Enregistrement sur disque` du Mixer. `Style du clavier` est dans le menu View du Piano Roll.
- La Plugin database est un dossier, `Base de plugins`, dans le dossier `LYNX` de vos documents. Vous pouvez l'organiser dans le gestionnaire de fichiers.

## Voir aussi

- [Configuration audio](/wiki/audio-setup/)
- [Contrôleurs MIDI](/wiki/midi-controllers/)
- [Raccourcis](/wiki/shortcuts/)
- [Plugins](/wiki/plugins/)
- [Dispositions](/wiki/layouts/)
