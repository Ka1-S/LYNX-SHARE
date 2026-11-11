---
source: 98cfcfdbfe
---
![La fenêtre Gérer les raccourcis : une liste d'actions avec recherche et les touches de chacune](/assets/img/wiki/shortcuts.webp)

Ce sont les touches par défaut de la 0.1.26 alpha. `Aide › Raccourcis` (`F1`) affiche la fiche des raccourcis dans l'application. `Options › Gérer les raccourcis...` ouvre l'éditeur de l'image, où vous les modifiez.

> Sous macOS, l'application affiche les touches avec leurs noms Mac : Ctrl s'affiche ⌘ et Alt s'affiche ⌥.

## Modifier un raccourci

1. Choisissez `Options › Gérer les raccourcis...`.
2. Tapez une action ou une touche, comme `Ctrl+L`, dans la zone de recherche. La liste à côté limite la recherche à un endroit, et `Modifiés uniquement` montre ceux que vous avez modifiés.
3. Cliquez sur l'action.
4. Cliquez sur `Modifier` pour remplacer ses touches, ou sur `Ajouter un raccourci` pour lui en donner une seconde, puis appuyez sur les nouvelles touches. `Esc` annule.
5. Si les touches sont déjà utilisées au même endroit, un message nomme l'autre action et vous permet de les déplacer.

`Retirer` efface les touches de l'action. `Rétablir par défaut` rétablit une action et `Tout réinitialiser...` les rétablit toutes. Vos modifications sont enregistrées dans `keys.json`, dans le dossier des réglages (`%APPDATA%\LYNX` sous Windows, `~/Library/Application Support/LYNX` sous macOS, `~/.config/LYNX` sous Linux). `Ouvrir keys.json` ouvre ce fichier.

## Transport et enregistrement

| Touche | Effet |
|---|---|
| `Space` | Lecture, ou arrêt et retour au point de départ de la lecture |
| `Ctrl+Space` | Lecture ou pause |
| `Shift+Space` | Lecture, ou arrêt au début du morceau ou du pattern |
| `L` | Passe du mode Pattern au mode Song et inversement |
| `R` | Active ou désactive l'enregistrement |
| `Shift+R` | Enregistre l'audio maintenant : arme l'insert sélectionné et enregistre |
| `Ctrl+M` | Active ou désactive le métronome |
| `Ctrl+P` | Active ou désactive le décompte avant l'enregistrement |
| `Ctrl+I` | Active ou désactive l'attente de la première note pour l'enregistrement |
| `Ctrl+B` | Active ou désactive le mélange des notes enregistrées |
| `Ctrl+E` | Active ou désactive l'édition step |
| `Ctrl+H` | Coupe le son (all notes off) |
| `Ctrl+T` | Active ou désactive le clavier de saisie en piano |
| `Alt+Numpad *` / `Alt+Numpad /` | Saute au marqueur de morceau suivant ou précédent |

Certaines de ces touches font autre chose dans un éditeur qui a le focus. Dans le Piano Roll, `Ctrl+M` importe un fichier MIDI et `Ctrl+B` duplique. Dans la Playlist, `Ctrl+P` est le mode Performance.

## Fichiers et édition

| Touche | Effet |
|---|---|
| `Ctrl+N` | Nouveau projet |
| `Ctrl+O` | Ouvrir un projet |
| `Ctrl+S` | Enregistrer |
| `Ctrl+Shift+S` | Enregistrer sous |
| `Ctrl+Alt+S` | Enregistrer une nouvelle version |
| `Ctrl+R` | Exporter l'audio |
| `Ctrl+Shift+R` | Exporter un fichier MP3 |
| `Ctrl+Shift+M` | Exporter un fichier MIDI |
| `Ctrl+Z` | Annuler. Pressé de nouveau, il rétablit cette étape |
| `Ctrl+Alt+Z` | Annuler en remontant, étape après étape |
| `Ctrl+Y` ou `Ctrl+Shift+Z` | Rétablir |
| `Ctrl+Alt+H` | Historique des annulations |
| `Ctrl+F` | Rechercher dans le Browser |

## Fenêtres

| Touche | Effet |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Sélecteur de plugins |
| `Shift+F8` | Base de plugins (dans le Browser) |
| `Enter` | Agrandit ou restaure la Playlist |
| `F10` | Réglages MIDI |
| `F11` | Infos du projet |
| `F12` | Ferme toutes les fenêtres (plugins, réglages de canal, outils) |
| `Alt+F12` | Ferme toutes les fenêtres de plugins |
| `Ctrl+F12` | Ferme toutes les fenêtres sauf celle qui a le focus |
| `Alt+F7` | Clavier tactile |
| `Ctrl+F5` | Tap tempo |
| `F1` | Aide-mémoire des raccourcis |

## Vue du clip et Vue des appareils

Ces touches ne fonctionnent que dans le flux par lancement de clips.

| Touche | Ce qu'elle fait |
|---|---|
| `Ctrl+Alt+L` | Afficher ou masquer la vue de détail |
| `Ctrl+Alt+3` | Vue du clip |
| `Ctrl+Alt+4` | Vue des appareils |
| `Shift+Tab` | Passer de la Vue du clip à la Vue des appareils |
| `Left` / `Right` | Vue des appareils : sélectionner l'appareil précédent, suivant |
| `Del` ou `Backspace` | Vue des appareils : supprimer l'effet sélectionné |

## Patterns

| Touche | Effet |
|---|---|
| `Numpad +` / `Numpad -` | Pattern suivant, précédent |
| `Numpad 1` à `Numpad 9` | Sélectionne le pattern 1 à 9 |
| `F4` | Pattern vide suivant, et le nommer |
| `Ctrl+F4` | Pattern vide suivant, sans le nommer |
| `Shift+F4` | Premier pattern vide, et le nommer |
| `F2` | Renomme et colore le pattern |
| `Ctrl+Shift+C` | Clone le pattern |
| `Ctrl+Shift+Ins` | Insère un pattern vide avant l'actuel |
| `Ctrl+Shift+Del` | Supprime le pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Monte, descend le pattern |

## Channel Rack

| Touche | Effet |
|---|---|
| `Up` / `Down` | Sélectionne le canal au-dessus, en dessous |
| `Shift+Up` / `Shift+Down` | Étend la sélection |
| `Alt+Up` / `Alt+Down` | Déplace les canaux sélectionnés |
| `Ctrl+A` | Sélectionne tous les canaux |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Coupe, copie, colle les steps des canaux sélectionnés |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Décale les steps vers la gauche, vers la droite |
| `Alt+C` | Clone les canaux sélectionnés |
| `Alt+Del` | Supprime les canaux sélectionnés |
| `Alt+G` | Groupe les canaux sélectionnés |
| `Ctrl+L` | Envoie les canaux sélectionnés vers des inserts du Mixer libres |
| `Ctrl+K` | Affiche ou masque les couloirs de graphe des steps |
| `1` à `0` | Coupe ou rétablit le son des canaux 1 à 10 |
| `Ctrl+1` à `Ctrl+0` | Met en solo les canaux 1 à 10 |

## Piano Roll

| Touche | Effet |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Crayon, Pinceau, Pinceau de batterie, Gomme, Muet, Couteau, Lasso, Zoom, Écouter |
| `S` | Active ou désactive les notes slide (nouvelles notes) |
| `G` / `K` | Canal précédent, suivant |
| `H` / `J` | Canal précédent, suivant qui contient des notes |
| `Ctrl+A` | Tout sélectionner |
| `Ctrl+D` ou `Esc` | Désélectionner |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copier, couper, coller |
| `Ctrl+B` | Dupliquer |
| `Del` | Supprime la sélection |
| `Up` / `Down` | Déplace la sélection d'un demi-ton |
| `Ctrl+Up` / `Ctrl+Down` | Déplace la sélection d'une octave |
| `Left` / `Right` | Déplace la sélection d'un pas de grille |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Décale les notes en rotation dans la mesure |
| `Backspace` | Désactive ou active le magnétisme |
| `Ctrl+Q` | Quantification rapide (débuts et longueurs) |
| `Shift+Q` | Quantifie les débuts |
| `Ctrl+U` | Hache sur la grille |
| `Ctrl+G` | Joint les notes qui se touchent |
| `Ctrl+L` | Legato |
| `Shift+D` | Réduit les notes à la grille |
| `Ctrl+P` | Active ou désactive le porta pour la sélection |
| `Alt+M` / `Shift+Alt+M` | Coupe, rétablit le son de la sélection |
| `Alt+V` | Active ou désactive les notes fantômes |
| `Shift+I` | Inverse la sélection |
| `Shift+G` / `Alt+G` | Groupe, dégroupe les notes sélectionnées |
| `Alt+C` | Colore les notes sélectionnées avec la couleur de note |
| `Shift+C` | Sélectionne les notes de la même couleur |
| `Shift+N` | Tampon d'accord : aucun (notes seules) |
| `Ctrl+Ins` / `Ctrl+Del` | Insère, supprime de l'espace (la sélection de temps) |
| `Ctrl+M` | Importe un fichier MIDI |
| `Ctrl+Shift+M` | Exporte en fichier MIDI |
| `PageUp` / `PageDown` | Zoome, dézoome |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoome sur 1, 4, 16 mesures |
| `Shift+4` | Zoome pour tout afficher |
| `Shift+5` | Zoome sur la sélection |
| `Shift+0` | Centre la vue sur la tête de lecture |

### Outils de notes

Chaque touche ouvre la boîte de dialogue d'un outil de notes du Piano Roll.

| Touche | Effet |
|---|---|
| `Alt+A` | Arpéger |
| `Alt+S` | Strum |
| `Alt+Y` | Retourner |
| `Alt+R` | Rendre aléatoire |
| `Alt+X` | Mise à l'échelle des niveaux |
| `Alt+K` | Limiter à une plage |
| `Alt+Q` | Quantifier |
| `Alt+U` | Hacher |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articuler |
| `Alt+E` | Riff machine |
| `Alt+P` | Générer une progression d'accords |
| `Ctrl+Alt+Y` | Relancer le dernier script |

## Playlist

| Touche | Effet |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Crayon, Pinceau, Gomme, Muet, Couteau, Lasso, Zoom, Écouter, Glissement |
| `Ctrl+A` | Tout sélectionner |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copier, couper, coller |
| `Ctrl+B` | Dupliquer |
| `Del` | Supprime les clips sélectionnés |
| `Shift+Del` | Supprime les clips sélectionnés et referme les trous |
| `Shift+Left` / `Shift+Right` | Déplacer les clips sélectionnés d'un pas de magnétisme vers la gauche, la droite |
| `Shift+Up` / `Shift+Down` | Déplacer les clips sélectionnés d'une piste vers le haut, le bas |
| `Alt+Left` / `Alt+Right` | Ajuster les clips sélectionnés vers la gauche, la droite d'un pas fin, sans magnétisme. `Shift+Alt` fait de même |
| `Ctrl+D` | Désélectionner les clips et le temps |
| `Shift+I` | Inverser la sélection |
| `Shift+C` | Sélectionner les clips de la source sélectionnée |
| `Shift+S` | Active ou désactive l'édition ripple |
| `Backspace` | Désactive ou active le magnétisme |
| `Alt+M` / `Shift+Alt+M` | Coupe, rétablit le son des clips sélectionnés |
| `Shift+M` | Active ou désactive l'étirement de l'audio quand on redimensionne des clips audio |
| `Ctrl+Shift+V` | Rend uniques les clips sélectionnés |
| `Ctrl+G` | Fusionne des clips de pattern |
| `Ctrl+Shift+G` | Fusionne des clips de pattern similaires |
| `Ctrl+Alt+G` | Fusionne des clips d'automation |
| `Ctrl+Shift+B` | Clone les pistes de Playlist sélectionnées |
| `Ctrl+Q` | Quantifie les débuts des clips |
| `7` / `8` | Baisse, monte d'un demi-ton la hauteur des clips audio sélectionnés |
| `9` | Inverse les clips audio sélectionnés |
| `Ctrl+Ins` / `Ctrl+Del` | Insère, retire la longueur de la sélection de temps sur toutes les pistes |
| `Ctrl+Alt+Ins` | Trancher les clips et insérer de l'espace |
| `Ctrl+Alt+Home` | Redimensionner les clips par la gauche, activé ou désactivé |
| `Ctrl+Left` / `Ctrl+Right` | Sélectionne le temps précédent, suivant |
| `Ctrl+Enter` | Sélectionne le temps autour de la sélection |
| `Alt+T` | Ajoute un marqueur de temps |
| `Ctrl+T` | Ajoute un marqueur automatiquement |
| `Shift+Alt+T` | Ajoute un changement de signature |
| `Shift+T` | Place un marqueur de boucle |
| `Ctrl+Alt+C` | Consolide la sélection de la Playlist |
| `Ctrl+P` | Active ou désactive le mode Performance |
| `PageUp` / `PageDown` | Zoome, dézoome |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoome sur 1, 4, 16 mesures |
| `Shift+4` | Zoome pour tout afficher |
| `Shift+5` | Zoome sur la sélection |
| `Shift+0` | Centre la vue sur la tête de lecture |

## Mixer

| Touche | Effet |
|---|---|
| `Alt+Left` / `Alt+Right` | Déplace les inserts sélectionnés vers la gauche, vers la droite |
| `F2` | Renomme et colore l'insert sélectionné |
| `S` | Met en solo les inserts sélectionnés |
| `Alt+S` | Met en solo les inserts sélectionnés avec les inserts qui les alimentent |
| `Ctrl+L` | Lie les canaux sélectionnés à l'insert sélectionné |
| `Ctrl+Shift+L` | Lie les canaux sélectionnés à des inserts à partir de celui qui est sélectionné |
| `Alt+L` | Sélectionne les canaux liés à l'insert sélectionné |
| `Del` | Supprimer les inserts sélectionnés. LYNX SHARE demande d'abord confirmation |
| `Ctrl+E` | Ouvre l'éditeur audio |
| `Shift+E` | Ouvre l'éditeur audio et enregistre |
| `Alt+R` | Rend les inserts armés en fichiers wave |
| `Shift+Alt+R` | Rend les inserts sélectionnés en fichiers wave |

## Browser

| Touche | Effet |
|---|---|
| `Up` / `Down` | Fichier précédent, suivant (avec aperçu) |
| `Shift+Up` / `Shift+Down` | Fichier précédent, suivant dans le canal sélectionné |
| `Enter` | Charge le fichier dans le canal sélectionné |
| `Left` | Ferme le dossier, ou remonte au dossier parent |
| `Right` | Ouvre le dossier |
| `F2` | Renomme le plugin sélectionné (Base de plugins) |

## À savoir

- Tant que le piano du clavier de saisie est activé, ses touches jouent des notes et la plupart des raccourcis à une lettre sont suspendus (`L` fonctionne toujours). Désactivez-le avec `Ctrl+T`.
- Un raccourci fonctionne soit partout, soit dans une seule fenêtre. Celui qui appartient à une fenêtre l'emporte sur les mêmes touches valables partout, tant que cette fenêtre a le focus.
- Certaines actions n'ont pas de touche par défaut, par exemple la palette d'ajout rapide. Donnez-leur-en une dans l'éditeur.
- Dans la Playlist, les touches qui déplacent des clips déplacent tous les clips quand aucun n'est sélectionné. Le pas fin de `Alt+Left` et `Alt+Right` est d'un pixel au zoom actuel.
- Les raccourcis de LYNX SHARE fonctionnent aussi quand la fenêtre d'un plugin a le focus, par exemple `Space`. Les lettres et chiffres simples vont d'abord au plugin, et `Esc` ferme sa fenêtre.

## Voir aussi

- [Visite de la fenêtre](/wiki/tour/)
- [Réglages](/wiki/settings/)
- [Contrôleurs MIDI](/wiki/midi-controllers/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
