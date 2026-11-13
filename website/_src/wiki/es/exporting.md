---
source: da5e41c322
---
![La ventana de exportación con WAV seleccionado: rango, cola y archivos a la izquierda, formato a la derecha, opciones de mezcla debajo](/assets/img/wiki/export.webp)

Todo lo que sale de LYNX SHARE pasa por `Archivo › Exportar`. Los formatos de audio (WAV, MP3, OGG y FLAC) comparten una sola ventana de exportación. MIDI, proyectos de FL Studio, sets de Ableton Live y proyectos comprimidos tienen sus propias entradas en el mismo menú.

## Exportar la canción como WAV

1. Elige `Archivo › Exportar › Archivo WAV...`.
2. Deja `Rango` en `Canción (Playlist)` y `Archivos` en `Un archivo (la mezcla)`.
3. Elige un `Resolución`: 16 bits, 24 bits o 32 bits (float).
4. Pon `Cola` en `Dejar el resto` para que las reverbs y las liberaciones suenen hasta el final tras el último compás.
5. Haz clic en `Exportar...`, elige un nombre y una carpeta, y espera al render.
6. Al terminar, LYNX SHARE indica dónde quedó el archivo y su nivel de pico.

`Ctrl+R` abre la ventana con el formato que exportaste la última vez. `Ctrl+Shift+R` la abre en MP3.

## Qué renderizar

| Ajuste | Qué hace |
|---|---|
| `Rango` | `Canción (Playlist)`, `Patrón actual` o `Selección de la Playlist` |
| `Reproducir` | Cuántas veces suena el rango en el archivo, desde `Una vez` hasta 16 veces |
| `Cola` | `Cortar el resto` se detiene justo al final. `Dejar el resto` deja que el sonido se apague solo. `Envolver el resto` mezcla la cola con el inicio, para bucles |
| `Cola máx.` | 3, 6, 12 o 30 segundos. La cola termina antes si el sonido se ha apagado |
| `Archivos` | Un archivo, o stems (mira abajo) |

## Formato de salida

Los cuatro botones eligen el formato. `Guardar también` escribe otros formatos a partir del mismo render, con el mismo nombre.

| Formato | Opciones |
|---|---|
| WAV | `Resolución`: 16 bits, 24 bits o 32 bits (float). `Dither`: `Desact.`, `TPDF` o `TPDF + noise shaping` |
| MP3 | `Tasa de bits`, constante |
| OGG | Tasa de bits |
| FLAC | `Resolución`, `Compresión` de `0 (más rápida)` a `8 (más pequeña)`, `Dither`. Sin pérdida en todos los niveles |

`Frec. muestreo` y `Canales` valen para todos los formatos. `Canales` es `Estéreo`, `Mono (ambos lados mezclados)`, `Mono (solo izquierda)` o `Mono (solo derecha)`.

## Opciones de mezcla

| Ajuste | Qué hace |
|---|---|
| `Normalizar (pico a 0 dB)` | Sube o baja todo el archivo para que su pico más fuerte llegue a escala completa |
| `Efectos del Master` | Desactivado: se puentean los efectos del insert Master |
| `Efectos de las inserciones` | Desactivado: se puentean los efectos de los demás inserts |
| `HQ para todos los plugins` | Los plugins usan su calidad de render. Desactivado: la exportación usa la calidad de reproducción |
| `Calentamiento de plugins` | Los plugins de terceros procesan un instante de silencio antes de que empiece la exportación |
| `Sin polifonía máxima` | Quita el límite de voces de los canales, así no se corta ninguna nota |
| `Recortar silencio de compensación` | Quita el silencio que la compensación de latencia de los plugins añade al principio |
| `Información de tempo`, `Marcadores de bucle`, `Marcadores de la playlist` | Solo WAV: guardan en el archivo el tempo, el bucle y los marcadores de la Playlist |
| `Mostrar archivo(s) al terminar` | Abre el gestor de archivos en el resultado |

