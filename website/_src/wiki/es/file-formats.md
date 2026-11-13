---
source: a79e43f031
---
LYNX SHARE guarda los proyectos como archivos `.lynx`. También abre proyectos de otros programas, carga los archivos de audio habituales y exporta audio, MIDI y proyectos para otros programas. La tabla lista todos los formatos de la 0.1.26 alpha.

Un proyecto abierto desde el formato de otro programa es una importación. Entonces `Archivo › Guardar` pregunta dónde escribir un archivo `.lynx`, y el archivo original nunca se modifica.

## Abrir un proyecto de otro programa

1. Elige `Archivo › Abrir...` (`Ctrl+O`).
2. Selecciona un archivo `.flp`, `.als`, `.flm`, `.mid` o `.zip`.
3. Para un `.flp`, elige `Ayuda › Last FL Studio import report` para leer qué se aproximó o se dejó fuera.
4. Elige `Archivo › Guardar` y selecciona dónde escribir el archivo `.lynx`.

## Formatos

| Formato | Abrir | Guardar / Exportar | Notas |
|---|---|---|---|
| `.lynx` | `Archivo › Abrir...` | `Archivo › Guardar`, con autoguardado | El formato de proyecto propio de LYNX SHARE (JSON) |
| `.zip` | `Archivo › Abrir...` lo extrae a una carpeta y abre el proyecto de dentro | `Archivo › Exportar › Proyecto con samples (.zip)...` | También abre el paquete de loops comprimido de FL Studio |
| `.lynxpack` | Se abre tras un resumen. Una parte se fusiona con el proyecto del anfitrión | `Archivo › Sesión online › Enviar proyecto...` | Un proyecto o una parte enviados en una [sesión online](/wiki/online-sessions/). Las partes con plugins de terceros viajan como renders de audio más los ajustes del plugin |
| `.flp` | `Archivo › Abrir...` | `Archivo › Exportar › Proyecto de FL Studio...`, un proyecto de FL Studio 2026 o un paquete de loops comprimido | Los plugins nativos suenan con motores recreados de exactitud variable. Los packs FLEX nunca se abren. Al exportar, los instrumentos y efectos exclusivos de LYNX se renderizan a audio o se cambian por el plugin de FL más cercano, y los moduladores se dejan fuera. Mira [Si vienes de FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Archivo › Abrir...` | No | Proyecto de FL Studio Mobile. Parcial: los instrumentos distintos de MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, el Slicer y DirectWave suenan con el sintetizador integrado |
| `.als` | `Archivo › Abrir...`, sets de Live 11 y 12 | `Archivo › Exportar › Set de Ableton Live (.als)...`, para Live 12 o Live 11 | Al importar, se descartan los dispositivos de Live sin equivalente en LYNX SHARE. Al exportar, los instrumentos y efectos propios de LYNX SHARE no se convierten, y se descartan la automatización de parámetros de efectos y las rutas de insert a insert. Mira [Si vienes de Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Archivo › Abrir...` crea un proyecto nuevo. `Archivo › Importar › Archivo MIDI...` y el Piano Roll importan al actual | `Archivo › Exportar › Archivo MIDI...` (`Ctrl+Shift+M`) | |
| `.wav` | Como sample o clip de audio | `Archivo › Exportar › Archivo WAV...`, 16 bits, 24 bits o 32 bits flotante | Dither, información de tempo, marcadores de bucle y marcadores de la Playlist opcionales |
| `.mp3` | Como sample o clip de audio | `Archivo › Exportar › Archivo MP3...` | Bitrate constante |
| `.ogg` | Como sample o clip de audio | `Archivo › Exportar › Archivo OGG...` | |
| `.flac` | Como sample o clip de audio | `Archivo › Exportar › Archivo FLAC...`, compresión de 0 a 8 | Sin pérdida en todos los niveles |
| `.aif`, `.aiff` | Como sample o clip de audio | No | |
| `.wv` | Como sample | No | Samples WavPack, como vienen en los packs de FL Studio |
| `.sf2`, `.sf3` | Los reproduce LYNX Soundfont | No | SoundFonts |
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
| `Archivo › Exportar › Huesos del proyecto...` | Escribe en una carpeta presets y partituras de canales y efectos |

La ventana de exportación renderiza la canción, el patrón actual o la selección de la Playlist.

## Desde la línea de comandos

En Windows el programa es `LYNX.exe`. En macOS y Linux es `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` y `--export-flp` también aceptan un `.als` como entrada.

## Conviene saber

- Los archivos de audio de más de 30 segundos se leen en streaming desde una caché en disco.
- Un `.flp`, `.als` o `.flm` nunca se escribe al guardar. Los comandos de exportación escriben archivos nuevos.
- Los archivos `.wv` son de solo lectura.
- Un `.zip` se extrae primero a una carpeta y luego se abre el proyecto de dentro.

## Relacionado

- [Importar proyectos](/wiki/importing-projects/)
- [Exportación](/wiki/exporting/)
- [Línea de comandos](/wiki/command-line/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
