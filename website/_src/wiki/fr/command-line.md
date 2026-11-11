---
source: 98829adcb8
---
LYNX SHARE peut aussi tourner sans ouvrir sa fenêtre. Une option rend un projet en audio, d'autres le convertissent en projet FL Studio ou en set Ableton Live, et plusieurs servent aux tests. Les vérifications du projet lui-même utilisent les mêmes options.

Le programme s'appelle `LYNX.exe` sous Windows et `LYNX` sous macOS et Linux. Sous macOS, le chemin complet est `/Applications/LYNX.app/Contents/MacOS/LYNX`. Les options sont les mêmes sur tous les systèmes.

## Rendre un projet dans un fichier

1. Ouvrez un terminal dans le dossier qui contient le programme. Sous Windows, c'est le dossier d'installation ou le dossier où vous avez extrait le zip.
2. Lancez `LYNX.exe --export project.flp out.flac`, avec votre propre projet et votre propre fichier de sortie.
3. Attendez que le programme se termine. Aucune fenêtre ne s'ouvre.
4. Lisez `out.flac.txt`, écrit à côté de la sortie. Pour un projet importé, il liste ce qui a été converti, approximé et laissé de côté, ainsi que les plugins trouvés ou manquants.

## Ouvrir un projet

```
LYNX.exe project.lynx
```

Démarre LYNX SHARE avec sa fenêtre et ouvre le fichier. Il accepte `.lynx`, `.flp`, `.als`, `.flm` et `.mid`. Un chemin relatif part du dossier actuel.

## Options d'usage courant

| Option | Effet |
|---|---|
| `--export <project> <out>` | Rend tout le morceau avec sa queue, comme la fenêtre Export. L'extension de `<out>` choisit le format : `.wav` (24 bits), `.flac`, `.ogg` ou `.mp3` (320 kbps) |
| `--export ... --pattern` | Rend uniquement le pattern actuel |
| `--export ... --insert N` | Rend uniquement la sortie de l'insert N du Mixer |
| `--export ... --rate N` | Rend à une autre fréquence d'échantillonnage, en Hz |
| `--stems <project> <base.wav>` | Écrit un WAV par insert du Mixer qui contient des canaux : `base - 1.wav`, `base - 2.wav` et ainsi de suite |
| `--export-flp <project> <out.flp>` | Écrit un projet FL Studio. Avec `out.zip`, il écrit un paquet de boucles zippé |
| `--export-flp ... --substitute` | Remplace les plugins propres à LYNX par le plugin FL le plus proche au lieu de les rendre en audio |
| `--export-als <project> <folder>` | Écrit un set Ableton Live dans `<folder>/<name> Project/<name>.als`, pour Live 12 |
| `--export-als ... --live11` | Écrit le set pour Live 11 |
| `--devices <report.txt>` | Liste les types de pilotes audio et les périphériques de cet ordinateur |
| `--showcase <out.lynx>` | Écrit le projet de démonstration de LYNX SHARE |
| `--lang <id>` | Utilise une autre langue d'interface pour cette exécution, par exemple `--lang pt-BR` |
| `--menu-entry on` ou `désactivé` | AppImage Linux uniquement : ajoute LYNX SHARE au menu des applications, ou l'en retire |

`--export`, `--stems` et `--export-flp` acceptent un `.lynx`, `.flp` ou `.als` comme projet. `--export-als` accepte aussi `.flm`.

Exemples :

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Options pour les tests

Elles servent à vérifier LYNX SHARE lui-même. Elles sont utiles dans un rapport de bug.

| Option | Effet |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Lance l'autotest sans périphérique audio. Le rapport se termine par `result=OK` s'il réussit |
| `--snapshot <prefix> [project]` | Dessine l'interface et ses fenêtres dans des fichiers PNG nommés d'après le préfixe. Les images de ce wiki sont faites ainsi |
| `--render <project> <out.wav> <seconds>` | Rend ce nombre de secondes du pattern actuel dans un WAV 24 bits |
| `--plugintest <plugin> <report.txt>` | Charge et teste un plugin VST3 ou CLAP |
| `--sandboxtest <plugin> <report.txt>` | Teste un plugin dans un processus séparé, y compris un plugin bloqué |
| `--scantest <folder> <report.txt>` | Analyse un dossier à la recherche de plugins, dans des processus séparés |
| `--fldb <report.txt> [project.flp]` | Rapporte l'installation de FL Studio trouvée ici, sa base de plugins et les correspondances de plugins d'un projet |
| `--devicetest <type> <device> <report.txt>` | Ouvre un pilote audio pendant une seconde de silence et en fait le rapport |
| `--uiperf <project> <report.txt>` | Mesure le temps de dessin du Mixer, de la Playlist et du Channel Rack |

Exemples :

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## À savoir

- Les résultats vont dans des fichiers, pas dans le terminal. `--export` écrit `<out>.txt`, `--export-flp` écrit `<out.flp>.txt`, `--export-als` écrit `export-als.txt` dans le dossier et `--stems` écrit `<base.wav>.txt`.
- Le programme lit l'entrée standard. Dans une boucle de shell, ajoutez `< /dev/null` à chaque appel.
- `--snapshot` dessine en 1280x720, sauf si la variable d'environnement `LYNX_SNAPSHOT_SIZE` dit autre chose, par exemple `1920x1080`.
- `--lang` accepte les identifiants des langues listées dans `Options › Réglages généraux...`, comme `en`, `pt-BR`, `de`, `es`, `fr` ou `ja`. Un identifiant inconnu est ignoré.
- `--export` utilise le même chemin de rendu que la fenêtre Export, mais aucune des autres options de la fenêtre. Pour le dither, la résolution en bits ou la normalisation, utilisez la fenêtre.
- Le code source a beaucoup d'autres options de test. Elles sont internes et peuvent changer d'une version à l'autre.

## Voir aussi

- [Exporter](/wiki/exporting/)
- [Formats de fichiers](/wiki/file-formats/)
- [Compiler depuis les sources](/wiki/build-from-source/)
- [Dépannage](/wiki/troubleshooting/)
