---
source: 8a120ab7de
---
Chaque version propose des builds pour Windows, macOS et Linux sur la [page des versions](https://github.com/Ka1-S/LYNX-SHARE/releases). Les fichiers sont construits à partir des sources étiquetées par GitHub Actions et listés avec leur SHA-256 dans `SHA256SUMS.txt`. La page [Téléchargement](/download/) renvoie vers les plus récents.

## Installer sur Windows

Windows 10 ou 11, 64 bits.

1. Téléchargez `LYNX-<version>-setup-x64.exe`.
2. Lancez-le. Il s'installe pour votre utilisateur seulement et ne demande pas de droits administrateur.
3. SmartScreen peut afficher un avertissement, car l'exécutable n'est pas encore signé. Choisissez `More info`, puis `Run anyway`.
4. Lancez LYNX SHARE depuis le menu Démarrer ou le raccourci du bureau.

L'installateur fait de LYNX SHARE le programme qui ouvre les projets `.lynx`. Il propose LYNX SHARE sous *Ouvrir avec* pour les fichiers `.flp`, sans changer le programme par défaut de FL Studio.

Il y a deux autres moyens :

| Moyen | Fichier ou lieu | Effet |
|---|---|---|
| Zip portable | `LYNX-<version>-windows-x64.zip` | Extrayez-le n'importe où et lancez `LYNX.exe`. Rien n'est installé. |
| Microsoft Store | [LYNX SHARE sur le Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Le Store l'installe et le tient à jour. |

## Installer sur macOS

macOS 11 Big Sur ou plus récent. Un seul `LYNX-<version>-macos-universal.dmg` universel couvre Intel et Apple silicon.

1. Ouvrez le `.dmg` et glissez LYNX SHARE dans *Applications*.
2. L'app n'est pas notariée par Apple et n'a pas de signature Developer ID : le premier lancement demande donc une étape de plus. Faites un clic droit (ou Control-clic) sur LYNX SHARE dans *Applications*, choisissez *Ouvrir*, puis *Ouvrir* de nouveau.
3. Sous macOS 15 Sequoia ou plus récent, essayez de l'ouvrir une fois, puis allez dans *Réglages Système › Confidentialité et sécurité* et cliquez sur *Ouvrir quand même*.

L'alternative par le Terminal :

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS demande l'accès au microphone la première fois que vous enregistrez.

## Installer sur Linux

x86_64 avec glibc 2.35 ou plus récente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch ou Manjaro à jour). LYNX SHARE a besoin de X11 ou XWayland. L'audio passe par PipeWire, JACK ou ALSA, et il n'y a rien de plus à installer pour cela.

AppImage (`LYNX-<version>-x86_64.AppImage`) :

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Certaines distributions ont besoin de libfuse2 pour lancer les AppImage (`libfuse2` sur Ubuntu 22.04, `libfuse2t64` sur 24.04, `fuse2` sur Arch, `fuse-libs` sur Fedora). Au premier démarrage, l'assistant de configuration propose d'ajouter LYNX SHARE au menu des applications et d'ouvrir les fichiers `.lynx` avec lui. Vous pouvez changer cela plus tard dans `Options › Réglages généraux...`, ou depuis un terminal avec `./LYNX-*.AppImage --menu-entry on` (ou `désactivé`).

Si vous préférez ne pas installer libfuse2, utilisez `LYNX-<version>-linux-x86_64.tar.gz` : extrayez-le et lancez `./install.sh`. Cela installe LYNX SHARE pour votre utilisateur sous `~/.local`, avec une entrée de menu, une icône et le type de fichier `.lynx`.

## Où sont gardés les réglages

| Système | Dossier des réglages |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (ou sous `$XDG_CONFIG_HOME`) |

Le dossier contient vos options, la langue choisie (`language.txt`), vos raccourcis (`keys.json`), vos dossiers de plugins (`plugin-folders.txt`) et la liste des plugins analysés (`plugins.xml`).

## Mettre à jour

Au démarrage, LYNX SHARE demande à GitHub le numéro de la version la plus récente. Rien sur vous n'est envoyé. Quand une nouvelle version est sortie, une barre affiche `LYNX SHARE <version> is available` avec un bouton `Nouveautés` et un bouton de mise à jour. `Aide › Rechercher des mises à jour...` interroge à tout moment.

| Build | Comment il se met à jour |
|---|---|
| Installateur Windows | `Mettre à jour` télécharge le nouvel installateur, interroge sur les modifications non enregistrées, installe et redémarre |
| Zip portable Windows, macOS | `Mettre à jour` télécharge le nouveau fichier et l'affiche dans son dossier |
| AppImage Linux | `Mettre à jour` met à jour le fichier AppImage sur place. Gear Lever et AppImageUpdate peuvent faire de même de l'extérieur |
| `.tar.gz` Linux | `Télécharger...` ouvre la page de téléchargement |
| Microsoft Store | Le Store le met à jour. Ce build n'a pas de vérification de mise à jour propre |

Pour désactiver la vérification, ouvrez `Options › Réglages généraux...` et décochez `Rechercher automatiquement les mises à jour`.

## Désinstaller

- Installateur Windows : désinstallez LYNX SHARE depuis la liste *Applications et fonctionnalités* de Windows.
- Zip portable : supprimez le dossier que vous avez extrait.
- `.tar.gz` Linux : lancez `./install.sh --uninstall`.

## À savoir

- Les builds Windows ne sont pas signés tant que la demande de signature de code du projet n'est pas acceptée. L'app macOS n'a qu'une signature ad hoc. Les paquets Linux ne sont pas signés.
- Pour vérifier un téléchargement, comparez son SHA-256 avec la ligne de `SHA256SUMS.txt` sur la page de la version.
- L'installateur, le zip, le `.dmg` et le `.tar.gz` incluent le projet de démonstration `Demo.lynx`.
- Fermer la barre de mise à jour avec son bouton de fermeture ignore cette version.

## Voir aussi

- [Pour commencer](/wiki/getting-started/)
- [Configuration audio](/wiki/audio-setup/)
- [Dépannage](/wiki/troubleshooting/)
- [Téléchargement](/download/)
- [Mises à jour](/updates/)
