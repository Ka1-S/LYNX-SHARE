---
source: de4ead4a7a
---
Esta página recorre una vez todo el camino: un patrón de batería, una melodía, un arreglo, una mezcla y un archivo exportado. Cada parte enlaza con la página que cubre su ventana al completo. Si en algún momento no oyes nada, consulta [Configuración de audio](/wiki/audio-setup/).

## Hacer un patrón de batería

1. Elige `Archivo › Nuevo desde plantilla › Vacío`. El proyecto empieza con cinco canales, Kick, Clap, Hat, Snare y Synth, y sin pasos.
2. Pulsa `F6` si el [Channel Rack](/wiki/channel-rack/) no se ve.
3. Haz clic en algunos pasos de la fila Kick. Si arrastras, dibujas varios, y el clic derecho borra.
4. Pulsa `Space`. El transporte está en modo Patrón, así que el patrón suena en bucle. Pulsa `Space` otra vez para detenerlo.
5. Dale a las filas Clap, Hat y Snare sus propios pasos.
6. Pulsa `F2` para nombrar y colorear el patrón.

Para añadir un sonido que no está, haz clic en el cuadro `+ añadir canal` bajo la última fila y elige un instrumento del menú. Para usar un sample tuyo, abre el [Browser](/wiki/browser/) con `Alt+F8`, haz clic en un archivo de audio para oírlo y haz doble clic para añadirlo como canal nuevo.

## Escribir una melodía

1. Pulsa `F4`. LYNX SHARE va al siguiente patrón vacío y pide su nombre.
2. Haz clic derecho en el nombre del canal Synth y elige `Piano Roll`.
3. Con el Lápiz (`P`), haz clic en la cuadrícula para añadir una nota.
4. Arrastra una nota para moverla y arrastra su extremo para cambiar su longitud. El clic derecho elimina una nota.
5. Pulsa `Space` para oír el patrón mientras editas.

`Ctrl+T` convierte el teclado de escritura en un piano, así que puedes probar notas con `Z` a `M` y `Q` a `P`. Vuelve a desactivarlo antes de usar atajos de letra como `P`.

## Arreglar los patrones

1. Pulsa `F5` para mostrar la [Playlist](/wiki/playlist/).
2. En el panel Selector, a la izquierda de la Playlist, haz clic en el patrón de batería. Ahora es la fuente de clips.
3. Con el Lápiz (`P`), haz clic en una pista para colocar el patrón. Haz clic otra vez más a la derecha para colocarlo de nuevo, o usa el Pincel (`B`) y arrastra para pintar clips uno tras otro.
4. Haz clic en el patrón de la melodía en el Selector y colócalo en otra pista.
5. Pulsa `L` para pasar al modo Canción y luego `Space`. La Playlist suena desde la posición de la canción. Haz clic en la regla para fijarla.

## Mezclar y añadir un efecto

1. Pulsa `F9` para mostrar el [Mixer](/wiki/mixer/). Los cinco canales iniciales ya suenan por los inserts 1 a 5, y un canal añadido desde el menú `+ añadir canal` toma un insert libre. El número del cuadro de ruta del canal en el Channel Rack indica cuál.
2. Para pasar canales a inserts libres propios, selecciónalos en el Channel Rack y pulsa `Ctrl+L`. Cada insert toma el nombre y el color del canal.
3. Arrastra el fader de un insert para fijar su nivel.
4. Haz clic en un insert para seleccionarlo. El panel de efectos de la derecha muestra sus diez ranuras.
5. Haz clic en una ranura vacía y elige un efecto. Haz clic en el nombre de la ranura para mostrar u ocultar la ventana del efecto. La luz de la ranura omite el efecto.

## Exportar la canción

![La ventana de exportación: rango y cola a la izquierda, formato, resolución y frecuencia de muestreo a la derecha, opciones de mezcla debajo](/assets/img/wiki/export.webp)

1. Pulsa `Ctrl+S` y guarda primero el proyecto.
2. Elige `Archivo › Exportar › Archivo WAV...`. Para otros formatos, el mismo menú tiene `Archivo MP3...`, `Archivo OGG...` y `Archivo FLAC...`.
3. Pon `Rango` en `Canción (Playlist)`. Las otras opciones son `Patrón actual` y `Selección de la Playlist`.
4. Deja las demás opciones como están.
5. Haz clic en `Exportar...`, elige el nombre del archivo y espera al render.

## Qué hizo cada tecla

| Comando | Tecla | Qué hace |
|---|---|---|
| Channel Rack | `F6` | Muestra u oculta el Channel Rack |
| Siguiente patrón vacío | `F4` | Va al siguiente patrón vacío y le pone nombre |
| Piano Roll | `F7` | Muestra u oculta el Piano Roll |
| Playlist | `F5` | Muestra u oculta la Playlist |
| Modo Patrón o Canción | `L` | Cambia lo que reproduce `Space` |
| Mixer | `F9` | Muestra u oculta el Mixer |
| Enviar a inserts libres | `Ctrl+L` | Da a los canales seleccionados sus propios inserts del Mixer |
| Guardar | `Ctrl+S` | Guarda el proyecto como `.lynx` |
| Exportar | `Ctrl+R` | Abre la ventana de exportación con el último formato usado |

## Conviene saber

- En el modo Patrón solo oyes el patrón actual. Si la canción suena en silencio tras arreglarla, comprueba que el transporte esté en modo Canción.
- La ventana de exportación empieza en `Canción (Playlist)` cuando el transporte está en modo Canción y la Playlist tiene clips. Si no, empieza en `Patrón actual`.
- El clic del metrónomo nunca forma parte de una exportación.
- El pie de la ventana de exportación muestra la duración de la canción en compases, su tiempo total y el espacio en disco que necesita el archivo.
- Por defecto, un proyecto nuevo tiene un Limitador en el insert Master. Forma parte de lo que oyes y de lo que exportas.
- Deshacer es `Ctrl+Z`. Si lo pulsas otra vez, rehace ese paso. `Ctrl+Alt+Z` retrocede más.

## Relacionado

- [Recorrido por la ventana](/wiki/tour/)
- [Exportación](/wiki/exporting/)
- [Automatización](/wiki/automation/)
- [Grabación](/wiki/recording/)
- [Instrumentos y efectos](/instruments-and-effects/)
