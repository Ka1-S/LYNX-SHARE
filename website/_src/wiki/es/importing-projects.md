---
source: 532b2eee04
---
`Archivo › Abrir...` (`Ctrl+O`) abre los proyectos `.lynx` propios de LYNX SHARE y también archivos `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` y `.mid`. Un proyecto de otro programa es una importación: se convierte en un proyecto nuevo de LYNX SHARE sin guardar, y el archivo original nunca se modifica.

## Abrir un proyecto de otro programa

1. Elige `Archivo › Abrir...` y selecciona el archivo `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` o de módulo de tracker (`.mod`, `.xm`, `.s3m`, `.it`).
2. Espera mientras LYNX SHARE busca los plugins y samples del proyecto.
3. Si no se encontró algo, se abre la ventana `Archivos que faltan`. Indícale dónde están tus samples, o ciérrala.
4. La barra de título muestra ahora de dónde viene el proyecto, por ejemplo "(importado de FL Studio)".
5. Elige `Archivo › Guardar`. LYNX SHARE pregunta dónde escribir un archivo `.lynx`.

## Qué pasa al proyecto

| Formato | Se convierte | No se convierte |
|---|---|---|
| `.flp` (FL Studio) | Canales, samples, patrones, notas slide y porta, todos los arreglos, marcadores, cambios de compás, automatización, vínculos a controladores, el Mixer con su enrutado, sidechains, EQ de pista y efectos nativos | Los paquetes FLEX están cifrados y nunca se abren. Un canal FLEX recibe un patch de sintetizador elegido según el nombre de su preset |
| `.als` (Live 11 y 12) | Pistas MIDI, de audio, de grupo y de retorno, tempo, compás, locators, el bucle, volumen, pan, silencio, solo y envíos con su automatización, clips de sesión y escenas, marcadores de warp, racks y Drum Racks, y muchos de los dispositivos propios de Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable y más) como propios de LYNX SHARE | Los dispositivos de Live sin equivalente en LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder y otros) se descartan y se listan |
| `.flm` (FL Studio Mobile) | Tempo, compás, un insert del Mixer por canal, una pista de la Playlist por carril, patrones, clips de audio con desfase y tono, kits de batería, DW Sampler y programas `.dwp`, el Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth y SuperSaw, los efectos, la automatización y los sets de acordes | Los demás generadores de FLM suenan con el sintetizador integrado |
| `.dawproject` (Bitwig Studio, Studio One, Cubase y otras apps) | Pistas, carpetas, clips, notas, audio con sus marcadores de warp, automatización, tempo, marcadores, el mezclador con envíos, escenas en la cuadrícula de clips y plugins VST3, VST2 y CLAP con su estado. El EQ, el compresor, la puerta y el limitador del formato pasan a ser los propios de LYNX SHARE | Los efectos de notas, las expresiones por nota y los dispositivos propios de la otra app se listan en el informe de importación |
| `.band` (GarageBand 10, GarageBand para iOS) | Pistas, regiones MIDI y de audio, tempo, compás, tonalidad, secciones como marcadores, nivel, pan y silencio, como pistas del diseño simple. Instrumentos Sampler hechos en GarageBand para iOS | Las pistas de instrumento suenan con el sintetizador integrado y la batería. Las regiones de Drummer, las carpetas de tomas y los cambios de tempo se listan. Los proyectos de GarageBand 11 y anteriores no se pueden leer |
| `.blx` (BandLab) | Pistas, regiones MIDI y de audio con sus bucles, tempo, compás, tonalidad, volumen, pan y silencio, automatización de volumen y pan, y los efectos comunes, como pistas del diseño simple | Las pistas de instrumento suenan con el sintetizador integrado y la batería. Los demás efectos y el preset de Mastering se dejan fuera y se listan. Probado solo con proyectos inventados |
| `.mid` | Notas, compases y todos los cambios de tempo | Un archivo MIDI no lleva sonidos: los canales reciben el sintetizador de LYNX SHARE o un canal MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (módulos de tracker) | Instrumentos como canales Sampler con sus samples, patrones, la lista de orden como Playlist, velocidad y tempo, volumen de nota, panorama, retardo y corte; los cambios de volumen y panorama como eventos del canal (MOD y XM), y los slides, el vibrato, los arpegios y las envolventes de tono como curvas de tono de las notas | Los demás efectos y las envolventes de instrumento se cuentan en el informe de importación. `Ver › Tracker` edita las notas a la manera de un tracker |
| `.zip` | El paquete de loops comprimido de FL Studio, o un proyecto exportado con sus samples: se descomprime en una carpeta propia y luego se abre el proyecto de dentro con sus samples | |

Los plugins VST2, VST3 y CLAP de terceros de un `.flp` o de un `.dawproject`, y los VST2 y VST3 de un `.als`, se cargan con su estado guardado si el mismo plugin está instalado en tu ordenador.

## Los plugins propios de FL Studio

Los generadores y efectos de FL Studio no se cargan. LYNX SHARE lee sus ajustes guardados y los reproduce con motores que recreó. Cada motor tiene un nivel de calidad, desde `Exact` (idéntico bit a bit al original en cada estado probado) hasta `Placeholder` (los ajustes se conservan, pero el plugin está en silencio o deja pasar el audio). Los niveles se explican en [Si vienes de FL Studio](/wiki/coming-from-fl-studio/).

Abrir un `.flp` funciona sin tener FL Studio instalado. Algunos motores necesitan ondas, samples o impulsos que se leen de tu propia instalación de FL Studio mientras funcionan.

Un `.flp` que usa el "FL Studio Mobile Rack" de FL Studio (el "Save as FLP" del propio FL Studio Mobile escribe uno por pista de instrumento) toca su instrumento y sus efectos con los mismos motores que un `.flm`, y el efecto "FL Studio Mobile Rack FX" también. Un rack con un kit de batería o varios instrumentos sigue sonando con el sintetizador integrado.

