---
source: a3748d4a2a
---
Chaque titre ci-dessous est un problème. Si le vôtre n'y figure pas, signalez-le comme décrit à la fin.

## Démarrer le programme

### Windows : SmartScreen avertit au premier lancement

L'exécutable n'est pas encore signé : SmartScreen peut donc afficher un avertissement la première fois que vous le lancez.

1. Cliquez sur `More info`.
2. Cliquez sur `Run anyway`.

L'installateur ne demande pas de droits administrateur.

### macOS : « ne peut pas être ouverte »

L'app n'est pas notariée par Apple et n'a pas de signature Developer ID : le premier lancement demande donc une étape de plus.

1. Glissez LYNX SHARE dans `Applications`.
2. Faites un clic droit (Control-clic) dessus et choisissez `Ouvrir`.
3. Cliquez de nouveau sur `Ouvrir`.

Sous macOS 15 Sequoia ou plus récent, essayez de l'ouvrir une fois, puis allez dans `System Settings › Privacy & Security` et cliquez sur `Open Anyway`. Ou, dans le Terminal :

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS demande l'accès au microphone la première fois que vous enregistrez.

### Linux : l'AppImage ne démarre pas

LYNX SHARE demande un système x86_64 avec glibc 2.35 ou plus récente (Ubuntu 22.04, Debian 12, Fedora 36, Arch ou Manjaro à jour) et X11 ou XWayland.

1. Rendez le fichier exécutable et lancez-le, avec les deux commandes ci-dessous.
2. S'il ne démarre toujours pas, installez libfuse2. Le paquet s'appelle `libfuse2` sur Ubuntu 22.04, `libfuse2t64` sur Ubuntu 24.04, `fuse2` sur Arch et `fuse-libs` sur Fedora.
3. Si vous préférez ne pas l'installer, téléchargez plutôt le `.tar.gz`, extrayez-le et lancez `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` installe LYNX SHARE pour votre utilisateur sous `~/.local`, avec une entrée de menu. `./install.sh --uninstall` le supprime.

## Son

### Pas de son, ou mauvais périphérique audio

1. Ouvrez `Options › Réglages audio...`.
2. Choisissez un pilote.
3. Choisissez un périphérique de sortie.

| Système | Pilotes |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO et le pilote ASIO LYNX intégré |
| macOS | Core Audio |
| Linux | PipeWire (natif), JACK et ALSA. Au premier démarrage, LYNX SHARE ouvre le premier de ces pilotes qui a des périphériques |

ASIO est un modèle de pilote Windows et n'existe que sous Windows. `Options › Assistant de configuration (langue, audio, dossiers de plugins)...` teste aussi le périphérique audio. Voir [Configuration audio](/wiki/audio-setup/).

### Linux : le son craque

Le thread audio devrait tourner avec une priorité temps réel.

1. Ouvrez `Options › Réglages audio...` et lisez `État`. Il indique si le thread a obtenu la priorité temps réel.
2. Si ce n'est pas le cas, installez rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` ou `sudo pacman -S rtkit`), ou ajoutez-vous au groupe `audio` avec une limite temps réel, comme dans les configurations JACK.
3. Fermez la session et rouvrez-la.

