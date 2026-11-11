---
source: 7db9a73959
---
Ci sono due modi per lavorare a un progetto con altre persone, e tutti e due sono sotto `File › Sessione online`. Un pacchetto è un file che invii a un amico, il quale ti rimanda una parte. Non richiede rete, account né server. Una sessione live collega tutti via internet attraverso il relay di LYNX SHARE, e ogni modifica arriva a tutti. In un progetto possono partecipare fino a 4 persone, te compreso.

## Inviare un progetto a un amico

![Invia progetto](/assets/img/wiki/session-send.webp)

1. Apri il progetto e scegli `File › Sessione online › Invia progetto...`.
2. Scrivi il tuo nome e quello del tuo amico.
3. Scegli cosa conserva il tuo amico quando la sessione finisce: `Solo la propria parte (consigliato)` o `Una copia completa del progetto`.
4. Salva il file `.lynxpack`.
5. Invia il file come preferisci: e-mail, una cartella condivisa, una chiavetta USB.

Il pacchetto contiene il progetto, ogni sample e ogni registrazione che usa, e i render audio ("sostituti") delle parti che usano un plugin di terze parti. Il tuo amico sente quelle parti senza possedere i tuoi plugin.

## Registrare una parte e rimandarla indietro

L'amico fa così:

1. Apre il `.lynxpack` con `File › Apri...`. Un riepilogo mostra chi lo ha inviato e quali insert del Mixer e tracce della Playlist sono tuoi. Clicca `Apri`.
2. Aggiunge i propri canali, pattern e audio sui propri insert e sulle proprie tracce della Playlist. Il materiale dell'host resta com'è.
3. Sceglie `File › Sessione online › Rimanda la mia parte...` e invia il nuovo pacchetto all'host.

Ogni ospite riceve un colore, 20 insert del Mixer e 16 tracce della Playlist.

## Unisci una parte

L'host fa così:

1. Apre il progetto che è stato inviato.
2. Sceglie `File › Sessione online › Unisci una parte...` e seleziona il pacchetto che l'amico ha rimandato.
3. I canali, i pattern, le note, le clip e gli insert dell'amico vengono aggiunti nel suo colore. Niente di tuo viene cambiato.

L'unione è un solo passo di annullamento. Una parte più recente dello stesso amico sostituisce la precedente.

## Avviare una sessione live o parteciparvi

![Avvia sessione live](/assets/img/wiki/session-start.webp)

L'host avvia la sessione:

1. Apri il progetto e scegli `File › Sessione online › Avvia sessione live...`.
2. Scrivi il tuo nome.
3. Sotto `Gli ospiti possono modificare`, scegli `Solo i propri canali, pattern, insert e righe (consigliato)` oppure `Tutto`. Sotto `Al termine della sessione, gli ospiti conservano`, scegli la propria parte o una copia completa.
4. Clicca `Inizio`, poi copia il codice di invito e invialo a un massimo di 3 persone.
5. Quando qualcuno chiede di entrare, clicca `Fai entrare`.

Un ospite partecipa:

1. Scegli `File › Sessione online › Partecipa a una sessione live...`.
2. Scrivi il tuo nome, incolla il codice di invito e clicca `Partecipa`.
3. Aspetta che l'host ti faccia entrare. Il progetto dell'host si apre.

## Durante una sessione live

![Chat della sessione](/assets/img/wiki/session-chat.webp)

Il menu `File › Sessione online` ha questi comandi mentre una sessione è in corso:

| Comando | Cosa fa |
|---|---|
| `Chat della sessione...` | Apre la chat. La voce di menu mostra il numero di messaggi non letti |
| `Segui il trasporto dell'host` | Per gli ospiti: riproduzione, stop e posizione seguono l'host |
| `Persone e permessi...` | Per l'host: cosa può cambiare ogni persona e cosa conserva alla fine |
| `Condividi i plugin con gli ospiti...` | Per l'host: quali dei tuoi plugin possono usare gli ospiti. La voce di menu mostra quanti sono condivisi |
| `Mostra codice di invito...` | Per l'host: mostra di nuovo il codice, con un pulsante `Copia` |
| `Chiudi sessione live` | Per l'host. Tutti escono e il codice smette di funzionare |
| `Lascia sessione live` | Per un ospite. La tua parte del progetto resta con te |

