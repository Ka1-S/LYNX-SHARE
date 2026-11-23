---
source: 5e5d2e8e05
---
![La disposition simple : des pistes sur une timeline, un beat de départ et la Drum Machine](/assets/img/learn/simple-beat-2.webp)

Choisissez la **disposition simple**. C'est une seule fenêtre avec vos pistes sur une timeline, des boucles, une boîte à rythmes et l'enregistrement rapide, comme le font BandLab, FL Studio Mobile et GarageBand. Les deux autres dispositions restent à un clic.

## Vos trois premières actions

1. **Choisissez la disposition.** Au premier démarrage de LYNX SHARE, une carte de bienvenue demande d'où vous venez. Cliquez sur `BandLab, FL Studio Mobile ou GarageBand` : la fenêtre passe à la disposition simple, avec un beat de départ sur une piste Drums. Plus tard, `Vue › Style de disposition › Disposition simple` y bascule à tout moment.
2. **Faites un beat.** Cliquez sur les cases de la Drum Machine sous la timeline. Voir [Faire un beat dans la disposition simple](/learn/simple-beat/).
3. **Ajoutez une boucle et votre voix.** `Boucles` ouvre le panneau des boucles : glissez une boucle sur la timeline. Ensuite, [enregistrez votre voix](/learn/record-voice/) sur une piste Voice.

## Ce qui est pareil

- Des pistes sur une timeline, une par voix ou instrument, chacune avec volume, panoramique, mute, solo et effets.
- Une boîte à rythmes avec une grille de pas, des boucles qui suivent le tempo et la tonalité du morceau, et l'enregistrement sur une piste de voix.
- Des effets à un seul bouton, et un son fini pour tout le morceau avant d'enregistrer.
- Faire de la musique entre amis : une [session en ligne](/learn/online-session/) réunit jusqu'à 4 personnes dans un même projet.

## Ce qui est différent

- LYNX SHARE est un programme de bureau pour Windows, macOS et Linux. Il ne tourne pas dans un navigateur ni sur un téléphone.
- Vos projets sont des fichiers `.lynx` sur votre ordinateur. Il n'y a ni compte ni cloud : enregistrez-les, sauvegardez-les et envoyez-les comme n'importe quel autre fichier.
- Les plugins tiers se chargent (VST3 et CLAP, Audio Units sur macOS, LV2 sur Linux, VST2 sur Windows et Linux), à côté des instruments et effets propres à LYNX SHARE.
- Quand vous voulez plus, la disposition Patterns (la façon de travailler de FL Studio, avec le Channel Rack et le Piano Roll) et le Mixer (`F9`) fonctionnent sur le même projet.

## Équivalences

| Dans ces applications | Dans la disposition simple de LYNX SHARE |
|---|---|
| La vue des pistes ou le Mix Editor | La timeline : une piste par voix ou instrument |
| Ajouter une piste | `+ Ajouter une piste` : `Voix / audio`, `Instrument`, `Batterie`, `Sampler`, `Guitare / basse`, `Importer`, `Boucles` |
| Une boîte à rythmes ou un séquenceur pas à pas | L'onglet `Boîte à rythmes` d'une piste Drums : une grille de pas, des patterns A à H, la longueur et le swing |
| Des pads de batterie | Les 16 pads d'une piste Sampler, joués avec les touches de l'ordinateur |
| Une bibliothèque de boucles | `Boucles` : glissez une boucle sur la timeline. Elle suit le tempo et la tonalité du projet |
| Piano roll | L'onglet `Éditeur` du dock, ou `Ouvrir dans le Piano Roll` pour tous les outils |
| Clavier à l'écran et accords | L'onglet `Clavier` du dock : piano, `Touches de gamme` ou le `Pad d'accords` |
| Effets de piste et presets | L'onglet `Effets` du dock : des effets à un seul bouton, des chaînes toutes prêtes sous `Preset`, et `Fusion` |
| Correction de hauteur pour la voix | LYNX Tune, un correcteur de hauteur avec tonalité et gamme (`+` dans le dock, puis `Plus d'effets...`) |
| Mastering | `Terminer` : un son fini pour tout le morceau, puis enregistrement en WAV ou MP3 |
| Paroles | `Paroles` |
| Accordeur | `Accordeur` dans la barre d'outils |
| Table de mixage | `F9` |
| Parties du morceau (intro, couplet, refrain) | `Sections` dans la barre de titre : des parties qui se déplacent, se copient et se répètent d'un bloc |
| Baisser le beat sous la voix, un envoi d'écho | `Atténuer sous` et le bouton `Écho`, à côté de `Réverb` dans l'onglet `Effets` du dock |

## Vos projets

- **FL Studio Mobile** (`.flm`) : `Fichier › Ouvrir...` l'ouvre. L'import est partiel : le tempo, les pistes, les patterns, les clips audio, les kits de batterie, le DW Sampler, le Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw et les effets de FLM sont repris. Les autres instruments de FLM jouent le synthé intégré de LYNX SHARE. `Fichier › Enregistrer une copie comme projet FL Studio Mobile (.flm)...` en écrit une copie pour FL Studio Mobile.
- **GarageBand** (`.band`, de GarageBand 10 ou de GarageBand pour iOS) : `Fichier › Ouvrir...` l'ouvre sous forme de pistes de la disposition simple, avec leurs régions, le tempo, la tonalité et le mixage. Les pistes d'instruments jouent le synthé et la batterie intégrés de LYNX SHARE, car GarageBand ne stocke pas leurs sons sous une forme que d'autres programmes peuvent lire.
- **BandLab** (`.blx`, un projet téléchargé par Cakewalk ou Sonar) : `Fichier › Ouvrir...` l'ouvre sous forme de pistes de la disposition simple, avec les régions, le tempo, la tonalité, le mixage, l'automation du volume et du panoramique et les effets courants. Les sons d'instruments et les autres effets de BandLab sont listés dans le rapport d'import.
- **Stems de n'importe quelle application** : exportez les pistes en fichiers audio ou MIDI, mettez-les dans un seul dossier, puis cliquez sur `+ Ajouter une piste`, et sur `Dossier...` à côté de `Importer`. Chaque fichier devient une piste qui commence au début.

`Aide › Dernier rapport d'import de projet` liste ce qui a été approximé ou laissé de côté. Voir [Formats de fichiers](/wiki/file-formats/).

## Bon à savoir

- `Fichier › Enregistrer` écrit un fichier `.lynx`. Le fichier que vous avez ouvert n'est jamais modifié.
- `Terminer` enregistre le morceau en WAV ou MP3, et `Fichier › Exporter` propose plus de formats et les stems. Voir [Exporter](/wiki/exporting/).
- `Aide › Tour de la première chanson` vous guide dans un premier morceau en cinq minutes environ.

BandLab est une marque de BandLab Technologies. FL Studio Mobile est une marque d'Image-Line Software. GarageBand est une marque d'Apple Inc. LYNX SHARE est un programme indépendant, non affilié à ces sociétés ni approuvé par elles.

## Voir aussi

- [Disposition Simple](/wiki/simple-layout/)
- [Dispositions](/wiki/layouts/)
- [Enregistrement](/wiki/recording/)
- [Apprendre : tutoriels](/learn/)
