---
source: 8df354b49e
---
![Il Browser a sinistra con le sue tre schede, la casella di ricerca, l'albero delle cartelle e la barra di anteprima](/assets/img/wiki/browser-tree.webp)

Il Browser elenca le tue cartelle di sample, il contenuto del progetto corrente e i plugin che LYNX SHARE conosce. Premi `Alt+F8` per mostrarlo o nasconderlo. `F8` da solo apre il selettore plugin, una vista separata a schermo intero con ogni strumento ed effetto.

## Caricare un sample nel progetto

1. Premi `Alt+F8` per mostrare il Browser.
2. Nella scheda `TUTTO`, fai clic su una cartella per aprirla.
3. Fai clic su un file audio per ascoltarlo. `Up` e `Down` scorrono i file e ne fanno sentire l'anteprima.
4. Fai doppio clic sul file per ottenere un nuovo canale con il sample nel [Channel Rack](/wiki/channel-rack/).
5. Oppure trascina il file nella [Playlist](/wiki/playlist/) per ottenere una clip audio.

## Schede

| Scheda | Cosa mostra |
|---|---|
| TUTTO | Il progetto corrente, il database dei plugin e ogni cartella di sample |
| PROGETTO | Solo ciò che usa il progetto corrente: pattern, effetti, generatori, automazione, file audio |
| PREFERITI | I file e le cartelle che hai contrassegnato (clic destro, poi `Preferito`) |

## Sfogliare e ascoltare l'anteprima

- Fai clic su una cartella per aprirla o chiuderla. Clic destro per un menu (comprimi tutto, mostra nel file manager).
- Fai clic su un file audio per ascoltarlo. La barra di anteprima sotto l'elenco ne mostra la forma d'onda. Fai clic sulla barra per riascoltare il file, oppure trascina da lì come se fosse il file.
- Il pannello sopra la barra di anteprima mostra data, dimensione e percorso del file selezionato.

## Usare un sample

| Azione | Cosa fa |
|---|---|
| Doppio clic | Nuovo canale con il sample |
| Trascina nel Channel Rack | Nuovo canale, oppure rilascia su una riga per caricarlo lì |
| Trascina nella Playlist | Clip audio |
| `Enter` | Carica il file nel canale selezionato |
| `Shift+Up` / `Shift+Down` | Passa al file precedente o successivo e lo carica nel canale selezionato |

L'ultimo ti permette di provare sample diversi sul posto mentre il pattern suona.

## Altri tipi di file

- **Progetto**: il doppio clic lo apre.
- **File MIDI**: il doppio clic lo apre come progetto.
- **SoundFont o SFZ**: il clic ne fa sentire l'anteprima, il doppio clic crea un canale LYNX Soundfont (o lo carica in quello selezionato).
- **Preset del Mixer**: il doppio clic lo carica negli insert del Mixer selezionati, oppure trascinalo su un insert.
- **Preset di effetto**: trascinalo su un insert del Mixer.

## Database dei plugin

La voce Database dei plugin elenca gli strumenti e gli effetti integrati di LYNX SHARE e i plugin installati. `Shift+F8` ci porta direttamente.

- Fai doppio clic su uno strumento per aggiungerlo come nuovo canale, oppure trascinalo nel Channel Rack.
- Fai doppio clic su un effetto per aggiungerlo all'insert del [Mixer](/wiki/mixer/) selezionato, oppure trascinalo su un insert o su uno slot di effetto.
- `F2` rinomina il plugin selezionato.
- Fai clic destro su un plugin e scegli `Sposta nella cartella` per ordinare i plugin in cartelle. Il menu degli slot del Mixer e il menu di aggiunta del Channel Rack elencano poi ogni cartella di primo livello.

## La scheda PROGETTO

I pattern elencati qui si possono selezionare con un clic per renderli il pattern corrente, aprire con un doppio clic nel [Piano Roll](/wiki/piano-roll/) o trascinare nella Playlist. La sezione del progetto corrente elenca anche la cronologia degli annullamenti: fai clic su un passaggio per tornare a quel punto.

## Ricerca

Premi `Ctrl+F` per mettere il cursore nella casella di ricerca. Cerca nella cartella selezionata (o in tutte le cartelle) e in quelle sotto. Nella scheda PROGETTO cerca tra gli elementi del progetto.

Tutte le parole devono corrispondere. La casella accetta anche `OR`, un segno meno per escludere una parola e i caratteri jolly `*`:

```
kick -loop
snare OR clap
hat*.wav
```

Le cartelle vengono indicizzate in background. `Aggiorna l'indice di ricerca` nel menu del Browser ricostruisce l'indice.

## Le tue cartelle

Usa `Aggiungi cartella` nell'elenco delle cartelle del Browser per aggiungere una cartella di sample, progetti o SoundFont. `Opzioni › Impostazioni dei file (cartelle di ricerca)` contiene le cartelle di ricerca.

## Tasti

I tasti freccia, `Enter` e `F2` richiedono che il Browser abbia il focus.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Mostra il Browser | `Alt+F8` | Mostra o nasconde il Browser |
| Selettore plugin | `F8` | Apre il selettore plugin a schermo intero |
| Database dei plugin | `Shift+F8` | Salta alla voce Database dei plugin |
| Cerca | `Ctrl+F` | Mette il cursore nella casella di ricerca |
| Scorri e ascolta | `Up` / `Down` | Seleziona il file precedente o successivo e lo riproduce |
| Chiudi cartella | `Left` | Chiude una cartella o va alla cartella superiore |
| Apri cartella | `Right` | Apre una cartella |
| Carica | `Enter` | Carica il file nel canale selezionato |
| Scorri e carica | `Shift+Up` / `Shift+Down` | Passa al file precedente o successivo e lo carica nel canale selezionato |
| Rinomina plugin | `F2` | Rinomina il plugin selezionato nel database dei plugin |

## Da sapere

- Rimuovere una cartella dall'elenco non la elimina dal disco.
- `F8` e `Alt+F8` sono cose diverse. `F8` è il selettore plugin, `Alt+F8` è il Browser.
- I risultati della ricerca vengono da un indice costruito in background. `Aggiorna l'indice di ricerca` nel menu del Browser lo ricostruisce.
- `Enter` carica il file nel canale selezionato. Il doppio clic crea un nuovo canale.

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Plugin di terze parti](/wiki/plugins/)
- [Formati di file](/wiki/file-formats/)
- [Annulla e cronologia](/wiki/undo-history/)
- [Strumenti ed effetti](/instruments-and-effects/)
