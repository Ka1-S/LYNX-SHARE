---
source: a5c1d056c8
---
LYNX SHARE funziona anche senza aprire la sua finestra. Un'opzione renderizza un progetto in audio, altre lo convertono in un progetto FL Studio o in un set di Ableton Live, e diverse servono per i test. I controlli del progetto stesso usano le stesse opzioni.

Il programma è `LYNX.exe` su Windows e `LYNX` su macOS e Linux. Su macOS il percorso completo è `/Applications/LYNX.app/Contents/MacOS/LYNX`. Le opzioni sono le stesse su ogni sistema.

## Renderizzare un progetto in un file

1. Apri un terminale nella cartella che contiene il programma. Su Windows è la cartella di installazione o quella in cui hai estratto lo zip.
2. Esegui `LYNX.exe --export project.flp out.flac`, con il tuo progetto e il tuo file di uscita.
3. Aspetta che il programma termini. Non si apre nessuna finestra.
4. Leggi `out.flac.txt`, scritto accanto al file di uscita. Per un progetto importato elenca cosa è stato convertito, approssimato e lasciato fuori, e quali plugin sono stati trovati o mancano.

## Aprire un progetto

```
LYNX.exe project.lynx
```

Avvia LYNX SHARE con la finestra e apre il file. Accetta `.lynx`, `.flp`, `.als`, `.flm` e `.mid`. Un percorso relativo parte dalla cartella corrente.

```
LYNX.exe --no-plugins project.lynx
```

Apre un progetto `.lynx` senza caricare i suoi plugin di terze parti, come fa `File › Apri senza plugin di terze parti...`. Ogni plugin conserva le sue impostazioni. Usalo per un progetto che un plugin impedisce di aprire.

## Opzioni per l'uso di ogni giorno

| Opzione | Cosa fa |
|---|---|
| `--export <project> <out>` | Renderizza l'intero brano con la sua coda, come la finestra Esporta. L'estensione di `<out>` sceglie il formato: `.wav` (24 bit), `.flac`, `.ogg` o `.mp3` (320 kbps) |
| `--export ... --pattern` | Renderizza solo il pattern corrente |
| `--export ... --insert N` | Renderizza solo l'uscita dell'insert N del Mixer |
| `--export ... --rate N` | Renderizza a un'altra frequenza di campionamento, in Hz |
| `--stems <project> <base.wav>` | Scrive un WAV per ogni insert del Mixer che ha canali: `base - 1.wav`, `base - 2.wav` e così via |
| `--export-flp <project> <out.flp>` | Scrive un progetto FL Studio. Con `out.zip` scrive un pacchetto di loop zippato |
| `--export-flp ... --substitute` | Sostituisce i plugin solo LYNX con il plugin FL più vicino invece di renderizzarli in audio |
| `--export-als <project> <folder>` | Scrive un set di Ableton Live in `<folder>/<name> Project/<name>.als`, per Live 12 |
| `--export-als ... --live11` | Scrive il set per Live 11 |
| `--export-bundle <project> <folder>` | Scrive nella cartella il pacchetto di `File › Esporta › Per altre app...` |
| `--export-bundle ... --zip` | Comprime la cartella in zip. Viene sostituita da `<folder>.zip` |
| `--export-bundle ... --no-mixdown` | Lascia fuori dal pacchetto il mixdown |
| `--devices <report.txt>` | Elenca i tipi di driver audio e i dispositivi di questo computer |
| `--showcase <out.lynx>` | Scrive il progetto demo di LYNX SHARE |
| `--lang <id>` | Usa un'altra lingua dell'interfaccia per questa esecuzione, per esempio `--lang pt-BR` |
| `--menu-entry on` o `off` | Solo AppImage Linux: aggiunge LYNX SHARE al menu delle applicazioni, o lo rimuove |

`--export`, `--stems` e `--export-flp` accettano come progetto un file `.lynx`, `.flp` o `.als`. `--export-als` accetta anche `.flm`. `--export-bundle` accetta un file `.lynx`, `.flp` o `.als`.

Esempi:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --export-bundle song.lynx out-folder --zip
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opzioni pensate per i test

Esistono per controllare LYNX SHARE stesso. Sono utili in una segnalazione di bug.

| Opzione | Cosa fa |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Esegue l'autotest senza un dispositivo audio. Il report finisce con `result=OK` se passa |
| `--snapshot <prefix> [project]` | Disegna l'interfaccia e le sue finestre in file PNG col nome dato dal prefisso. Le immagini di questo wiki sono fatte così |
| `--render <project> <out.wav> <seconds>` | Renderizza quei secondi del pattern corrente in un WAV a 24 bit |
| `--plugintest <plugin> <report.txt>` | Carica e testa un plugin VST3 o CLAP |
| `--sandboxtest <plugin> <report.txt>` | Testa un plugin in un processo separato, anche uno che si blocca |
| `--scantest <folder> <report.txt>` | Cerca plugin in una cartella, in processi separati |
| `--fldb <report.txt> [project.flp]` | Riporta l'installazione di FL Studio trovata qui, il suo database dei plugin e i plugin corrispondenti di un progetto |
| `--devicetest <type> <device> <report.txt>` | Apre un driver audio per un secondo di silenzio e lo riporta |
| `--uiperf <project> <report.txt>` | Misura i tempi di disegno del Mixer, della Playlist e del Channel Rack |

Esempi:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Da sapere

- I risultati vanno in file, non nel terminale. `--export` scrive `<out>.txt`, `--export-flp` scrive `<out.flp>.txt`, `--export-als` scrive `export-als.txt` nella cartella, `--export-bundle` scrive `export-bundle.txt` nella cartella e `--stems` scrive `<base.wav>.txt`.
- Il programma legge lo standard input. In un ciclo di shell, aggiungi `< /dev/null` a ogni chiamata.
- `--snapshot` disegna a 1280x720 salvo che la variabile d'ambiente `LYNX_SNAPSHOT_SIZE` dica altro, per esempio `1920x1080`.
- `--lang` accetta gli id delle lingue elencate in `Opzioni › Impostazioni generali...`, come `en`, `pt-BR`, `de`, `es`, `fr` o `ja`. Un id sconosciuto viene ignorato.
- `--export` usa lo stesso percorso di render della finestra Esporta, ma nessuna delle altre opzioni della finestra. Per dither, profondità di bit o normalizzazione, usa la finestra.
- Il sorgente ha molte altre opzioni di test. Sono interne e possono cambiare da una versione all'altra.

## Correlati

- [Esportazione](/wiki/exporting/)
- [Formati di file](/wiki/file-formats/)
- [Compilare dai sorgenti](/wiki/build-from-source/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
