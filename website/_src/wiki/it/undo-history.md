---
source: 9f36c60cc7
---
![La finestra Cronologia annullamenti: sei passi da Start a Add notes, con Mute: Kick come passo corrente](/assets/img/wiki/history.webp)

Ogni modifica al progetto è un passo nella cronologia degli annullamenti: una nota che aggiungi, una clip che sposti, un fader, un cambio di instradamento, il tempo. `Modifica › Cronologia annullamenti...` (`Ctrl+Alt+H`) mostra i passi come un elenco. A parte, LYNX SHARE salva copie di backup del progetto mentre lavori.

## Tornare a un passo precedente

1. Scegli `Modifica › Cronologia annullamenti...`, oppure premi `Ctrl+Alt+H`.
2. L'elenco mostra i passi con il più recente in basso. La prima riga, `Inizio`, è il progetto com'era quando è stato aperto.
3. Clicca un passo. Il progetto torna allo stato subito dopo di esso, e i passi successivi diventano grigi.
4. Clicca un passo grigio per andare di nuovo avanti.
5. Fai una nuova modifica per continuare dal passo che hai scelto. I passi grigi spariscono.

## I tasti di annullamento

`Ctrl+Z` non funziona come nella maggior parte dei programmi. Annulla l'ultimo passo e, premuto di nuovo, lo ripete, quindi alterna tra gli ultimi due stati. Per andare più indietro, usa `Ctrl+Alt+Z`.

| Comando | Tasto | Cosa fa |
|---|---|---|
| `Annulla` | `Ctrl+Z` | Annulla l'ultimo passo. Dopo un annullamento ripete il passo successivo, e la voce di menu diventa `Annulla (ripeti)` |
| `Annulla all'indietro` | `Ctrl+Alt+Z` | Torna sempre indietro, un passo per pressione |
| `Ripeti` | `Ctrl+Y` o `Ctrl+Shift+Z` | Va avanti |
| `Cronologia annullamenti...` | `Ctrl+Alt+H` | Apre l'elenco |

Dopo ogni annullamento o ripetizione, la barra dei suggerimenti nomina il passo e mostra il livello, per esempio `Level 2/3`.

Se preferisci il comportamento abituale, attiva `Modalità annulla alternativa (annulla classico, invece di annulla/ripeti sulla stessa scorciatoia)` in `Opzioni › Impostazioni generali...`. `Ctrl+Z` allora torna sempre indietro.

## Cosa è un passo e cosa no

- Le modifiche al progetto sono passi: note, pattern, clip, canali, livelli e instradamento del mixer, effetti, automazione, tempo, mute.
- Una registrazione è un passo. Lo è anche l'unione di una parte in una [sessione online](/wiki/online-sessions/), e `Crea un comp da queste take`.
- Le cose che non cambiano il progetto non sono passi: zoom, scorrimento, spostamento delle finestre e le impostazioni del programma stesso.
- Salvare ed esportare non si annullano. I file che hanno scritto restano.
- Le finestre dei plugin di LYNX SHARE hanno un annulla e ripeti separato per le modifiche di quel plugin, accanto alle impostazioni A e B.

## Impostazioni

Entrambe sono in `Opzioni › Impostazioni generali...`.

| Impostazione | Cosa fa |
|---|---|
| `Livelli massimi di annullamento` | Quanti passi si conservano: 25, 50, 100, 200, 500 o 1000. Il valore predefinito è 100 |
| `Modalità annulla alternativa` | Fa di `Ctrl+Z` un annulla classico |

## Backup

LYNX SHARE salva una copia di backup del progetto aperto ogni 2 minuti per impostazione predefinita, e conserva le 10 più recenti per progetto. Un backup è un normale file `.lynx` nella cartella `autosave` dentro la cartella delle impostazioni, con il nome del progetto seguito da `(autosaved on ...)` e dalla data e dall'ora.

Ogni volta che salvi sopra un file di progetto esistente, la versione sostituita viene copiata nella stessa cartella con `(overwritten on ...)` nel nome.

| Comando | Cosa fa |
|---|---|
| `File › Ripristina l'ultimo backup...` | Sostituisce il progetto aperto con il suo backup più recente, dopo aver chiesto. Il file di progetto cambia solo quando salvi |
| `File › Apri cartella dei salvataggi automatici` | Mostra i backup nel file manager |
| `File › Salva nuova versione` (`Ctrl+Alt+S`) | Salva come `Song_2`, `Song_3` e così via, accanto al file corrente |

La sezione `Backup` di `Opzioni › Impostazioni generali...` ha queste impostazioni:

| Impostazione | Cosa fa |
|---|---|
| `Salva una copia di backup` | `Mai`, oppure un intervallo in minuti |
| `Backup conservati per progetto` | 1, 3, 5, 10, 20 o 50 |
| `Attendi per i backup mentre il brano suona o si suona una tastiera MIDI` | Trattiene un backup per non interromperti |
| `Attesa massima tra i backup` | Da 5 a 60 minuti. Un backup viene fatto dopo questo tempo anche mentre suoni |

Se l'ultima sessione è finita in modo inatteso, al successivo avvio viene offerto di aprire il backup più recente salvato da quella sessione.

## Da sapere

- La cronologia non viene salvata con il progetto. Aprire un progetto o iniziarne uno nuovo la svuota.
- Quando la cronologia è piena, i passi più vecchi vengono scartati.
- In una sessione online live, annulla e ripeti ripristinano solo le tue modifiche, mai quelle degli altri.
- Con `Salva una copia di backup` su `Mai`, le copie fatte quando salvi sopra un file vengono comunque conservate.
- La cartella delle impostazioni è `%APPDATA%\LYNX` su Windows, `~/Library/Application Support/LYNX` su macOS e `~/.config/LYNX` su Linux.

## Correlati

- [Scorciatoie](/wiki/shortcuts/)
- [Impostazioni](/wiki/settings/)
- [Formati di file](/wiki/file-formats/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
- [Cosa fa solo LYNX SHARE: sicurezza in caso di crash](/why/#crash)
