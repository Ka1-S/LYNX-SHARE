---
source: 7ca5e689db
---
![La palette di aggiunta rapida: strumenti a sinistra, effetti a destra, ciascuno contrassegnato come integrato o motore FL integrato](/assets/img/wiki/quickadd.webp)

LYNX SHARE include i suoi strumenti ed effetti, così un progetto suona senza plugin di terze parti. Ce ne sono di tre tipi: gli originali di LYNX SHARE (un synth, un sampler, un player SoundFont, batterie, EQ, compressori, limiter, reverb, delay), motori scritti per riprodurre le impostazioni dei plugin nativi di FL Studio ed effetti che sostituiscono plugin di terze parti noti.

Ognuno ha una pagina propria nel [catalogo](/instruments-and-effects/). `Aiuto › Plugin integrati (elenco online)...` apre l'elenco con lo stato di ogni motore.

## Aggiungere uno strumento e un effetto

1. Premi `F6` per mostrare il Channel Rack.
2. Fai clic sulla casella `+ aggiungi canale` sotto l'ultima riga e scegli uno strumento. Il menu li raggruppa per categoria.
3. Premi `F9` per mostrare il Mixer e seleziona l'insert attraverso cui suona il canale.
4. Fai clic su uno slot vuoto nel pannello degli effetti e scegli un effetto. Si apre la sua finestra.
5. Fai clic sul nome dello slot per nascondere o mostrare di nuovo quella finestra.

## Il selettore plugin

Premi `F8` per il selettore plugin. Copre lo schermo e mostra ogni generatore ed effetto come immagine del suo editor, con le categorie come schede lungo il bordo inferiore.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Cerca | Digita | Attenua i plugin che non corrispondono |
| Scorri i plugin | `Up` / `Down` | Sposta l'evidenziazione |
| Aggiungi | `Enter`, doppio clic o clic destro | Un generatore diventa un nuovo canale. Un effetto va nell'insert del Mixer selezionato |
| Trascina un'immagine | | Nel Channel Rack per un nuovo canale, su un canale per sostituirne lo strumento, oppure su un insert o uno slot del Mixer |
| Chiudi | `Esc` o `F8` | Chiude il selettore senza aggiungere nulla |

Le immagini dei plugin di LYNX SHARE sono disegnate da LYNX SHARE in background. Un plugin di terze parti mostra un semplice riquadro con il nome del produttore finché il suo editor non è stato aperto almeno una volta.

`Modifica › Palette di aggiunta rapida...` apre invece un semplice elenco: strumenti a sinistra, effetti a destra, filtrati mentre scrivi. `Tab` cambia elenco e `Enter` aggiunge la voce evidenziata. Il database dei plugin del [Browser](/wiki/browser/) (`Shift+F8`) elenca gli stessi plugin ad albero.

![Il database dei plugin del Browser con la cartella dei motori FL aperta](/assets/img/wiki/browser-plugins.webp)

## Preset

La finestra di un effetto integrato ha una barra dei preset nell'intestazione.

| Controllo | Cosa fa |
|---|---|
| Frecce | Preset precedente e successivo |
| Nome del preset | Apre il menu: `Preset di LYNX SHARE`, `I tuoi preset`, `Preset di FL Studio` dove esistono, `Salva preset...` e `Mostra la cartella dei preset` |
| `A` / `B` | Due impostazioni da confrontare. Il pulsante accanto copia quella che senti sull'altra |

Il nome del preset e le impostazioni A e B vengono salvati con il progetto.

## Cosa significa "apre le impostazioni di FL"

I plugin nativi di FL Studio non vengono mai caricati. LYNX SHARE ha un motore proprio per ciascuno, e quel motore legge le impostazioni salvate dal plugin FL. Funziona in due casi:

- Un progetto `.flp`. Ogni generatore ed effetto di FL si apre sul suo motore LYNX SHARE con le impostazioni salvate, e i suoi controlli restano modificabili e automatizzabili.
- Un preset `.fst`. Fai doppio clic su un preset di synth nel Browser per ottenere un nuovo canale, oppure trascina un preset di effetto su un insert del Mixer. Fai clic destro su uno slot di effetto occupato per `Preset di FL`, che elenca i preset trovati nella tua installazione di FL Studio.

Quanto ogni motore si avvicina all'originale è indicato per singolo motore, da Exact a Placeholder. I livelli sono spiegati in [Se vieni da FL Studio](/wiki/coming-from-fl-studio/).

## Motori che richiedono la tua installazione di FL Studio

LYNX SHARE non contiene codice, audio o dati d'onda di Image-Line. Alcuni motori hanno bisogno delle onde, dei sample, degli impulsi o delle tabelle del plugin originale, e li leggono dalla tua installazione di FL Studio mentre girano.

| Tipo | Motori |
|---|---|
| Strumenti | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Effetti | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Se i file non vengono trovati, l'aggiunta di uno di questi motori può fallire con un messaggio che dice che i file di dati di un'installazione di FL Studio non sono stati trovati o non si sono potuti leggere.

## Sostituti dei plugin di terze parti

Quando un progetto usa un effetto di terze parti che non è installato, un effetto LYNX SHARE con lo stesso ruolo ne prende lo slot: un reverb per un reverb, un delay per un delay.

- Lo slot mostra il nome del plugin originale con un badge LYNX SHARE.
- Dove lo stato salvato si può leggere, le sue impostazioni vengono tradotte. Altrimenti il sostituto parte da un preset predefinito.
- Il plugin originale e le sue impostazioni restano nel progetto. Quando il plugin viene installato, il progetto si apre di nuovo con quello.
- Fai clic destro su uno slot sostituito e scegli `Bypassa tutti i sostituti LYNX SHARE di questo progetto` per sentire il progetto senza di loro.

L'interruttore è `Sostituisci i plugin mancanti con equivalenti LYNX SHARE` in `Opzioni › Impostazioni generali...`, attivo di default. In un nuovo progetto, gli stessi effetti sono nel menu dello slot vuoto sotto `Equivalenti LYNX SHARE`.

## Da sapere

- Gli strumenti mancanti non vengono ancora sostituiti. Un plugin strumento mancante diventa un synth integrato scelto in base al nome del canale.
- L'automazione dei parametri del plugin originale viene conservata ma non pilota il sostituto.
- I pacchetti FLEX sono cifrati e non vengono mai aperti. Un canale FLEX riceve una patch di synth scelta in base al nome del suo preset.
- Un insert ha 10 slot di effetto.

## Correlati

- [Catalogo di strumenti ed effetti](/instruments-and-effects/)
- [Plugin di terze parti](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