In `Persone e permessi...`, ogni persona ha uno di quattro ruoli: `I propri canali, pattern, insert e righe`, `Le proprie parti + tempo e impostazioni del progetto`, `Tutto` o `Solo ascolto (nessuna modifica)`. Disattivare un permesso non elimina mai nulla.

## Plugin che gli altri non hanno

Un canale o un insert il cui plugin non è installato sul tuo computer suona un sostituto e mostra un badge ambra `R` nel Channel Rack e nel Mixer.

- In un pacchetto, i sostituti sono render audio fatti quando il pacchetto viene salvato. Seguono le clip che sposti, ma non le modifiche alle note di un plugin che non hai. Chiedi un nuovo pacchetto a chi lo possiede.
- In una sessione live, le parti che usano un plugin che qualcuno non ha vengono renderizzate da chi ce l'ha e inviate in streaming agli altri come audio.

## Condividere i tuoi plugin con gli ospiti

In una sessione live l'host può lasciare che gli ospiti usino plugin che ha solo l'host.

L'host fa così:

1. Scegli `File › Sessione online › Condividi i plugin con gli ospiti...`.
2. Spunta i plugin da condividere. `Cerca nei tuoi plugin` restringe la lista. Non viene condiviso nulla finché non lo spunti.

Un ospite fa così:

1. Apri il menu dei plugin di uno dei tuoi canali o slot degli effetti. I plugin condivisi vengono per primi, sotto `On <host>'s computer`.
2. Scegline uno. La sua finestra elenca i parametri del plugin come slider, con `Cerca parametri`.
3. Aggiungi note e muovi gli slider. La finestra mostra `Updating...` mentre il computer dell'host esegue il render, poi `Aggiornato`.

- Il plugin resta sul computer dell'host e gira con la licenza dell'host. LYNX SHARE non invia mai il plugin, i suoi file, i suoi preset né la sua licenza.
- L'ospite sente un render che segue le note e i valori dopo un attimo. Non viene suonato dal vivo, e l'ospite non ha la finestra propria del plugin.
- Quando l'host non c'è, suona l'ultimo render. La condivisione finisce con la sessione, e l'ultimo render resta.
- Un ospite con il ruolo `Solo ascolto (nessuna modifica)` non può usare i plugin condivisi.
- Alcune licenze di plugin non permettono ad altre persone di usare il plugin, nemmeno a distanza. Controlla ogni licenza prima di condividere.

## Cosa passa in rete

I pacchetti sono file. Non viene caricato nulla da nessuna parte.

Una sessione live si collega solo finché ne fai parte. Tutto passa dal relay `relay.lynx-share.com`, con cifratura end-to-end tramite una chiave che deriva dal codice di invito. Il relay inoltra i dati senza poterli leggere. Vede il tuo indirizzo IP, quando eri connesso e quanti byte sono passati. Le altre persone nella sessione vedono il tuo nome, le tue modifiche, i sample che la tua parte usa e se stai riproducendo o registrando. Non vedono il tuo indirizzo IP. I dettagli sono nell'[informativa sulla privacy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Da sapere

- `Solo la propria parte` è una cortesia tra musicisti, non una protezione dalla copia. Il pacchetto è un normale zip.
- Entrambe le parti dovrebbero usare la stessa versione di LYNX SHARE. Il riepilogo del pacchetto avvisa quando differiscono.
- Un ospite la cui connessione cade si riconnette da solo e mantiene le sue modifiche.
- Se LYNX SHARE dell'host si chiude durante una sessione, al successivo avvio viene offerto di riprenderla e gli ospiti si riconnettono da soli.
- Se LYNX SHARE di un ospite si chiude durante una sessione, al successivo avvio ti chiede se rientrare. `Rientra` ti riporta dentro come la stessa persona, con la tua parte e tutto ciò che avevi cambiato. Funziona per 12 ore.
- La versione per Linux può ospitare una sessione live dalla 0.1.26.
- In una sessione live, annulla e ripeti ripristinano solo le tue modifiche.
- Chiunque abbia il codice di invito può chiedere di partecipare, ma solo dopo che l'host lo ha fatto entrare.

## Correlati

- [Cosa fa solo LYNX SHARE: sessioni online](/why/#online)
- [Formati di file](/wiki/file-formats/)
- [Plugin](/wiki/plugins/)
- [Annulla e cronologia](/wiki/undo-history/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
