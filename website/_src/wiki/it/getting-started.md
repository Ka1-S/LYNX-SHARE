---
source: a6d9063ec3
---
LYNX SHARE è una DAW gratuita e open source per Windows, macOS e Linux. Lavora a pattern: li programmi nel Channel Rack e nel Piano Roll, li arrangi nella Playlist e li mixi nel Mixer. Apre i progetti di FL Studio (`.flp`) e i set di Ableton Live (`.als`), e salva nel suo formato `.lynx`.

È in versione alpha. Aspettati dei bug e salva spesso.

## Metterlo in funzione

1. Controlla la tabella qui sotto per vedere che il tuo sistema sia supportato.
2. Scaricalo e installalo. [Installazione](/wiki/install/) ha i passaggi per ogni sistema.
3. Avvialo. Una breve configurazione guidata sceglie la lingua, prova l'uscita audio ed elenca le cartelle dei plugin, che poi vengono scansionate in background. Puoi saltarla e riaprirla più tardi con `Opzioni › Configurazione guidata (lingua, audio, cartelle dei plugin)...`.
4. Se non senti nulla, vai a [Configurazione audio](/wiki/audio-setup/).
5. Leggi [Un giro della finestra](/wiki/tour/), poi fai [La tua prima canzone](/wiki/first-song/).

## Cosa ti serve

| Sistema | Requisito |
|---|---|
| Windows | Windows 10 o 11, 64 bit |
| macOS | macOS 11 Big Sur o successivo, Intel o Apple silicon |
| Linux | x86_64 con glibc 2.35 o successiva (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch o Manjaro aggiornati), X11 o XWayland |

Non ti serve un'interfaccia audio. LYNX SHARE suona attraverso il dispositivo audio che il tuo sistema usa già. Non ti serve nemmeno FL Studio. Se è installato sullo stesso computer, LYNX SHARE ne trova i sample e usa gli stessi file di plugin per i progetti `.flp`.

I plugin di terze parti sono facoltativi. LYNX SHARE ha i suoi strumenti ed effetti, elencati nel [catalogo](/instruments-and-effects/).

## Salvare il lavoro

`Ctrl+S` salva, `Ctrl+Shift+S` salva con un nuovo nome e `Ctrl+Alt+S` salva una nuova versione (Song_2, Song_3 e così via). C'è un salvataggio automatico, raggiungibile da `File › Apri cartella dei salvataggi automatici` e `File › Ripristina l'ultimo backup...`.

`File › Nuovo da modello` ha quattro punti di partenza: Vuoto, Basic beat, Trap with 808 e House.

## Il progetto demo

L'installer e lo zip per Windows, il `.dmg` per macOS e il `.tar.gz` per Linux includono un progetto demo, `Demo.lynx`. Aprilo con `File › Apri...` (`Ctrl+O`). Su Windows si trova nella cartella di installazione, accanto a `LYNX.exe`.

Puoi anche generarlo da un terminale. Vedi [Riga di comando](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Dove andare dopo

| Se vuoi | Leggi |
|---|---|
| Imparare ogni finestra | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Aprire un progetto di un altro programma | [Se vieni da FL Studio](/wiki/coming-from-fl-studio/), [Se vieni da Ableton Live](/wiki/coming-from-ableton/) |
| Usare i tuoi plugin VST, CLAP, LV2 o AU | [Plugin](/wiki/plugins/) |
| Cercare un tasto | [Scorciatoie](/wiki/shortcuts/) |
| Risolvere un problema | [Risoluzione dei problemi](/wiki/troubleshooting/) e le [FAQ](/faq/) |

## Da sapere

- Il file del programma si chiama `LYNX.exe` su Windows e `LYNX` su macOS e Linux. Anche la cartella delle impostazioni si chiama `LYNX`.
- Su Windows e macOS il primo avvio può mostrare un avviso di sistema, perché la build per Windows non è ancora firmata e quella per macOS non è notarizzata. [Installazione](/wiki/install/) spiega su cosa fare clic.
- L'interfaccia è in inglese di default su un sistema la cui lingua LYNX SHARE non ha. Puoi cambiarla in `Opzioni › Impostazioni generali...`. Una nuova lingua si applica dopo un riavvio.
- I pacchetti FLEX nei progetti di FL Studio sono cifrati e non vengono mai aperti. Quei canali ricevono una patch di synth scelta in base al nome del preset.

## Correlati

- [Installazione](/wiki/install/)
- [Configurazione audio](/wiki/audio-setup/)
- [Un giro della finestra](/wiki/tour/)
- [La tua prima canzone](/wiki/first-song/)
- [Download](/download/)
