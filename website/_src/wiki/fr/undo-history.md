---
source: 9f36c60cc7
---
![La fenêtre Historique des annulations : six étapes de Start à Add notes, avec Muet : Kick comme étape actuelle](/assets/img/wiki/history.webp)

Chaque modification du projet est une étape de l'historique d'annulation : une note ajoutée, un clip déplacé, un fader, un changement de routage, le tempo. `Éditer › Historique des annulations...` (`Ctrl+Alt+H`) montre les étapes sous forme de liste. Indépendamment, LYNX SHARE enregistre des copies de sauvegarde du projet pendant que vous travaillez.

## Revenir à une étape antérieure

1. Choisissez `Éditer › Historique des annulations...`, ou appuyez sur `Ctrl+Alt+H`.
2. La liste montre les étapes, la plus récente en bas. La première ligne, `Début`, est le projet tel qu'il a été ouvert.
3. Cliquez sur une étape. Le projet revient à l'état juste après elle, et les étapes suivantes passent en gris.
4. Cliquez sur une étape grise pour avancer de nouveau.
5. Faites une nouvelle modification pour continuer depuis l'étape choisie. Les étapes grises disparaissent alors.

## Les touches d'annulation

`Ctrl+Z` ne fonctionne pas comme dans la plupart des programmes. Il annule la dernière étape, et si on appuie de nouveau, il la rétablit : il alterne donc entre les deux derniers états. Pour remonter plus loin, utilisez `Ctrl+Alt+Z`.

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| `Annuler` | `Ctrl+Z` | Annule la dernière étape. Après une annulation, il rétablit l'étape suivante, et l'entrée de menu affiche `Annuler (rétablir)` |
| `Annuler en arrière` | `Ctrl+Alt+Z` | Recule toujours, d'une étape par appui |
| `Rétablir` | `Ctrl+Y` ou `Ctrl+Shift+Z` | Avance |
| `Historique des annulations...` | `Ctrl+Alt+H` | Ouvre la liste |

Après chaque annulation ou rétablissement, la barre d'aide nomme l'étape et indique le niveau, par exemple `Level 2/3`.

Si vous préférez le comportement habituel, activez `Mode d'annulation alternatif (annulation classique, au lieu d'annuler/rétablir sur le même raccourci)` dans `Options › Réglages généraux...`. `Ctrl+Z` recule alors toujours.

## Ce qui est une étape et ce qui n'en est pas une

- Les modifications du projet sont des étapes : notes, patterns, clips, canaux, niveaux et routage du Mixer, effets, automation, tempo, états muets.
- Un enregistrement est une étape. La fusion d'une partie dans une [session en ligne](/wiki/online-sessions/) aussi, de même que `Créer un comp avec ces prises`.
- Ce qui ne modifie pas le projet n'est pas une étape : zoomer, faire défiler, déplacer des fenêtres, et les réglages du programme lui-même.
- L'enregistrement et l'export ne s'annulent pas. Les fichiers qu'ils ont écrits restent.
- Les fenêtres des plugins propres à LYNX SHARE ont leur propre annulation et rétablissement pour les changements de ce plugin, à côté des réglages A et B.

## Réglages

Les deux se trouvent dans `Options › Réglages généraux...`.

| Réglage | Ce qu'il fait |
|---|---|
| `Niveaux d'annulation maximum` | Combien d'étapes sont conservées : 25, 50, 100, 200, 500 ou 1000. La valeur par défaut est 100 |
| `Mode d'annulation alternatif` | Fait de `Ctrl+Z` une annulation classique |

## Sauvegardes

Par défaut, LYNX SHARE enregistre une copie de sauvegarde du projet ouvert toutes les 2 minutes, et garde les 10 plus récentes par projet. Une sauvegarde est un fichier `.lynx` normal dans le dossier `autosave` à l'intérieur du dossier des réglages, nommé d'après le projet avec `(autosaved on ...)` suivi de la date et de l'heure.

Chaque fois que vous enregistrez par-dessus un fichier de projet existant, la version remplacée est copiée dans le même dossier avec `(overwritten on ...)` dans son nom.

| Commande | Ce qu'elle fait |
|---|---|
| `Fichier › Revenir à la dernière sauvegarde...` | Remplace le projet ouvert par sa sauvegarde la plus récente, après confirmation. Le fichier de projet lui-même ne change que lorsque vous enregistrez |
| `Fichier › Ouvrir le dossier de sauvegarde automatique` | Montre les sauvegardes dans le gestionnaire de fichiers |
| `Fichier › Enregistrer une nouvelle version` (`Ctrl+Alt+S`) | Enregistre sous `Song_2`, `Song_3` et ainsi de suite, à côté du fichier actuel |

La section `Sauvegarde` de `Options › Réglages généraux...` contient ces réglages :

| Réglage | Ce qu'il fait |
|---|---|
| `Enregistrer une copie de sauvegarde` | `Jamais`, ou un intervalle en minutes |
| `Sauvegardes conservées par projet` | 1, 3, 5, 10, 20 ou 50 |
| `Attendre pour les sauvegardes pendant la lecture du morceau ou le jeu sur un clavier MIDI` | Retient une sauvegarde pour qu'elle ne vous interrompe pas |
| `Attente maximale entre sauvegardes` | 5 à 60 minutes. Une sauvegarde est faite après ce délai même pendant que vous jouez |

Si la dernière session s'est terminée de façon inattendue, le démarrage suivant propose d'ouvrir la sauvegarde la plus récente de cette session.

## Bon à savoir

- L'historique n'est pas enregistré avec le projet. Ouvrir un projet ou en démarrer un nouveau le vide.
- Quand l'historique est plein, les étapes les plus anciennes sont abandonnées.
- Dans une session en ligne en direct, annuler et rétablir ne ramènent que vos propres modifications, jamais celles des autres.
- Avec `Enregistrer une copie de sauvegarde` sur `Jamais`, les copies faites quand vous enregistrez par-dessus un fichier sont tout de même conservées.
- Le dossier des réglages est `%APPDATA%\LYNX` sous Windows, `~/Library/Application Support/LYNX` sous macOS et `~/.config/LYNX` sous Linux.

## Voir aussi

- [Raccourcis](/wiki/shortcuts/)
- [Réglages](/wiki/settings/)
- [Formats de fichiers](/wiki/file-formats/)
- [Dépannage](/wiki/troubleshooting/)
- [Ce que seul LYNX SHARE fait : la protection contre les plantages](/why/#crash)
