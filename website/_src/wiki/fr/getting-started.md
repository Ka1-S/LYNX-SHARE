---
source: a6d9063ec3
---
LYNX SHARE est une DAW gratuite et open source pour Windows, macOS et Linux. Elle fonctionne avec des patterns : vous les programmez dans le Channel Rack et le Piano Roll, les arrangez dans la Playlist et les mixez dans le Mixer. Elle ouvre les projets FL Studio (`.flp`) et les sets Ableton Live (`.als`), et enregistre dans son propre format `.lynx`.

C'est une alpha. Attendez-vous à des bugs, et enregistrez souvent.

## La mettre en route

1. Consultez le tableau ci-dessous pour vérifier que votre système est pris en charge.
2. Téléchargez-la et installez-la. [Installation](/wiki/install/) donne les étapes pour chaque système.
3. Lancez-la. Un court assistant de configuration choisit la langue, teste la sortie audio et liste les dossiers de plugins, qui sont ensuite analysés en arrière-plan. Vous pouvez l'ignorer et le rouvrir plus tard avec `Options › Assistant de configuration (langue, audio, dossiers de plugins)...`.
4. Si vous n'entendez rien, allez à [Configuration audio](/wiki/audio-setup/).
5. Lisez [Visite de la fenêtre](/wiki/tour/), puis faites [Votre premier morceau](/wiki/first-song/).

## Ce qu'il vous faut

| Système | Configuration requise |
|---|---|
| Windows | Windows 10 ou 11, 64 bits |
| macOS | macOS 11 Big Sur ou plus récent, Intel ou Apple silicon |
| Linux | x86_64 avec glibc 2.35 ou plus récente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch ou Manjaro à jour), X11 ou XWayland |

Vous n'avez pas besoin d'interface audio. LYNX SHARE joue par le périphérique audio que votre système utilise déjà. Vous n'avez pas besoin non plus de FL Studio. S'il est installé sur le même ordinateur, LYNX SHARE trouve ses samples et utilise les mêmes fichiers de plugins pour les projets `.flp`.

Les plugins tiers sont facultatifs. LYNX SHARE a ses propres instruments et effets, listés dans le [catalogue](/instruments-and-effects/).

## Enregistrer votre travail

`Ctrl+S` enregistre, `Ctrl+Shift+S` enregistre sous un nouveau nom, et `Ctrl+Alt+S` enregistre une nouvelle version (Song_2, Song_3 et ainsi de suite). Il y a une sauvegarde automatique, accessible depuis `Fichier › Ouvrir le dossier de sauvegarde automatique` et `Fichier › Revenir à la dernière sauvegarde...`.

`Fichier › Nouveau à partir d'un modèle` propose quatre points de départ : Empty, Basic beat, Trap with 808 et House.

## Le projet de démonstration

L'installateur et le zip Windows, le `.dmg` macOS et le `.tar.gz` Linux incluent un projet de démonstration, `Demo.lynx`. Ouvrez-le avec `Fichier › Ouvrir...` (`Ctrl+O`). Sous Windows, il se trouve dans le dossier d'installation, à côté de `LYNX.exe`.

Vous pouvez aussi le générer depuis un terminal. Voir [Ligne de commande](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Où aller ensuite

| Vous voulez | Lisez |
|---|---|
| Apprendre chaque fenêtre | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Ouvrir un projet d'un autre programme | [Venir de FL Studio](/wiki/coming-from-fl-studio/), [Venir d'Ableton Live](/wiki/coming-from-ableton/) |
| Utiliser vos plugins VST, CLAP, LV2 ou AU | [Plugins](/wiki/plugins/) |
| Chercher une touche | [Raccourcis](/wiki/shortcuts/) |
| Régler un problème | [Dépannage](/wiki/troubleshooting/) et la [FAQ](/faq/) |

## À savoir

- Le fichier du programme s'appelle `LYNX.exe` sous Windows et `LYNX` sous macOS et Linux. Le dossier des réglages s'appelle aussi `LYNX`.
- Sous Windows et macOS, le premier lancement peut afficher un avertissement du système, car la version Windows n'est pas encore signée et la version macOS n'est pas notariée. [Installation](/wiki/install/) explique sur quoi cliquer.
- L'interface est en anglais par défaut sur un système dont LYNX SHARE n'a pas la langue. Vous pouvez la changer dans `Options › Réglages généraux...`. Une nouvelle langue s'applique après un redémarrage.
- Les packs FLEX des projets FL Studio sont chiffrés et ne sont jamais ouverts. Ces canaux reçoivent un patch de synthé choisi d'après le nom du preset.

## Voir aussi

- [Installation](/wiki/install/)
- [Configuration audio](/wiki/audio-setup/)
- [Visite de la fenêtre](/wiki/tour/)
- [Votre premier morceau](/wiki/first-song/)
- [Téléchargement](/download/)
