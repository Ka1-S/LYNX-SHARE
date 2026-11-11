---
source: 7ca5e689db
---
![Het Snel-toevoegpalet: instrumenten links, effecten rechts, elk gelabeld als ingebouwd of ingebouwde FL-engine](/assets/img/wiki/quickadd.webp)

LYNX SHARE levert eigen instrumenten en effecten mee, zodat een project klinkt zonder plugins van derden. Er zijn drie soorten: originelen van LYNX SHARE (een synth, een sampler, een SoundFont-speler, drums, EQ's, compressors, limiters, reverbs, delays), engines die zijn geschreven om de instellingen van de native plugins van FL Studio af te spelen, en effecten die in de plaats komen van bekende plugins van derden.

Elk ervan heeft een eigen pagina in de [catalogus](/instruments-and-effects/). `Help › Ingebouwde plugins (online lijst)...` opent de lijst met de status van elke engine.

## Een instrument en een effect toevoegen

1. Druk op `F6` om het Channel Rack te tonen.
2. Klik op het vak `+ kanaal toevoegen` onder de laatste rij en kies een instrument. Het menu groepeert ze per categorie.
3. Druk op `F9` om de Mixer te tonen en selecteer de insert waar het kanaal doorheen speelt.
4. Klik op een leeg slot in het effectenpaneel en kies een effect. Het venster ervan opent.
5. Klik op de naam van het slot om dat venster weer te verbergen of te tonen.

## De Plugin-kiezer

Druk op `F8` voor de Plugin-kiezer. Hij beslaat het scherm en toont elke generator en elk effect als een afbeelding van de eigen editor, met de categorieën als tabs onderaan.

| Commando | Toets | Wat het doet |
|---|---|---|
| Zoeken | Typen | Dimt de plugins die niet overeenkomen |
| Door de plugins lopen | `Up` / `Down` | Verplaatst de markering |
| Toevoegen | `Enter`, dubbelklikken of rechtsklikken | Een generator wordt een nieuw kanaal. Een effect komt in de geselecteerde Mixer-insert |
| Een afbeelding slepen | | Naar het Channel Rack voor een nieuw kanaal, naar een kanaal om het instrument te vervangen, of naar een Mixer-insert of -slot |
| Sluiten | `Esc` of `F8` | Sluit de kiezer zonder iets toe te voegen |

De afbeeldingen van de eigen plugins van LYNX SHARE worden op de achtergrond door LYNX SHARE getekend. Een plugin van derden toont een effen tegel met de naam van de maker totdat de editor één keer is geopend.

`Bewerken › Snel-toevoegpalet...` opent in plaats daarvan een eenvoudige lijst: instrumenten links, effecten rechts, gefilterd terwijl je typt. Met `Tab` wissel je van lijst en `Enter` voegt het gemarkeerde item toe. De Plugindatabase van de [Browser](/wiki/browser/) (`Shift+F8`) toont dezelfde plugins als boom.

![De Plugindatabase van de Browser met de map FL-engines open](/assets/img/wiki/browser-plugins.webp)

## Presets

Het venster van een ingebouwd effect heeft een presetbalk in de kop.

| Regelaar | Wat het doet |
|---|---|
| Pijlen | Vorige en volgende preset |
| Presetnaam | Opent het menu: `LYNX-SHARE-presets`, `Je presets`, `FL Studio-presets` waar die bestaan, `Preset opslaan...` en `Presetmap tonen` |
| `A` / `B` | Twee instellingen om te vergelijken. De knop ernaast kopieert de instelling die je hoort naar de andere |

De presetnaam en de instellingen A en B worden met het project opgeslagen.

## Wat "opent de instellingen van FL" betekent

De native plugins van FL Studio worden nooit geladen. LYNX SHARE heeft voor elk ervan een eigen engine, en die engine leest de instellingen die de FL-plugin heeft opgeslagen. Dit werkt op twee plekken:

- Een `.flp`-project. Elke FL-generator en elk FL-effect opent op zijn LYNX SHARE-engine met de opgeslagen instellingen, en de regelaars blijven bewerkbaar en automatiseerbaar.
- Een `.fst`-preset. Dubbelklik in de Browser op een synthpreset voor een nieuw kanaal, of sleep een effectpreset naar een Mixer-insert. Rechtsklik op een gevuld effectslot voor `FL-presets`, met de presets die in je FL Studio-installatie zijn gevonden.

Hoe dicht elke engine bij het origineel komt, staat per engine vermeld, van Exact tot Placeholder. De niveaus worden uitgelegd in [Overstappen van FL Studio](/wiki/coming-from-fl-studio/).

## Engines die je FL Studio-installatie nodig hebben

LYNX SHARE bevat geen code, audio of golfdata van Image-Line. Sommige engines hebben de golven, samples, impulsen of tabellen nodig die bij de originele plugin horen, en lezen die tijdens het draaien uit je eigen FL Studio-installatie.

| Type | Engines |
|---|---|
| Instrumenten | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Effecten | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Als de bestanden niet worden gevonden, kan het toevoegen van zo'n engine mislukken met een melding dat de databestanden van een FL Studio-installatie niet zijn gevonden of niet konden worden gelezen.

## Vervangers voor plugins van derden

Als een project een effect van een derde gebruikt dat niet is geïnstalleerd, neemt een LYNX SHARE-effect met dezelfde rol zijn slot over: een reverb voor een reverb, een delay voor een delay.

- Het slot toont de naam van de originele plugin met een LYNX SHARE-badge.
- Waar de opgeslagen staat gelezen kan worden, worden de instellingen vertaald. Anders begint de vervanger met een standaardpreset.
- De originele plugin en zijn instellingen blijven in het project. Zodra de plugin is geïnstalleerd, opent het project er weer mee.
- Rechtsklik op een slot met een vervanger en kies `Alle LYNX-SHARE-vervangers van dit project bypassen` om het project zonder vervangers te horen.

De schakelaar is `Ontbrekende plugins vervangen door LYNX-SHARE-equivalenten` in `Opties › Algemene instellingen...`, standaard aan. In een nieuw project staan dezelfde effecten in het menu van het lege slot onder `LYNX-SHARE-equivalenten`.

## Goed om te weten

- Ontbrekende instrumenten worden nog niet vervangen. Een ontbrekende instrumentplugin wordt een ingebouwde synth, gekozen op de naam van het kanaal.
- Automatisering van de parameters van de originele plugin blijft bewaard, maar stuurt de vervanger niet aan.
- FLEX-packs zijn versleuteld en worden nooit geopend. Een FLEX-kanaal krijgt een synthpatch die op de naam van de preset wordt gekozen.
- Een insert heeft 10 effectslots.

## Gerelateerd

- [Catalogus van instrumenten en effecten](/instruments-and-effects/)
- [Plugins van derden](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
