---
source: c82c79e958
---
LYNX SHARE tiene tres diseños. Si vienes de Logic, Cubase, Reaper, Pro Tools o Studio One, empieza por el **Diseño simple con el Mixer a mano**: una pista por instrumento o voz en una línea de tiempo, como ya conoces. El Diseño Patrón, la forma de trabajar de FL Studio, está ahí para cuando quieras patrones.

## Tus primeros pasos

1. **Elige el diseño.** La primera vez que arranca LYNX SHARE, una tarjeta de bienvenida pregunta de dónde vienes. Haz clic en `Logic, Cubase, Reaper, Pro Tools o Studio One`: el Diseño simple se abre en su tarjeta de nueva pista, y el Mixer aparece con tu primera pista. Más tarde, `Ver › Estilo de diseño › Diseño simple` cambia a él en cualquier momento, y `F9` abre el Mixer.
2. **Añade una pista.** `+ Añadir pista` ofrece `Voz / audio`, `Instrumento`, `Batería`, `Sampler`, `Guitarra / bajo`, `Importar` y `Loops`. Toca una pista de instrumento con las teclas del ordenador, el teclado en pantalla o un teclado MIDI.
3. **Graba.** Elige tu dispositivo de audio en `Opciones › Configuración de audio...` y consulta [Graba tu voz](/learn/record-voice/).

## Qué es igual

- Pistas en una línea de tiempo con clips de audio, MIDI y automatización.
- Grabación de audio y MIDI, con tomas en bucle y comping.
- Un Mixer con inserts, envíos, sidechain, grupos y compensación de retardo.
- Plugins de terceros: VST3 y CLAP en todos los sistemas, Audio Units en macOS, LV2 en Linux, VST2 en Windows y Linux.
- Exportación de la canción, de stems por pista y de MIDI.

## Qué es distinto

- **Las pistas y los inserts del mixer son independientes.** En el Diseño simple, cada pista nueva recibe su propio canal y su propia pista de mixer. En el Diseño Patrón, tú enrutas los canales a los inserts, como en FL Studio.
- **Patrones.** En el Diseño Patrón, las notas viven en patrones que colocas en la Playlist, y un patrón puede contener varios instrumentos.
- **Las teclas siguen a FL Studio.** `Ctrl+Z` deshace un paso y, pulsado otra vez, lo rehace. `Ctrl+Alt+Z` retrocede más.
- **No hay importación de proyectos de Logic, Cubase, Reaper ni Pro Tools.** Traslada proyectos con archivos DAWproject, stems o MIDI (abajo).

## Teclas

| Para | Pulsa |
|---|---|
| Reproducir, o parar y volver | `Space` |
| Activar o desactivar la grabación | `R` |
| Mixer | `F9` |
| Piano Roll | `F7` |
| Playlist | `F5` |
| Browser | `Alt+F8` |
| Abrir, guardar | `Ctrl+O`, `Ctrl+S` |
| Deshacer, y retroceder más | `Ctrl+Z`, `Ctrl+Alt+Z` |
| Exportar audio, MP3, MIDI | `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+Shift+M` |

`Ayuda › Atajos` enumera todas las teclas. Consulta [Atajos](/wiki/shortcuts/).

## Trasladar proyectos

- **Cubase, Studio One y Bitwig**: guarda allí un archivo DAWproject (`.dawproject`) y ábrelo con `Archivo › Abrir...`. Pasan las pistas, carpetas, clips, notas, el audio con sus marcadores de warp, la automatización, el tempo, los marcadores, el mixer con envíos, y los plugins VST3, VST2 y CLAP con su estado. `Archivo › Exportar › DAWproject (.dawproject)...` hace el camino inverso.
- **Logic, Reaper y Pro Tools**: exporta cada pista como un archivo de audio o MIDI desde el inicio de la canción a una carpeta. En LYNX SHARE haz clic en `+ Añadir pista` y luego en `Carpeta...` junto a `Importar`: cada archivo se convierte en una pista que empieza al principio.
- **Para sacar**: la ventana de exportación escribe un archivo por insert del mixer o por canal, y `Archivo › Exportar › Archivo MIDI...` escribe las notas.

Consulta [Formatos de archivo](/wiki/file-formats/) y [Exportar](/wiki/exporting/).

## Conviene saber

- Logic es una marca de Apple Inc., Cubase de Steinberg Media Technologies, Reaper de Cockos Incorporated, Pro Tools de Avid Technology, Studio One de PreSonus Audio Electronics y Bitwig de Bitwig GmbH. LYNX SHARE es un programa independiente, sin afiliación ni aval de ellos.
- El Diseño de lanzamiento de clips añade una cuadrícula de clips con escenas junto a la línea de tiempo. Consulta [Disposiciones](/wiki/layouts/).

## Relacionado

- [Disposiciones](/wiki/layouts/)
- [Mixer](/wiki/mixer/)
- [Grabación](/wiki/recording/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Aprende: tutoriales](/learn/)