Les détails sont dans [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## Plugins

### Un plugin plante

Exécutez-le dans son propre processus, pour que seul le plugin s'arrête.

1. Ouvrez `Options › Réglages généraux...`.
2. Activez `Exécuter les plugins dans un processus séparé (protège LYNX SHARE des plantages)`.
3. Rouvrez le projet.

Un plugin qui a planté ou cessé de répondre dans son propre processus est coupé. Enregistrez le projet et rouvrez-le pour recharger le plugin. Voir [Plugins tiers](/wiki/plugins/).

### Un plugin n'est pas dans la liste

1. Choisissez `Options › Rechercher des plugins`.
2. S'il manque toujours, ouvrez `Options › Gestionnaire de plugins...`, cliquez sur `Dossiers de plugins...` et ajoutez le dossier du plugin.
3. Regardez l'onglet `En échec`. Un fichier de plugin qui échoue à l'analyse est ignoré. Sous Windows, la raison est dans `%APPDATA%\LYNX\scan-failures.log`.
4. Cliquez sur `Tout réanalyser...` pour réessayer les fichiers en échec.

### Un projet utilise un plugin qui n'est pas installé

Un effet est remplacé par un effet LYNX SHARE de même rôle, et ses réglages sont conservés dans le projet. `Aide › Fichiers manquants...` liste ce qui manque. Voir [Instruments et effets intégrés](/wiki/built-in/).

### La vidéo ne fonctionne pas sous Linux

Les pistes vidéo et `Export with video` demandent FFmpeg. LYNX SHARE indique comment l'installer.

## Rapports de plantage

![La question du rapport de plantage affichée au démarrage suivant, avec Send, Show what will be sent et Don't send](/assets/img/wiki/crash-report.webp)

Les rapports de plantage sont facultatifs. Après un plantage ou un blocage, le démarrage suivant demande s'il faut envoyer un rapport.

| Commande | Effet |
|---|---|
| `Afficher ce qui sera envoyé` | Montre le texte exact |
| `Signaler plutôt sur GitHub` | Ouvre le formulaire de ticket de GitHub, prérempli. Vous joignez les fichiers vous-même |
| `Ouvrir le dossier contenant le rapport` | Montre les fichiers du rapport sur votre ordinateur |
| `Include file names (projects, samples)` | Désactivé, seule l'extension de chaque fichier est envoyée |
| `Toujours envoyer sans demander` | Envoie les futurs rapports sans poser la question |

`Rapports de plantage` dans `Options › Réglages généraux...` règle `Ask` (par défaut), `Toujours envoyer` ou `Ne jamais envoyer`.

Un rapport contient la version de LYNX SHARE, le système d'exploitation, le CPU, les cartes graphiques et leurs pilotes, la taille de l'écran, le pilote et le périphérique audio, le fichier de plantage ou de blocage, les 60 dernières étapes de la session et les noms et versions des plugins tiers qui étaient chargés.

Il ne contient ni audio, ni projets, ni samples, ni le nom de votre ordinateur, ni votre nom d'utilisateur. Le serveur ne stocke aucune adresse IP. Les identifiants envoyés sont listés dans `crash-reports\sent.log`, dans le dossier des réglages. Pour faire supprimer un rapport, ouvrez un ticket avec son identifiant. Le texte complet se trouve dans la [politique de confidentialité](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Signaler un problème

![La fenêtre Report a problem : une zone de description, une adresse e-mail facultative et le bouton Send](/assets/img/wiki/report-problem.webp)

1. Choisissez `Aide › Signaler un problème...`.
2. Décrivez ce qui s'est passé. L'adresse e-mail est facultative, pour le cas où vous voulez une réponse.
3. Laissez `Joindre les derniers fichiers de plantage / blocage et les étapes de cette session` cochée si le problème était un plantage.
4. Cliquez sur `Afficher ce qui sera envoyé` pour vérifier le texte.
5. Cliquez sur `Envoyer`. Rien n'est envoyé avant cela.

Vous pouvez aussi ouvrir un ticket sur [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## À savoir

- Après un plantage, des copies de sauvegarde de votre projet se trouvent dans le dossier de sauvegarde automatique de LYNX SHARE.
- Quand un plugin a participé au plantage, la question propose d'exécuter désormais ce plugin dans un processus séparé.
- Le dossier des réglages est `%APPDATA%\LYNX` sous Windows, `~/Library/Application Support/LYNX` sous macOS et `~/.config/LYNX` sous Linux.

## Voir aussi

- [Configuration audio](/wiki/audio-setup/)
- [Plugins tiers](/wiki/plugins/)
- [Installation](/wiki/install/)
- [FAQ](/faq/)
- [Protection contre les plantages](/why/#crash)
