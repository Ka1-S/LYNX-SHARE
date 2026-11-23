---
source: 532b2eee04
---
`Fichier › Ouvrir...` (`Ctrl+O`) ouvre les projets `.lynx` propres à LYNX SHARE, ainsi que les fichiers `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` et `.mid`. Un projet venu d'un autre programme est un import : il devient un nouveau projet LYNX SHARE non enregistré, et le fichier d'origine n'est jamais modifié.

## Ouvrir un projet d'un autre programme

1. Choisissez `Fichier › Ouvrir...` et sélectionnez le fichier `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` ou de module tracker (`.mod`, `.xm`, `.s3m`, `.it`).
2. Patientez pendant que LYNX SHARE cherche les plugins et les samples du projet.
3. Si quelque chose est introuvable, la fenêtre `Fichiers manquants` s'ouvre. Indiquez-lui où sont vos samples, ou fermez-la.
4. La barre de titre indique maintenant d'où vient le projet, par exemple « (importé depuis FL Studio) ».
5. Choisissez `Fichier › Enregistrer`. LYNX SHARE demande où écrire un fichier `.lynx`.

## Ce qui passe

| Format | Converti | Non converti |
|---|---|---|
| `.flp` (FL Studio) | Canaux, samples, patterns, notes slide et porta, tous les arrangements, marqueurs, changements de signature, automation, liens de contrôleurs, le Mixer avec son routage, sidechains, égaliseur de piste et effets natifs | Les packs FLEX sont chiffrés et jamais ouverts. Un canal FLEX reçoit un patch de synthé choisi d'après le nom de son preset |
| `.als` (Live 11 et 12) | Pistes MIDI, audio, de groupe et de retour, tempo, signature, locators, boucle, volume, pan, muet, solo et envois avec leur automation, clips Session et scènes, marqueurs de warp, racks et Drum Racks, et beaucoup d'appareils propres à Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable et d'autres) sous forme d'appareils LYNX SHARE | Les appareils de Live sans équivalent LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder et d'autres) sont abandonnés et listés |
| `.flm` (FL Studio Mobile) | Tempo, signature, un insert de mixeur par canal, une piste de Playlist par couloir, patterns, clips audio avec décalage et hauteur, kits de batterie, programmes DW Sampler et `.dwp`, le Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth et SuperSaw, les effets, l'automation et les jeux d'accords | Les autres générateurs FLM jouent le synthé intégré |
| `.dawproject` (Bitwig Studio, Studio One, Cubase et d'autres applications) | Pistes, dossiers, clips, notes, audio avec ses marqueurs de warp, automation, tempo, marqueurs, le mixer avec ses envois, scènes dans la grille de clips, et plugins VST3, VST2 et CLAP avec leur état. L'EQ, le compresseur, le gate et le limiteur du format deviennent ceux de LYNX SHARE | Les effets de notes, les expressions par note et les appareils propres à l'autre application sont listés dans le rapport d'import |
| `.band` (GarageBand 10, GarageBand pour iOS) | Pistes, régions MIDI et audio, tempo, signature, tonalité, sections en marqueurs, niveau, pan et muet, comme pistes de la disposition simple. Instruments Sampler créés dans GarageBand pour iOS | Les pistes d'instrument jouent le synthé et la batterie intégrés. Les régions Drummer, les dossiers de prises et les changements de tempo sont listés. Les projets de GarageBand 11 et antérieurs ne sont pas lisibles |
| `.blx` (BandLab) | Pistes, régions MIDI et audio avec leurs boucles, tempo, signature, tonalité, volume, pan et muet, automation du volume et du pan, et les effets courants, comme pistes de la disposition simple | Les pistes d'instrument jouent le synthé et la batterie intégrés. Les autres effets et le preset Mastering sont laissés de côté et listés. Testé uniquement sur des projets inventés |
| `.mid` | Notes, signatures et tous les changements de tempo | Un fichier MIDI ne contient aucun son : les canaux reçoivent le synthé LYNX SHARE ou un canal MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (modules tracker) | Les instruments en canaux Sampler avec leurs samples, les patterns, la liste d'ordre en Playlist, la vitesse et le tempo, le volume des notes, le panoramique, le retard et la coupure ; les changements de volume et de panoramique comme événements du canal (MOD et XM), et les slides, le vibrato, les arpèges et les enveloppes de hauteur comme courbes de hauteur des notes | Les autres effets et les enveloppes d'instrument sont comptés dans le rapport d'import. `Vue › Tracker` édite les notes à la façon d'un tracker |
| `.zip` | Le paquet de boucles zippé de FL Studio, ou un projet exporté avec ses samples : décompressé dans un dossier à part, puis le projet qu'il contient s'ouvre avec ses samples | |

Les plugins tiers VST2, VST3 et CLAP d'un `.flp` ou d'un `.dawproject`, et les plugins VST2 et VST3 d'un `.als`, se chargent avec leur état enregistré si le même plugin est installé sur votre ordinateur.

## Les plugins propres à FL Studio

Les générateurs et effets de FL Studio ne sont pas chargés. LYNX SHARE lit leurs réglages enregistrés et les joue sur des moteurs qu'il a recréés. Chaque moteur a un niveau de qualité, de `Exact` (identique bit à bit à l'original pour chaque état testé) à `Placeholder` (les réglages sont conservés, mais le plugin est muet ou laisse passer l'audio tel quel). Les niveaux sont expliqués dans [Venir de FL Studio](/wiki/coming-from-fl-studio/).

Ouvrir un `.flp` fonctionne sans FL Studio installé. Certains moteurs ont besoin d'ondes, de samples ou d'impulsions qui sont lus dans votre propre installation de FL Studio à l'exécution.

Un `.flp` qui utilise le « FL Studio Mobile Rack » de FL Studio (le « Save as FLP » propre à FL Studio Mobile en écrit un par piste d'instrument) joue son instrument et ses effets sur les mêmes moteurs qu'un `.flm`, et l'effet « FL Studio Mobile Rack FX » aussi. Un rack avec un kit de batterie ou plusieurs instruments joue toujours le synthé intégré.

## Sons d'usine de FL Studio Mobile

Les sons d'usine qu'utilise un `.flm` (kits de batterie, programmes DirectWave, SoundFonts, les ondes de GMSynth) appartiennent à FL Studio Mobile et ne sont jamais fournis avec LYNX SHARE. LYNX SHARE les cherche dans votre propre copie du dossier `Resources` de l'application : ajoutez-le dans `Réglages › Fichiers › Ressources de FL Studio Mobile` (choisir le dossier au-dessus, ou un dossier d'installation de FL Studio, fonctionne aussi). L'installation de FL Studio n'en contient qu'une partie, et elle est parcourue après vos dossiers. Les fichiers encore manquants sont listés dans la fenêtre Fichiers manquants.

## Samples manquants

![La fenêtre Missing files sur son onglet Samples : fichiers trouvés en vert, manquants en ambre, avec Search folder, Locate et Skip](/assets/img/wiki/missing-files.webp)

L'onglet `Échantillons` liste chaque sample introuvable, avec son chemin enregistré et ce qui l'utilise.

| Commande | Effet |
|---|---|
| `Rechercher dans un dossier...` | Cherche tous les samples manquants dans un dossier et ses sous-dossiers |
| `Localiser...` | Demande où se trouve un sample. Les autres trouvés dans le même dossier sont repris aussi |
| `Passer` | Laisse le sample de côté. Son canal reste muet |

`Appliquer` utilise les fichiers qui ont été trouvés.

L'onglet `Trouvés automatiquement` liste les fichiers que LYNX SHARE a trouvés tout seul dans un autre dossier. Enregistrez le projet pour garder les nouveaux emplacements.

## Plugins manquants

LYNX SHARE cherche chaque plugin tiers par son identifiant, puis par son fichier, puis par son nom. Un projet enregistré avec le VST2 s'ouvre avec le VST3 du même produit, et inversement, mais avec les réglages par défaut.

Quand un effet n'est pas installé, un effet LYNX SHARE de même rôle prend le slot : une réverb pour une réverb, un compresseur pour un compresseur.

- Le slot affiche le nom du plugin d'origine avec un badge LYNX SHARE.
- Quand l'état enregistré est lisible, les réglages sont traduits. Sinon, le remplaçant part d'un preset par défaut. Le tableau des produits, de leurs remplaçants et de ce qui est traduit pour chacun se trouve dans [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- L'automation des paramètres de l'original est conservée mais ne pilote pas le remplaçant.
- La référence et l'état du plugin d'origine restent dans le projet. Une fois le plugin installé, le projet s'ouvre de nouveau avec lui.
- `Contourner tous les substituts LYNX SHARE de ce projet`, dans le menu clic droit d'un slot remplaçant, les désactive tous d'un coup.

L'interrupteur est `Remplacer les plugins manquants par des équivalents LYNX SHARE` dans `Options › Réglages généraux...`, activé par défaut. S'il est désactivé, le slot d'un effet manquant est laissé de côté dans le mixeur. Un instrument manquant n'est pas remplacé. Il devient un synthé intégré avec un preset choisi d'après le nom du canal.

Pour récupérer le vrai plugin, installez-le, choisissez `Options › Rechercher des plugins`, puis rouvrez le projet.

## Fichiers MIDI

![La fenêtre Import MIDI data : pistes, canaux, type de canal et options](/assets/img/wiki/midi-import-dialog.webp)

`Fichier › Ouvrir...` crée un nouveau projet à partir d'un fichier MIDI. `Fichier › Importer › Fichier MIDI...` ouvre d'abord la fenêtre `Importer des données MIDI`, où vous choisissez ce qu'il faut importer.

| Réglage | Effet |
|---|---|
| `Quelles pistes importer ?` | Toutes les pistes, ou une seule |
| `Quels canaux importer ?` | Cliquez sur un canal MIDI pour l'exclure. Un clic droit ne garde que celui-là |
| `Type de canal` | `Synthé LYNX SHARE` ou `MIDI Out` |
| `Démarrer un nouveau projet` | Démarre un projet vierge. Désactivé : ajoute à celui qui est ouvert |
| `Créer un canal par piste` | Désactivé : un canal par canal MIDI |
| `Assigner des pistes du Mixer aux nouveaux canaux` | Route les nouveaux canaux vers des inserts de mixeur consécutifs |
| `Importer les signatures rythmiques` | La signature du fichier devient celle du projet |

`Ctrl+M` dans le [Piano Roll](/wiki/piano-roll/) importe un fichier MIDI dans le canal actuel.

## Importer des pistes d'un autre projet

`Fichier › Importer › Pistes d'un autre projet...` apporte des pistes d'un autre projet dans celui qui est ouvert, sans le fermer. Il lit tous les formats que lit `Fichier › Ouvrir...`.

1. Choisissez le projet. Une fenêtre liste ses pistes de Playlist et ce que contient chacune.
2. Cochez les pistes voulues.
3. Choisissez si `Avec les inserts de la console et les effets` et `Avec l'automation` viennent aussi, et `Les placer au` le début du morceau ou le début de la sélection de temps.
4. Cliquez pour importer. Les nouvelles pistes, canaux, patterns et inserts arrivent en une seule étape, donc un seul annuler les retire tous.

## À savoir

- `Aide › Fichiers manquants...` rouvre la fenêtre plus tard. `Aide › Dernier rapport d'import de projet` liste ce qui a été approximé, laissé de côté ou non installé.
- Les clips Session d'un set Live vont dans la grille de clips de la disposition Clip-launch. `Clips de session des sets Live` dans `Options › Réglages généraux...` les place plutôt sur la Playlist.

## Voir aussi

- [Formats de fichiers](/wiki/file-formats/)
- [Venir de FL Studio](/wiki/coming-from-fl-studio/)
- [Venir d'Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exporter](/wiki/exporting/)