## Sonidos de fábrica de FL Studio Mobile

Los sonidos de fábrica que usa un `.flm` (kits de batería, programas de DirectWave, SoundFonts, las ondas de GMSynth) pertenecen a FL Studio Mobile y nunca se distribuyen con LYNX SHARE. LYNX SHARE los busca en tu propia copia de la carpeta `Resources` de la app: añádela en `Configuración › Archivos › Recursos de FL Studio Mobile` (elegir la carpeta que la contiene, o una carpeta de instalación de FL Studio, también funciona). La instalación de FL Studio guarda solo una parte de ellos y se busca después de tus carpetas. Los archivos que sigan faltando se listan en la ventana Archivos que faltan.

## Samples que faltan

![La ventana Archivos que faltan en su pestaña Samples: archivos encontrados en verde, los que faltan en ámbar, con Search folder, Locate y Skip](/assets/img/wiki/missing-files.webp)

La pestaña `Samples` lista todos los samples que no se encontraron, con su ruta guardada y lo que los usa.

| Comando | Qué hace |
|---|---|
| `Buscar en carpeta...` | Busca en una carpeta y sus subcarpetas todos los samples que faltan |
| `Localizar...` | Pregunta dónde está un sample. También se recogen los demás que estén en la misma carpeta |
| `Omitir` | Deja el sample fuera. Su canal queda en silencio |

`Aplicar` usa los archivos que se encontraron.

La pestaña `Encontrados automáticamente` lista los archivos que LYNX SHARE encontró por sí mismo en otra carpeta. Guarda el proyecto para conservar las ubicaciones nuevas.

## Plugins que faltan

LYNX SHARE busca cada plugin de terceros por su identificador, luego por su archivo y luego por su nombre. Un proyecto guardado con el VST2 se abre con el VST3 del mismo producto y al revés, pero entonces con los ajustes por defecto.

Cuando un efecto no está instalado, un efecto de LYNX SHARE con la misma función ocupa la ranura: una reverb para una reverb, un compresor para un compresor.

- La ranura muestra el nombre del plugin original con una insignia de LYNX SHARE.
- Si se puede leer el estado guardado, los ajustes se traducen. Si no, el sustituto parte de un preset por defecto. La tabla de productos, sus sustitutos y lo que se traduce de cada uno está en [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- La automatización de los parámetros del original se conserva, pero no controla al sustituto.
- La referencia y el estado del plugin original se quedan en el proyecto. Cuando instales el plugin, el proyecto se abrirá con él otra vez.
- `Omitir todos los sustitutos de LYNX SHARE de este proyecto`, en el menú de clic derecho de una ranura con sustituto, los desactiva todos a la vez.

El interruptor es `Sustituir los plugins que faltan por equivalentes de LYNX SHARE` en `Opciones › Configuración general...`, activado por defecto. Si está desactivado, la ranura de un efecto que falta se deja fuera del mezclador. Un instrumento que falta no se sustituye. Se convierte en un sintetizador integrado con un preset elegido según el nombre del canal.

Para recuperar el plugin real, instálalo, elige `Opciones › Buscar plugins` y abre el proyecto otra vez.

## Archivos MIDI

![La ventana Import MIDI data: pistas, canales, tipo de canal y opciones](/assets/img/wiki/midi-import-dialog.webp)

`Archivo › Abrir...` crea un proyecto nuevo a partir de un archivo MIDI. `Archivo › Importar › Archivo MIDI...` abre primero la ventana `Importar datos MIDI`, donde eliges qué traer.

| Ajuste | Qué hace |
|---|---|
| `¿Qué pistas importar?` | Todas las pistas, o una |
| `¿Qué canales importar?` | Haz clic en un canal MIDI para dejarlo fuera. El clic derecho conserva solo ese |
| `Tipo de canal` | `Sintetizador de LYNX SHARE` o `MIDI Out` |
| `Iniciar proyecto nuevo` | Inicia un proyecto en blanco. Desactivado: añade al proyecto abierto |
| `Crear un canal por pista` | Desactivado: un canal por canal MIDI |
| `Asignar pistas del Mixer a los canales nuevos` | Enruta los canales nuevos a inserts del mezclador consecutivos |
| `Importar compases` | El compás del archivo pasa a ser el del proyecto |

`Ctrl+M` en el [Piano Roll](/wiki/piano-roll/) importa un archivo MIDI en el canal actual.

## Importar pistas de otro proyecto

`Archivo › Importar › Pistas de otro proyecto...` trae pistas de otro proyecto al que tienes abierto, sin cerrarlo. Lee todos los formatos que lee `Archivo › Abrir...`.

1. Elige el proyecto. Una ventana lista sus pistas de la Playlist y lo que contiene cada una.
2. Marca las pistas que quieres.
3. Elige si vienen también `Con inserts del mezclador y efectos` y `Con automatización`, y `Colocarlas en` el inicio de la canción o el inicio de la selección de tiempo.
4. Haz clic para importar. Las pistas, canales, patrones e inserts nuevos llegan como un solo paso, así que un solo deshacer los quita todos.

## Conviene saber

- `Ayuda › Archivos que faltan...` vuelve a abrir la ventana más tarde. `Ayuda › Último informe de importación de proyecto` lista lo que se aproximó, se dejó fuera o no estaba instalado.
- Los clips de sesión de un set de Live van a la cuadrícula de clips del diseño de lanzamiento de clips. `Clips de sesión de sets de Live` en `Opciones › Configuración general...` los coloca en la Playlist en su lugar.

## Relacionado

- [Formatos de archivo](/wiki/file-formats/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exportación](/wiki/exporting/)
