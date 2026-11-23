---
source: c82c79e958
---
LYNX SHARE a trois dispositions. Si vous venez de Logic, Cubase, Reaper, Pro Tools ou Studio One, commencez par la **disposition simple avec le Mixer à portée de main** : une piste par instrument ou par voix sur une timeline, comme vous la connaissez. La disposition Patterns, la façon de travailler de FL Studio, est là quand vous voulez des patterns.

## Vos premiers pas

1. **Choisissez la disposition.** Au premier démarrage de LYNX SHARE, une carte de bienvenue demande d'où vous venez. Cliquez sur `Logic, Cubase, Reaper, Pro Tools ou Studio One` : la disposition simple s'ouvre sur sa carte New track, et le Mixer apparaît avec votre première piste. Plus tard, `Vue › Style de disposition › Disposition simple` y bascule à tout moment, et `F9` ouvre le Mixer.
2. **Ajoutez une piste.** `+ Ajouter une piste` propose `Voix / audio`, `Instrument`, `Batterie`, `Sampler`, `Guitare / basse`, `Importer` et `Boucles`. Jouez une piste d'instrument avec les touches de l'ordinateur, le clavier à l'écran ou un clavier MIDI.
3. **Enregistrez.** Choisissez votre périphérique audio dans `Options › Réglages audio...`, puis voyez [Enregistrer votre voix](/learn/record-voice/).

## Ce qui est pareil

- Des pistes sur une timeline avec des clips audio, MIDI et d'automation.
- L'enregistrement audio et MIDI, avec prises en boucle et comping.
- Un Mixer avec inserts, envois, sidechain, groupes et compensation de latence.
- Des plugins tiers : VST3 et CLAP sur tous les systèmes, Audio Units sur macOS, LV2 sur Linux, VST2 sur Windows et Linux.
- L'export du morceau, des stems par piste, et du MIDI.

## Ce qui est différent

- **Les pistes et les inserts du mixer sont séparés.** Dans la disposition simple, chaque nouvelle piste reçoit son propre canal et sa propre piste de mixer. Dans la disposition Patterns, vous routez vous-même les canaux vers les inserts, comme dans FL Studio.
- **Les patterns.** Dans la disposition Patterns, les notes vivent dans des patterns que vous placez sur la Playlist, et un pattern peut contenir plusieurs instruments.
- **Les touches suivent FL Studio.** `Ctrl+Z` annule une étape, et rétablit si vous appuyez de nouveau. `Ctrl+Alt+Z` remonte plus loin.
- **Pas d'import de projet depuis Logic, Cubase, Reaper ou Pro Tools.** Déplacez les projets avec des fichiers DAWproject, des stems ou du MIDI (ci-dessous).

## Touches

| Pour | Appuyez sur |
|---|---|
| Lire, ou arrêter et revenir en arrière | `Space` |
| Activer ou couper l'enregistrement | `R` |
| Mixer | `F9` |
| Piano roll | `F7` |
| Playlist | `F5` |
| Browser | `Alt+F8` |
| Ouvrir, enregistrer | `Ctrl+O`, `Ctrl+S` |
| Annuler, et remonter plus loin | `Ctrl+Z`, `Ctrl+Alt+Z` |
| Exporter audio, MP3, MIDI | `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+Shift+M` |

`Aide › Raccourcis` liste toutes les touches. Voir [Raccourcis](/wiki/shortcuts/).

## Transférer des projets

- **Cubase, Studio One et Bitwig** : enregistrez-y un fichier DAWproject (`.dawproject`) et ouvrez-le avec `Fichier › Ouvrir...`. Les pistes, les dossiers, les clips, les notes, l'audio avec ses marqueurs de warp, l'automation, le tempo, les marqueurs, le mixer avec ses envois, et les plugins VST3, VST2 et CLAP avec leur état sont repris. `Fichier › Exporter › DAWproject (.dawproject)...` fait le chemin inverse.
- **Logic, Reaper et Pro Tools** : exportez chaque piste en fichier audio ou MIDI depuis le début du morceau dans un seul dossier. Dans LYNX SHARE, cliquez sur `+ Ajouter une piste`, puis sur `Dossier...` à côté de `Importer` : chaque fichier devient une piste qui commence au début.
- **Pour ressortir** : la fenêtre d'export écrit un fichier par insert du mixer ou par canal, et `Fichier › Exporter › Fichier MIDI...` écrit les notes.

Voir [Formats de fichiers](/wiki/file-formats/) et [Exporter](/wiki/exporting/).

## Bon à savoir

- Logic est une marque d'Apple Inc., Cubase de Steinberg Media Technologies, Reaper de Cockos Incorporated, Pro Tools d'Avid Technology, Studio One de PreSonus Audio Electronics et Bitwig de Bitwig GmbH. LYNX SHARE est un programme indépendant, non affilié à ces sociétés ni approuvé par elles.
- La disposition par lancement de clips ajoute une grille de clips avec des scènes à côté de la timeline. Voir [Dispositions](/wiki/layouts/).

## Voir aussi

- [Dispositions](/wiki/layouts/)
- [Mixer](/wiki/mixer/)
- [Enregistrement](/wiki/recording/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Apprendre : tutoriels](/learn/)
