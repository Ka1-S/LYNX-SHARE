---
source: 358890815a
---
`Archivo › Abrir...` (`Ctrl+O`) abre los proyectos `.lynx` propios de LYNX SHARE y también archivos `.flp`, `.als`, `.flm` y `.mid`. Un proyecto de otro programa es una importación: se convierte en un proyecto nuevo de LYNX SHARE sin guardar, y el archivo original nunca se modifica.

## Abrir un proyecto de otro programa

1. Elige `Archivo › Abrir...` y selecciona el archivo `.flp`, `.als`, `.flm` o `.mid`.
2. Espera mientras LYNX SHARE busca los plugins y samples del proyecto.
3. Si no se encontró algo, se abre la ventana `Archivos que faltan`. Indícale dónde están tus samples, o ciérrala.
4. La barra de título muestra ahora de dónde viene el proyecto, por ejemplo "(importado de FL Studio)".
5. Elige `Archivo › Guardar`. LYNX SHARE pregunta dónde escribir un archivo `.lynx`.

## Qué pasa al proyecto

| Formato | Se convierte | No se convierte |
|---|---|---|
| `.flp` (FL Studio) | Canales, samples, patrones, notas slide y porta, todos los arreglos, marcadores, cambios de compás, automatización, vínculos a controladores, el Mixer con su enrutado, sidechains, EQ de pista y efectos nativos | Los paquetes FLEX están cifrados y nunca se abren. Un canal FLEX recibe un patch de sintetizador elegido según el nombre de su preset |
| `.als` (Live 11 y 12) | Pistas MIDI, de audio, de grupo y de retorno, tempo, compás, locators, el bucle, volumen, pan, silencio, solo y envíos con su automatización, clips de sesión y escenas, marcadores de warp, racks y Drum Racks, y muchos de los dispositivos propios de Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift y más) como propios de LYNX SHARE | Los dispositivos de Live sin equivalente en LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, parches de FM Operator y otros) se descartan y se listan. Los MIDI Effect Racks no se convierten |
| `.flm` (FL Studio Mobile) | Tempo, compás, un insert del Mixer por canal, una pista de la Playlist por carril, patrones, clips de audio con desfase y tono, kits de batería, DW Sampler y programas `.dwp`, el Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth y SuperSaw, los efectos, la automatización y los sets de acordes | Los demás generadores de FLM suenan con el sintetizador integrado |
| `.mid` | Notas, compases y todos los cambios de tempo | Un archivo MIDI no lleva sonidos: los canales reciben el sintetizador de LYNX SHARE o un canal MIDI Out |

Los plugins VST2, VST3 y CLAP de terceros de un `.flp`, y los VST2 y VST3 de un `.als`, se cargan con su estado guardado si el mismo plugin está instalado en tu ordenador.

## Los plugins propios de FL Studio

Los generadores y efectos de FL Studio no se cargan. LYNX SHARE lee sus ajustes guardados y los reproduce con motores que recreó. Cada motor tiene un nivel de calidad, desde `Exact` (idéntico bit a bit al original en cada estado probado) hasta `Placeholder` (los ajustes se conservan, pero el plugin está en silencio o deja pasar el audio). Los niveles se explican en [Si vienes de FL Studio](/wiki/coming-from-fl-studio/).

Abrir un `.flp` funciona sin tener FL Studio instalado. Algunos motores necesitan ondas, samples o impulsos que se leen de tu propia instalación de FL Studio mientras funcionan.

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

## Conviene saber

- `Ayuda › Archivos que faltan...` vuelve a abrir la ventana más tarde. `Ayuda › Last FL Studio import report` lista lo que se aproximó, se dejó fuera o no estaba instalado.
- Los clips de sesión de un set de Live van a la cuadrícula de clips del diseño de lanzamiento de clips. `Clips de sesión de sets de Live` en `Opciones › Configuración general...` los coloca en la Playlist en su lugar.

## Relacionado

- [Formatos de archivo](/wiki/file-formats/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exportación](/wiki/exporting/)
