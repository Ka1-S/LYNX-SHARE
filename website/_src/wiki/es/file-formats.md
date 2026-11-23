---
source: e1ff018baf
---
LYNX SHARE guarda los proyectos como archivos `.lynx`. También abre proyectos de otros programas, carga los archivos de audio habituales y exporta audio, MIDI y proyectos para otros programas. La tabla lista todos los formatos de la 0.1.31 alpha.

Un proyecto abierto desde el formato de otro programa es una importación. Entonces `Archivo › Guardar` pregunta dónde escribir un archivo `.lynx`, y el archivo original nunca se modifica.

## Abrir un proyecto de otro programa

1. Elige `Archivo › Abrir...` (`Ctrl+O`).
2. Selecciona un archivo `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid` o `.zip`.
3. Elige `Ayuda › Último informe de importación de proyecto` para leer qué se aproximó o se dejó fuera.
4. Elige `Archivo › Guardar` y selecciona dónde escribir el archivo `.lynx`.

## Formatos

| Formato | Abrir | Guardar / Exportar | Notas |
|---|---|---|---|
| `.lynx` | `Archivo › Abrir...` | `Archivo › Guardar`, con autoguardado | El formato de proyecto propio de LYNX SHARE (JSON). `Archivo › Guardar una copia para un LYNX anterior...` escribe una copia que abre 0.1.27 o 0.1.28, y lista lo que esa versión no puede conservar |
| `.zip` | `Archivo › Abrir...` lo extrae a una carpeta y abre el proyecto de dentro | `Archivo › Exportar › Paquete de loops comprimido...` | También abre el paquete de loops comprimido de FL Studio |
| `.lynxpack` | Se abre tras un resumen. Una parte se fusiona con el proyecto del anfitrión | `Archivo › Sesión online › Enviar proyecto...` | Un proyecto o una parte enviados en una [sesión online](/wiki/online-sessions/). Las partes con plugins de terceros viajan como renders de audio más los ajustes del plugin |
| `.flp` | `Archivo › Abrir...` | `Archivo › Exportar › Proyecto de FL Studio...`, un proyecto de FL Studio 2026 o un paquete de loops comprimido | Los plugins nativos suenan con motores recreados de exactitud variable. Los packs FLEX nunca se abren. Al exportar, los instrumentos y efectos exclusivos de LYNX se renderizan a audio o se cambian por el plugin de FL más cercano, y los moduladores se dejan fuera. Mira [Si vienes de FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Archivo › Abrir...` | `Archivo › Guardar una copia como proyecto de FL Studio Mobile (.flm)...` | Proyecto de FL Studio Mobile. Parcial: los instrumentos distintos de MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, el Slicer y DirectWave suenan con el sintetizador integrado. La exportación es una copia: conserva las notas, el tempo, el mixer, los instrumentos propios de FL Studio Mobile, el DW Sampler, 3x Osc, los drum kits, los efectos y la automatización del nivel del mixer, el panning, los efectos, el DW Sampler y GMSynth, con los samples al lado. En la copia, los demás instrumentos conservan sus notas en el MiniSynth, y los efectos sin módulo FLM y la automatización de otros destinos (los mandos propios de los sintetizadores, plugins de terceros) se dejan fuera; una ventana lo enumera todo antes de guardar. |
| `.als` | `Archivo › Abrir...`, sets de Live 11 y 12 | `Archivo › Exportar › Set de Ableton Live (.als)...`, para Live 12 o Live 11 | Al importar, se descartan los dispositivos de Live sin equivalente en LYNX SHARE. Al exportar, los instrumentos y efectos propios de LYNX SHARE no se convierten (Helix pasa a ser Wavetable), y se descartan la automatización de parámetros de efectos y las rutas de insert a insert. Mira [Si vienes de Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `Archivo › Abrir...` | `Archivo › Exportar › DAWproject (.dawproject)...` | El formato de intercambio abierto de Bitwig, Studio One, Cubase y otras apps: pistas, carpetas, clips, notas, audio con sus marcadores de warp, automatización, tempo, marcadores, el mezclador con envíos y plugins VST3, VST2 y CLAP con su estado. Los efectos de notas y las expresiones MIDI se dejan fuera al importar; la cuadrícula de clips, otros arreglos y algunos detalles por nota, al exportar. En ambas direcciones se lista lo que se dejó fuera |
| `.band` | `Archivo › Abrir...` (el paquete, su archivo `projectData` o un zip de él) | No | Proyecto de GarageBand 10 o GarageBand para iOS, como pistas del diseño simple. Parcial: las pistas de instrumento suenan con el sintetizador integrado y la batería, salvo los instrumentos Sampler hechos en GarageBand para iOS. Los proyectos de GarageBand 11 y anteriores no se pueden leer |
| `.blx` | `Archivo › Abrir...`, o su carpeta con la importación `Carpeta...` del diseño simple | No | Proyecto de BandLab descargado por Cakewalk o Sonar, como pistas del diseño simple con regiones, tempo, tonalidad, la mezcla, automatización de volumen y pan y los efectos comunes. Parcial: los sonidos de instrumento de BandLab y otros pedales se listan en el informe de importación |
| `.mid`, `.midi` | `Archivo › Abrir...` crea un proyecto nuevo. `Archivo › Importar › Archivo MIDI...` y el Piano Roll importan al actual | `Archivo › Exportar › Archivo MIDI...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `Archivo › Abrir...` | No | Módulos de tracker (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). Los instrumentos pasan a ser canales Sampler con sus samples, la canción pasa a ser clips de patrón con sus cambios de tempo, y `Ver › Tracker` edita las notas. Parcial: efectos como slides, vibrato y arpegios, envolventes y cambios de volumen sobre una nota que suena aparecen en el informe de importación |
| `.wav` | Como sample o clip de audio | `Archivo › Exportar › Archivo WAV...`, 16 bits, 24 bits o 32 bits flotante | Dither, información de tempo, marcadores de bucle y marcadores de la Playlist opcionales |
| `.mp3` | Como sample o clip de audio | `Archivo › Exportar › Archivo MP3...` | Bitrate constante |
| `.ogg` | Como sample o clip de audio | `Archivo › Exportar › Archivo OGG...` | |
| `.flac` | Como sample o clip de audio | `Archivo › Exportar › Archivo FLAC...`, compresión de 0 a 8 | Sin pérdida en todos los niveles |
| `.aif`, `.aiff` | Como sample o clip de audio | `Archivo › Exportar › Archivo AIFF...`, 16 bits o 24 bits | |
| `.m4a` | – | `Archivo › Exportar › Archivo M4A...` | Solo exportación. AAC con el codificador del sistema en Windows y macOS; no se ofrece en Linux |
| `.wv` | Como sample | No | Samples WavPack, como vienen en los packs de FL Studio |
| `.sf2`, `.sf3` | Los reproduce LYNX Soundfont | No | SoundFonts |
| `.dwp`, `.dwb` | Doble clic en el Browser, soltarlo en el Channel Rack o `Abrir programa / banco...` de DirectWave | Guardado de programas y bancos de DirectWave | Programas y bancos de DirectWave. Los samples se buscan donde los busca DirectWave |
| `.gig` | Como canal de DirectWave | No | Instrumentos de GigaStudio: capas de velocity, round robin, loops, afinación y la envolvente de amplitud. Los samples de release-trigger, los keyswitches y los samples comprimidos aún no se leen |
| `.ncw` | Como sample o clip de audio | No | Los samples comprimidos de Kontakt. Los instrumentos `.nki` de Kontakt necesitan el propio Kontakt como plugin; las bibliotecas protegidas nunca se descifran |
| `.fst` | Desde el Browser | No | Preset de un plugin nativo de FL Studio. Solo para los plugins para los que LYNX SHARE tiene motor |
| `.fsc` | Piano Roll y editor de eventos | Piano Roll y editor de eventos | Archivo de partitura y automatización de FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Cargar archivo de preset...` en el menú de presets de un plugin alojado | `Guardar preset...` y `Exportar archivo de preset...` | Presets de plugins de terceros. `.fxp` y `.fxb` son para plugins VST2 y `.vstpreset` para plugins VST3, y solo mientras el plugin se ejecuta dentro de LYNX SHARE |
| Archivo de vídeo | `Archivo › Importar › Archivo de vídeo...` pone un vídeo en la Playlist | `Escribir también un vídeo con esta mezcla` en la ventana de exportación | La importación usa los decodificadores del sistema en Windows y macOS, y necesita FFmpeg en Linux. La exportación necesita FFmpeg |

## Otras exportaciones

| Comando | Qué hace |
|---|---|
| Opción `Archivos` de la ventana de exportación | Stems: un archivo por insert del mixer, por canal, o por canal solo con instrumentos |
| `Archivo › Exportar › Todas las pistas de la Playlist` | Un archivo por pista de la Playlist |
| `Archivo › Exportar › Todos los grupos de pistas de la Playlist` | Un archivo por grupo de pistas |
| `Archivo › Exportar › Masterizar...` | Renderiza el proyecto y lo masteriza a un objetivo de sonoridad, en tu ordenador |
| `Archivo › Exportar › Para otras apps...` | Una carpeta con stems, archivos MIDI, un set de Ableton Live, un proyecto de FL Studio, la mezcla final y un README, comprimida si quieres |
| `Archivo › Exportar › Huesos del proyecto...` | Escribe en una carpeta presets y partituras de canales y efectos |

La ventana de exportación renderiza la canción, el patrón actual o la selección de la Playlist.

## Desde la línea de comandos

En Windows el programa es `LYNX.exe`. En macOS y Linux es `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` y `--export-flp` también aceptan un `.als` como entrada.

## Conviene saber

- Los archivos de audio de más de 30 segundos se leen en streaming desde una caché en disco.
- Un `.flp`, `.als`, `.flm`, `.dawproject`, `.band` o `.blx` nunca se escribe al guardar. Los comandos de exportación escriben archivos nuevos.
- Los archivos `.wv` son de solo lectura.
- Un `.zip` se extrae primero a una carpeta y luego se abre el proyecto de dentro.

## Relacionado

- [Importar proyectos](/wiki/importing-projects/)
- [Exportación](/wiki/exporting/)
- [Línea de comandos](/wiki/command-line/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
