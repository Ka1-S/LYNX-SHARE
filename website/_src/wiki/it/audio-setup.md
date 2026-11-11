---
source: 3801d5daff
---
![La pagina Audio della finestra Impostazioni, senza ancora un dispositivo di uscita scelto](/assets/img/wiki/settings-audio.webp)

La pagina Audio della finestra Impostazioni sceglie dove va il suono e da dove arrivano le registrazioni. Aprila con `Opzioni › Impostazioni audio...`. Anche la configurazione guidata (`Opzioni › Configurazione guidata (lingua, audio, cartelle dei plugin)...`) prova l'uscita audio.

## Far uscire il suono

1. Apri `Opzioni › Impostazioni audio...`.
2. Scegli un `Driver`. La nota sotto il riquadro dice a cosa serve quel driver.
3. Scegli un `Dispositivo di uscita`.
4. Clicca `Riproduci un suono di prova`.
5. Leggi la sezione `Stato`. Mostra il driver in uso, la frequenza di campionamento e la latenza di uscita, oppure `Nessun dispositivo audio aperto: scegli un dispositivo di uscita.`

## Driver

Il riquadro `Driver` elenca i tipi di driver del tuo sistema.

| Sistema | Driver | Cosa fa |
|---|---|---|
| Windows | Windows Audio (condiviso) | Gli altri programmi mantengono il loro suono. Windows mixa tutti i programmi a passi fissi, quindi il buffer non può scendere sotto quel passo |
| Windows | Windows Audio (condiviso, periodo a bassa latenza) | Condiviso, ma chiede a Windows un periodo di mixaggio più corto. Solo alcuni driver lo supportano |
| Windows | Windows Audio (esclusivo, latenza minima) | LYNX SHARE usa il dispositivo da solo. Gli altri programmi restano muti finché suona |
| Windows | DirectSound (obsoleto) | Vecchio percorso audio di Windows, con latenza alta. Per quando gli altri falliscono |
| Windows | ASIO | Driver a bassa latenza. Il driver ASIO di un produttore funziona con il suo dispositivo |
| Windows | LYNX ASIO (integrato) | Il driver ASIO di LYNX SHARE, per qualsiasi dispositivo, in riproduzione e in registrazione. `Impostazioni del driver...` sceglie la modalità condivisa o esclusiva e i dispositivi |
| macOS | Core Audio | L'audio del sistema, condiviso con gli altri programmi |
| Linux | PipeWire (nativo) | LYNX SHARE parla direttamente con PipeWire. `System default` segue l'uscita scelta nelle impostazioni audio del sistema |
| Linux | JACK / PipeWire | Per un server JACK già in esecuzione, o per PipeWire con `pipewire-jack` |
| Linux | ALSA | Parla con la scheda audio. `default` o `pipewire` condividono il dispositivo, un dispositivo `hw:` prende la scheda solo per LYNX SHARE |

Su Linux, al primo avvio si apre il primo tra PipeWire, JACK e ALSA che ha dispositivi. Una scelta salvata viene mantenuta.

## Le impostazioni della pagina

| Impostazione | Cosa fa |
|---|---|
| `Driver` | Il tipo di driver, dalla tabella qui sopra |
| `Dispositivo di uscita` | Dove suona il Master |
| `Dispositivo di ingresso` | La sorgente per la registrazione audio e per gli ingressi degli insert del Mixer. `(nessuno)` per impostazione predefinita |
| `Frequenza` | Le frequenze che il dispositivo offre |
| `Dimensione del buffer` | Le dimensioni che il dispositivo offre, ognuna con la sua latenza in millisecondi. Buffer più piccoli rispondono più in fretta quando suoni. Quelli più grandi lasciano più spazio alla CPU, con meno click |
| `Impostazioni del driver...` | Apre la finestra del driver stesso (driver ASIO) |
| `Riproduci un suono di prova` | Riproduce un suono di prova attraverso il dispositivo di uscita |
| `Registra l'audio in ingresso` | Registra i primi due canali di ingresso in un nuovo insert quando nessun insert è armato |
| `Formato del file` | Le registrazioni sono WAV a 24 bit o WAV a 32 bit float, alla frequenza di campionamento del dispositivo |
| `Misura l'andata e ritorno` | Riproduce 8 click da `Uscita del ping` a `Ingresso del ping` attraverso un cavo o un loopback e misura il ritardo, così le take finiscono dove le hai suonate |
| `Regolazione fine (campioni)` | Sposta ogni nuova take. Un valore positivo è più presto nella Playlist |
| `Traccia del metronomo` | La traccia del Mixer su cui suona il metronomo. `---` è il Master. Il click non finisce mai in un export |
| `Suona le note troncate al trasporto` | Suona le note che attraversano la posizione di partenza |
| `Reimposta i plugin al trasporto` | Azzera le code degli effetti quando la riproduzione parte o salta |

## Quando non c'è suono

- Guarda prima `Stato`. Se nessun dispositivo è aperto, scegli un dispositivo di uscita.
- Prova un altro driver. Su Windows, `Windows Audio (condiviso)` lascia agli altri programmi il loro suono e `DirectSound (obsoleto)` c'è per quando gli altri falliscono.
- Controlla il fader del Master nel [Mixer](/wiki/mixer/) e la manopola `MASTER` nella barra degli strumenti.
- In modalità Pattern suona solo il pattern corrente. Se è vuoto non senti nulla. Premi `L` per la modalità Brano.
- Su Linux, se il suono si spezza, `Stato` mostra se il thread audio ha ottenuto la priorità real-time. [Risoluzione dei problemi](/wiki/troubleshooting/) ha la soluzione.

## Da sapere

- ASIO esiste solo su Windows.
- Con `Windows Audio (condiviso)` il buffer più piccolo è il passo di mixaggio del sistema. La nota sotto il riquadro `Driver` dà il valore per il tuo dispositivo. Per una latenza più bassa usa la modalità esclusiva o un driver ASIO.
- Con PipeWire il buffer va da 32 a 2048 campioni e la frequenza di campionamento da 44,1 a 192 kHz. Se PipeWire mantiene la sua frequenza, ricampiona il flusso di LYNX SHARE.
- Con PipeWire, quando un dispositivo con nome viene scollegato, LYNX SHARE torna a `System default` e mantiene la dimensione del buffer.
- `Stato` mostra anche il carico CPU dell'audio e conta i dropout (buffer underrun).
- Puoi elencare i driver e i dispositivi senza aprire la finestra. Vedi `--devices` in [Riga di comando](/wiki/command-line/).

## Correlati

- [Impostazioni](/wiki/settings/)
- [Registrazione](/wiki/recording/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
