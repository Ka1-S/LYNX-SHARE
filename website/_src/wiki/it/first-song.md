---
source: de4ead4a7a
---
Questa pagina percorre una volta l'intero cammino: un pattern di batteria, una melodia, un arrangiamento, un mix e un file esportato. Ogni parte rimanda alla pagina che tratta la sua finestra per intero. Se a un certo punto non senti nulla, vedi [Configurazione audio](/wiki/audio-setup/).

## Fare un pattern di batteria

1. Scegli `File › Nuovo da modello › Vuoto`. Il progetto parte con cinque canali, Kick, Clap, Hat, Snare e Synth, e nessuno step.
2. Premi `F6` se il [Channel Rack](/wiki/channel-rack/) non è visibile.
3. Fai clic su alcuni step nella riga Kick. Trascinando ne disegni diversi, e il clic destro cancella.
4. Premi `Space`. Il trasporto è in modalità Pattern, quindi il pattern suona in loop. Premi di nuovo `Space` per fermare.
5. Dai alle righe Clap, Hat e Snare i loro step.
6. Premi `F2` per dare un nome e un colore al pattern.

Per aggiungere un suono che non c'è, fai clic sulla casella `+ aggiungi canale` sotto l'ultima riga e scegli uno strumento dal menu. Per usare un sample tuo, apri il [Browser](/wiki/browser/) con `Alt+F8`, fai clic su un file audio per ascoltarlo e fai doppio clic per aggiungerlo come nuovo canale.

## Scrivere una melodia

1. Premi `F4`. LYNX SHARE va al prossimo pattern vuoto e ne chiede il nome.
2. Fai clic destro sul nome del canale Synth e scegli `Piano Roll`.
3. Con la Matita (`P`), fai clic nella griglia per aggiungere una nota.
4. Trascina una nota per spostarla e trascina la sua fine per cambiarne la durata. Il clic destro elimina una nota.
5. Premi `Space` per sentire il pattern mentre modifichi.

`Ctrl+T` trasforma la tastiera del PC in un pianoforte, così puoi provare prima le note da `Z` a `M` e da `Q` a `P`. Spegnilo di nuovo prima di usare scorciatoie a lettera come `P`.

## Arrangiare i pattern

1. Premi `F5` per mostrare la [Playlist](/wiki/playlist/).
2. Nel Pannello di selezione a sinistra della Playlist, fai clic sul pattern di batteria. Ora è la sorgente delle clip.
3. Con la Matita (`P`), fai clic su una traccia per piazzare il pattern. Fai di nuovo clic più a destra per piazzarlo ancora, oppure usa il Pennello (`B`) e trascina per dipingere clip una dopo l'altra.
4. Fai clic sul pattern della melodia nel Selettore e piazzalo su un'altra traccia.
5. Premi `L` per passare alla modalità Brano, poi `Space`. La Playlist suona dalla posizione del brano. Fai clic sul righello per impostarla.

## Mixare e aggiungere un effetto

1. Premi `F9` per mostrare il [Mixer](/wiki/mixer/). I cinque canali iniziali suonano già attraverso gli insert da 1 a 5, e un canale aggiunto dal menu `+ aggiungi canale` prende un insert libero. Il numero nella casella di instradamento del canale nel Channel Rack dice quale.
2. Per spostare i canali su insert liberi tutti loro, selezionali nel Channel Rack e premi `Ctrl+L`. Ogni insert prende il nome e il colore del canale.
3. Trascina il fader di un insert per impostarne il livello.
4. Fai clic su un insert per selezionarlo. Il pannello degli effetti a destra mostra i suoi dieci slot.
5. Fai clic su uno slot vuoto e scegli un effetto. Fai clic sul nome dello slot per mostrare o nascondere la finestra dell'effetto. La spia dello slot mette in bypass l'effetto.

## Esportare il brano

![La finestra di esportazione: intervallo e coda a sinistra, formato, profondità di bit e frequenza di campionamento a destra, opzioni del mix sotto](/assets/img/wiki/export.webp)

1. Premi `Ctrl+S` e salva prima il progetto.
2. Scegli `File › Esporta › File Wave...`. Per altri formati lo stesso menu ha `File MP3...`, `File OGG...` e `File FLAC...`.
3. Imposta `Estensione` su `Brano (Playlist)`. Le altre scelte sono `Pattern corrente` e `Selezione della Playlist`.
4. Lascia le altre opzioni come sono.
5. Fai clic su `Esporta...`, scegli il nome del file e aspetta il render.

## A cosa serviva ogni tasto

| Comando | Tasto | Cosa fa |
|---|---|---|
| Channel Rack | `F6` | Mostra o nasconde il Channel Rack |
| Prossimo pattern vuoto | `F4` | Va al prossimo pattern vuoto e gli dà un nome |
| Piano Roll | `F7` | Mostra o nasconde il Piano Roll |
| Playlist | `F5` | Mostra o nasconde la Playlist |
| Modalità Pattern o Brano | `L` | Cambia ciò che suona `Space` |
| Mixer | `F9` | Mostra o nasconde il Mixer |
| Manda a insert liberi | `Ctrl+L` | Dà ai canali selezionati i loro insert del Mixer |
| Salva | `Ctrl+S` | Salva il progetto come `.lynx` |
| Esporta | `Ctrl+R` | Apre la finestra di esportazione con l'ultimo formato usato |

## Da sapere

- In modalità Pattern senti solo il pattern corrente. Se il brano è muto dopo l'arrangiamento, controlla che il trasporto sia in modalità Brano.
- La finestra di esportazione parte su `Brano (Playlist)` quando il trasporto è in modalità Brano e la Playlist ha delle clip. Altrimenti parte su `Pattern corrente`.
- Il clic del metronomo non fa mai parte di un'esportazione.
- Il piede della finestra di esportazione mostra la durata del brano in battute, il tempo totale e lo spazio su disco che serve al file.
- Di default un nuovo progetto ha un Limiter sull'insert Master. Fa parte di ciò che senti e di ciò che esporti.
- Annulla è `Ctrl+Z`. Premuto di nuovo, ripete quel passaggio. `Ctrl+Alt+Z` va più indietro.

## Correlati

- [Un giro della finestra](/wiki/tour/)
- [Esportazione](/wiki/exporting/)
- [Automazione](/wiki/automation/)
- [Registrazione](/wiki/recording/)
- [Strumenti ed effetti](/instruments-and-effects/)