## Stems y pistas

![La ventana de exportación en FLAC con un archivo por insert del mixer, nueve archivos en total](/assets/img/wiki/export-flac-stems.webp)

`Archivos` tiene tres modos de stems:

- `Uno por insert (stems)`: un archivo por cada insert que recibe sonido.
- `Uno por canal (stems)`: un archivo por cada canal que suena.
- `Uno por canal, solo instrumentos (sin envíos ni buses)`.

Marca `Y la mezcla completa` para obtener la mezcla junto a los stems. La línea de abajo muestra cuántos archivos se escribirán.

`Archivo › Exportar › Todas las pistas de la Playlist` y `Todos los grupos de pistas de la Playlist` escriben un archivo por pista de la Playlist o por grupo. En el menú del Mixer, `Grabación en disco › Renderizar las pistas armadas a archivo(s) WAV...` (`Alt+R`) abre la ventana de exportación solo para los inserts armados.

## Otras entradas del menú Exportar

| Comando | Tecla | Qué hace |
|---|---|---|
| `Archivo MIDI...` | `Ctrl+Shift+M` | Escribe las notas como archivo MIDI estándar (`.mid`) |
| `Masterizar...` | `Ctrl+Alt+M` | Renderiza el proyecto y lo masteriza a un objetivo de sonoridad en LUFS, en tu ordenador |
| `Proyecto con samples (.zip)...` | ninguna | El proyecto y sus samples en un solo archivo comprimido |
| `Proyecto de FL Studio...` | ninguna | Un `.flp`, o un paquete de loops comprimido con todos los samples |
| `Set de Ableton Live (.als)...` | ninguna | Un set para Live 12 o Live 11 |
| `Huesos del proyecto...` | ninguna | Presets y partituras de canales y efectos, en una carpeta |

## Exportar a FL Studio y Ableton Live

El `.flp` es un proyecto de FL Studio 2026. Los samplers, clips de audio, generadores y efectos propios de FL Studio, plugins de terceros, patrones, la Playlist, el Mixer y la automatización pasan con sus ajustes. Para lo que FL Studio no tiene, eliges entre `Renderizarlos a audio (suena igual)` y `Usar el plugin de FL más cercano (sigue editable, puede sonar distinto)`. Con la segunda opción, los instrumentos exclusivos de LYNX se dejan fuera. Los moduladores siempre se dejan fuera. Un informe lista qué se renderizó, sustituyó o dejó fuera. Los detalles están en [Si vienes de FL Studio](/wiki/coming-from-fl-studio/).

El `.als` contiene una pista MIDI por canal, pistas de audio, grupos, retornos, envíos, racks y Drum Racks, plugins VST2 y VST3 con su estado, marcadores, y automatización de volumen, pan y tempo. Los instrumentos y efectos propios de LYNX SHARE no se convierten, aunque se conservan sus pistas y notas. La automatización de parámetros de efectos y las rutas de insert a insert se descartan. Los detalles están en [Si vienes de Ableton Live](/wiki/coming-from-ableton/).

## Conviene saber

- `Dither` aparece atenuado en MP3, y las opciones de tempo y marcadores aparecen atenuadas en todos los formatos salvo WAV.
- Con `Selección de la Playlist` y sin selección en la Playlist, se renderiza toda la canción. La ventana te avisa.
- Si el insert Master está silenciado, la ventana avisa de que el archivo quedará en silencio.
- `Escribir también un vídeo con esta mezcla` aparece solo cuando el proyecto tiene una pista de vídeo, y necesita FFmpeg.
- `Renderizar a través del hardware en tiempo real` aparece solo cuando el proyecto tiene un insert de hardware activo. Esa exportación dura tanto como la canción.
- El render a audio, donde un insert se convierte en un clip de audio, suena unos 5 dB más fuerte que el insert que reemplaza en esta versión.

## Relacionado

- [Formatos de archivo](/wiki/file-formats/)
- [Línea de comandos](/wiki/command-line/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
