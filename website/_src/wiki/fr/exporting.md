---
source: da5e41c322
---
![La fenêtre d'export avec WAV sélectionné : étendue, queue et fichiers à gauche, format à droite, options de mixage en dessous](/assets/img/wiki/export.webp)

Tout ce qui sort de LYNX SHARE passe par `Fichier › Exporter`. Les formats audio (WAV, MP3, OGG et FLAC) partagent une seule fenêtre d'export. Le MIDI, les projets FL Studio, les sets Ableton Live et les projets zippés ont leurs propres entrées dans le même menu.

## Exporter le morceau en WAV

1. Choisissez `Fichier › Exporter › Fichier Wave...`.
2. Laissez `Étendue` sur `Morceau (Playlist)` et `Fichiers` sur `Un fichier (le mixage)`.
3. Choisissez une `Résolution` : 16 bits, 24 bits ou 32 bits (flottant).
4. Réglez `File d'attente` sur `Laisser le reste` pour que les réverbs et les release s'éteignent après la dernière mesure.
5. Cliquez sur `Exporter...`, choisissez un nom et un dossier, puis attendez le rendu.
6. Une fois terminé, LYNX SHARE indique où est allé le fichier et son niveau de crête.

`Ctrl+R` ouvre la fenêtre avec le dernier format exporté. `Ctrl+Shift+R` l'ouvre sur MP3.

## Que faut-il rendre

| Réglage | Ce qu'il fait |
|---|---|
| `Étendue` | `Morceau (Playlist)`, `Pattern actuel` ou `Sélection de la Playlist` |
| `Lecture` | Combien de fois l'étendue est jouée dans le fichier, de `Une fois` à 16 fois |
| `File d'attente` | `Couper le reste` s'arrête exactement à la fin. `Laisser le reste` laisse le son s'éteindre. `Replier le reste` mixe la queue au début, pour les boucles |
| `Queue maximale` | 3, 6, 12 ou 30 secondes. La queue s'arrête plus tôt quand le son s'est éteint |
| `Fichiers` | Un fichier, ou des stems (voir plus bas) |

## Format de sortie

Les quatre boutons choisissent le format. `Enregistrer aussi` écrit d'autres formats à partir du même rendu, avec le même nom.

| Format | Options |
|---|---|
| WAV | `Résolution` : 16 bits, 24 bits ou 32 bits (flottant). `Dither` : `Désactivé`, `TPDF` ou `TPDF + noise shaping` |
| MP3 | `Débit`, constant |
| OGG | Débit |
| FLAC | `Résolution`, `Compression` de `0 (la plus rapide)` à `8 (le plus petit)`, `Dither`. Sans perte à tous les niveaux |

`Fréq. d'échantillonnage` et `Canaux` valent pour tous les formats. `Canaux` vaut `Stéréo`, `Mono (deux côtés mixés)`, `Mono (gauche seul)` ou `Mono (droite seul)`.

## Options de mixage

| Réglage | Ce qu'il fait |
|---|---|
| `Normaliser (crête à 0 dB)` | Monte ou baisse tout le fichier pour que sa crête la plus forte atteigne la pleine échelle |
| `Effets du Master` | Désactivé : les effets de l'insert Master sont contournés |
| `Effets des pistes` | Désactivé : les effets des autres inserts sont contournés |
| `HQ pour tous les plugins` | Les plugins utilisent leur qualité de rendu. Désactivé : l'export utilise la qualité de lecture |
| `Préchauffage des plugins` | Les plugins tiers traitent un instant de silence avant le début de l'export |
| `Désactiver la polyphonie max.` | Lève la limite de voix des canaux, pour qu'aucune note ne soit coupée |
| `Retirer le silence de compensation` | Retire le silence que la compensation de latence des plugins ajoute au début |
| `Informations de tempo`, `Marqueurs de boucle`, `Marqueurs de la playlist` | WAV uniquement : stockent dans le fichier le tempo, la boucle et les marqueurs de la Playlist |
| `Afficher le(s) fichier(s) à la fin` | Ouvre le gestionnaire de fichiers sur le résultat |

## Stems et pistes

![La fenêtre d'export réglée sur FLAC avec un fichier par insert du Mixer, neuf fichiers au total](/assets/img/wiki/export-flac-stems.webp)

`Fichiers` a trois modes de stems :

- `Un par insert (stems)` : un fichier pour chaque insert qui reçoit du son.
- `Un par canal (stems)` : un fichier pour chaque canal qui joue.
- `Un par canal, instruments seuls (sans envois ni bus)`.

Cochez `Et le mixage complet` pour obtenir le mixage à côté des stems. La ligne en bas indique combien de fichiers seront écrits.

`Fichier › Exporter › Toutes les pistes de la Playlist` et `Tous les groupes de pistes de la Playlist` écrivent un fichier par piste ou par groupe de la Playlist. Dans le menu du Mixer, `Enregistrement sur disque › Rendre les pistes armées en fichier(s) WAV...` (`Alt+R`) ouvre la fenêtre d'export pour les seuls inserts armés.

## Autres entrées du menu Export

| Commande | Touche | Ce qu'elle fait |
|---|---|---|
| `Fichier MIDI...` | `Ctrl+Shift+M` | Écrit les notes dans un fichier MIDI standard (`.mid`) |
| `Masteriser...` | `Ctrl+Alt+M` | Rend le projet et le masterise vers une cible de loudness en LUFS, sur votre ordinateur |
| `Projet avec samples (.zip)...` | aucune | Le projet et ses samples dans une seule archive |
| `Projet FL Studio...` | aucune | Un `.flp`, ou un paquet de boucles zippé avec tous les samples |
| `Set Ableton Live (.als)...` | aucune | Un set pour Live 12 ou Live 11 |
| `Os du projet...` | aucune | Presets et partitions des canaux et des effets, dans un dossier |

## Exporter vers FL Studio et Ableton Live

Le `.flp` est un projet FL Studio 2026. Les samplers, clips audio, générateurs et effets propres à FL Studio, plugins tiers, patterns, la Playlist, le Mixer et l'automation passent avec leurs réglages. Pour ce que FL Studio n'a pas, vous choisissez entre `Les rendre en audio (même son)` et `Utiliser le plugin FL le plus proche (reste modifiable, peut sonner différemment)`. Avec le second choix, les instruments propres à LYNX sont laissés de côté. Les modulateurs sont toujours laissés de côté. Un rapport liste ce qui a été rendu, remplacé ou laissé de côté. Les détails sont dans [Venir de FL Studio](/wiki/coming-from-fl-studio/).

Le `.als` contient une piste MIDI par canal, des pistes audio, des groupes, des retours, des envois, des racks et des Drum Racks, des plugins VST2 et VST3 avec leur état, des marqueurs, et l'automation du volume, du pan et du tempo. Les instruments et effets propres à LYNX SHARE ne sont pas convertis, mais leurs pistes et leurs notes sont conservées. L'automation des paramètres d'effets et les routages d'insert à insert sont abandonnés. Les détails sont dans [Venir d'Ableton Live](/wiki/coming-from-ableton/).

## Bon à savoir

- `Dither` est grisé pour le MP3, et les options de tempo et de marqueurs sont grisées pour tous les formats sauf WAV.
- Avec `Sélection de la Playlist` et aucune sélection dans la Playlist, tout le morceau est rendu. La fenêtre vous prévient.
- Si l'insert Master est muet, la fenêtre prévient que le fichier sera silencieux.
- `Écrire aussi une vidéo avec ce mixage` n'apparaît que si le projet a une piste vidéo, et il faut FFmpeg.
- `Rendre via le matériel en temps réel` n'apparaît que si le projet a un insert matériel actif. Cet export dure aussi longtemps que le morceau.
- Render to audio, qui convertit un insert en clip audio, joue environ 5 dB plus fort que l'insert qu'il remplace dans cette version.

## Voir aussi

- [Formats de fichiers](/wiki/file-formats/)
- [Ligne de commande](/wiki/command-line/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
