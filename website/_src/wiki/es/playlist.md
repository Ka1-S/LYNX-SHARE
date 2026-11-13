---
source: 1b54c6493f
---
![La Playlist con clips en pistas](/assets/img/playlist.webp)

La Playlist es donde los patrones se convierten en una canción. Contiene tres tipos de clips en sus pistas: clips de patrón, clips de audio y clips de automatización. Pulsa `F5` para mostrarla u ocultarla, y `Enter` para maximizarla o restaurarla.

El transporte tiene dos modos. El modo Patrón reproduce el patrón actual en bucle. El modo Canción reproduce la Playlist. Pulsa `L` para cambiar.

## Colocar un patrón en la canción

1. Pulsa `F5` para mostrar la Playlist.
2. En el panel Selector, haz clic en un patrón. El Selector lista los patrones, el audio y la automatización del proyecto, y el que pulses pasa a ser la fuente de clips.
3. Pulsa `P` para el Lápiz.
4. Haz clic en una pista para colocar el clip. Mantén el botón pulsado y arrastra para posicionarlo.
5. Pulsa `B` para el Pincel y arrastra a lo largo de una pista para pintar clips uno tras otro.
6. Pulsa `L` para el modo Canción y luego `Space` para reproducir.

Se pueden arrastrar archivos de audio desde el [Browser](/wiki/browser/) a la Playlist.

## Herramientas

![La barra de herramientas de la Playlist con el cuadro de arreglo, las herramientas y el cuadro de ajuste](/assets/img/wiki/playlist-tools.webp)

Las letras funcionan mientras el piano del teclado de escritura (`Ctrl+T`) está desactivado.

| Herramienta | Tecla | Qué hace |
|---|---|---|
| Lápiz | `P` | El clic coloca el clip del Selector |
| Pincel | `B` | Arrastrar pinta clips uno tras otro |
| Borrador | `D` | El clic o arrastrar elimina |
| Silenciar | `T` | El clic o arrastrar silencia o activa clips |
| Cuchilla | `C` | Arrastra una línea para cortar a través de los clips, el clic corta en ese punto |
| Lazo | `E` | Arrastrar dibuja un cuadro de selección. `Shift` añade |
| Zoom | `Z` | El clic acerca, arrastrar un rango hace zoom a él, el clic derecho aleja |
| Escuchar | `Y` | El clic o arrastrar reproduce desde ese punto |
| Deslizar | `S` | Arrastrar desliza el contenido del clip dentro del clip |

## Trabajar con clips

- Arrastra un clip para moverlo. `Shift`+arrastrar hace una copia.
- Arrastra el final de un clip para cambiar su tamaño. Arrastrar el inicio lo recorta mientras el contenido se queda en su sitio.
- El clic derecho elimina un clip. `Ctrl`+arrastrar selecciona.
- Haz doble clic en un clip para abrirlo.
- El botón de la esquina superior izquierda de un clip abre el menú del clip (hacer único, renombrar y colorear, trocear, y para audio: tono, ajustar al tempo, invertir).
- El menú de un clip de audio sigue la disposición de FL Studio en dos columnas. La columna izquierda tiene la sección `Clip de audio` (`Preescucha`, `Silenciado`, `Renombrar y colorear...`, `Cambiar color...`, `Color aleatorio`, `Cambiar icono...`, `Seleccionar canal de origen`, `Configuración del canal`, `Hacer único`, `Seleccionar todos los clips similares`, `Delete`, `Propiedades...`) y la sección `Región` (`Seleccionar región`, `Trocear`). La columna derecha tiene `Sample` (`Hacer único como muestra`, `Editar muestra`, `Extraer stems del sample`, `Detectar tempo`, `Ajustar al tempo`), `Automatización` (`Automatizar › Paneo` o `Volumen`, `Fundido cruzado con`) y `Fundidos` (`Restablecer fundidos`). Los elementos propios de LYNX SHARE, como `Dividir aquí`, `Reversa`, `Normalizar` y `Convertir a MIDI`, van al final, bajo una línea.
- `Automatizar › Paneo` o `Volumen` crea un clip de automatización tan largo como el clip de audio, en su pista, para el pan o el volumen del ajuste Levels del canal.
- Mantén `Alt` al arrastrar para ignorar el ajuste.
- Los clips de audio tienen tiradores de fundido y una ganancia de clip que se arrastra hacia arriba o abajo. Haz clic derecho en un tirador de fundido para ver su menú, con `Crear fundido cruzado`. Haz clic derecho en el tirador de ganancia para `Restablecer ganancia` y `Normalizar`.
- `Shift+M` activa Stretch: cambiar el tamaño de un clip de audio entonces estira el audio en lugar de cortarlo.
- `Shift+Left` y `Shift+Right` mueven los clips seleccionados un paso de ajuste, `Shift+Up` y `Shift+Down` una pista. `Alt+Left` y `Alt+Right` los mueven un píxel de pantalla con el zoom actual, sin ajuste. Sin ningún clip seleccionado, se mueven todos los clips.
- `Hacer único como muestra` en el menú de un clip de audio da al clip un archivo nuevo con lo que sonaba, con su tono, estiramiento e inversión ya aplicados al audio.
- Un clip de audio con marcadores de warp los muestra como pequeñas banderas en su borde superior. Arrastra una bandera hacia un lado para mover el marcador.

## Menús Edit y Select

El menú de la Playlist tiene un submenú `Editar` y uno `Seleccionar`. `Editar` contiene los comandos de clips: cortar, copiar y pegar, `Desplazar a la izquierda`, `Desplazar a la derecha`, `Desplazar arriba`, `Desplazar abajo`, `Hacer único`, `Hacer único como muestra`, `Insertar espacio`, `Eliminar espacio`, los comandos de unión y las opciones de fundido. `Seleccionar` tiene `Seleccionar por la fuente seleccionada` (`Shift+C`), `Invertir selección` (`Shift+I`), `Deseleccionar` (`Ctrl+D`), los clips silenciados y los que se solapan, y `Lazo mágico`. Con `Lazo mágico` activado, mueve el ratón en un bucle alrededor de los clips, sin pulsar ningún botón, para seleccionarlos.

## Pistas

En la cabecera de una pista, el clic selecciona, `Ctrl`+clic añade o quita, `Shift`+clic selecciona un rango y el clic derecho abre el menú de la pista. `Shift`+rueda mueve la pista hacia arriba o abajo. La luz de la pista la silencia (clic) o la pone en solo (`Ctrl`+clic o clic derecho). Arrastra el separador entre dos pistas para cambiar el tamaño de una, o `Shift`+arrastrar para cambiar el de todas.

`Ver` en el menú de la Playlist elige qué muestran los encabezados de pista: `Mostrar controles en las pistas de audio`, `Mostrar niveles en las pistas de audio` y `Mostrar niveles en las pistas de instrumento`.

`Modo de pista` en el menú de la pista convierte una pista en `Sin asignar`, `Pista de audio` o `Pista de instrumento`, como en FL Studio. `Pista de audio` lista primero `Nueva(s) pista(s) del mezclador` y luego cada insert del mixer por número y nombre. La elección se aplica a todas las pistas seleccionadas. `Pista de instrumento` tiene `Usar un canal existente`, `Más plugins...` y la lista de instrumentos del Channel rack.

El `+` de arriba a la izquierda de las cabeceras de pista abre las mismas opciones para una pista nueva: `Añadir pista de audio`, `Más plugins...` y los instrumentos. Un clic derecho en `+` añade de inmediato una pista de audio.

## Pattern Blocks

Con `Bloques de patrón (proyecto actual)` activado en el menú de la Playlist, una sección bajo las pistas tiene una fila por patrón. Ahí, el Lápiz coloca un bloque y lo arrastra a lo largo de su fila, el Pincel pinta bloques y la Goma los elimina. El clic derecho borra con cualquier herramienta.

## Regla, bucle y marcadores

- Haz clic en la regla para fijar la posición de la canción.
- Arrastrar con el botón derecho, `Ctrl`+arrastrar o doble clic y arrastrar en la regla crea una selección de tiempo, que es también el bucle. `Shift`+arrastrar la mueve.
- `Ctrl+Ins` inserta la longitud de la selección de tiempo en todas las pistas, `Ctrl+Del` la quita.
- `Alt+T` añade un marcador. Haz clic en un marcador para saltar a él.
- `Ctrl`+rueda hace zoom, `Alt`+rueda cambia la altura de las pistas.

## Automatización

Haz clic derecho en un control o fader y elige `Crear clip de automatización`. El clip aparece en la Playlist. Con el Lápiz, arrastra sus puntos y haz clic en la curva para añadir uno. Los carriles de automatización bajo las pistas son opcionales y están desactivados por defecto. Consulta [Automatización](/wiki/automation/).

Haz clic derecho en un punto para ver su menú. Su lista `Modo` tiene los 13 modos de curva de FL Studio para el segmento que termina en ese punto: `Suavizar`, `Retención`, tres curvas simples, tres curvas dobles, `Media senoidal`, `Escalera`, `Escalera suave`, `Pulso` y `Onda`. Un punto nuevo toma el modo usado en último lugar. Los modos se leen y se escriben en los archivos .flp.

## Arreglos

El cuadro de arreglo de la barra de herramientas cambia entre arreglos. Un proyecto puede tener varios.

## Teclas

| Comando | Tecla | Qué hace |
|---|---|---|
| Seleccionar todo | `Ctrl+A` | Selecciona todos los clips |
| Copiar, cortar, pegar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, corta o pega clips |
| Duplicar | `Ctrl+B` | Duplica la selección |
| Eliminar | `Del` | Elimina los clips seleccionados |
| Eliminar con desplazamiento | `Shift+Del` | Elimina los clips seleccionados y cierra los huecos |
| Edición con desplazamiento | `Shift+S` | Activa o desactiva la edición con desplazamiento |
| Silenciar, activar | `Alt+M` / `Shift+Alt+M` | Silencia o activa los clips seleccionados |
| Desplazar clips | `Shift+Left` / `Shift+Right` | Mueve los clips seleccionados un paso de ajuste |
| Desplazar a otra pista | `Shift+Up` / `Shift+Down` | Mueve los clips seleccionados una pista |
| Empujar clips | `Alt+Left` / `Alt+Right` | Mueve los clips seleccionados un píxel de pantalla, sin ajuste |
| Hacer único | `Ctrl+Shift+V` | Hace únicos los clips seleccionados |
| Fusionar | `Ctrl+G` | Fusiona clips de patrón |
| Stretch | `Shift+M` | Activa Stretch al cambiar el tamaño de clips de audio |
| Insertar tiempo | `Ctrl+Ins` | Inserta la longitud de la selección de tiempo en todas las pistas |
| Eliminar tiempo | `Ctrl+Del` | Quita la selección de tiempo en todas las pistas |
| Añadir marcador | `Alt+T` | Añade un marcador |
| Zoom | `PageUp` / `PageDown` | Acerca o aleja |
| Ajustar zoom | `Shift+4` | Hace zoom para mostrar todo |
| Maximizar | `Enter` | Maximiza o restaura la Playlist |

## Conviene saber

- La Playlist solo se oye en el modo Canción. En el modo Patrón, `Space` reproduce el patrón actual, sea lo que sea lo que haya en las pistas.
- `Ver › Estilo de diseño › Flujo por lanzamiento de clips` añade una cuadrícula de clips y escenas junto a la Playlist. Es opcional. Consulta [Disposiciones](/wiki/layouts/).
- Con varios clips de automatización superpuestos en un mismo control, la elección de cuál suena puede diferir de la de FL Studio (notas de la versión 0.1.25).
- Como en FL Studio, un patrón suena desde un solo clip a la vez. Cuando empieza un clip posterior del mismo patrón, en cualquier pista, el clip anterior deja de iniciar notas desde ahí. Los clips de patrones distintos suenan juntos. Esto vale en proyectos nuevos y en proyectos de FL Studio. Un proyecto .lynx guardado antes de la 0.1.27 sigue tocando cada clip completo.
- Un tempo movido por un controlador MIDI vinculado no cambia el tamaño de los clips de audio.
- En Linux, el vídeo necesita FFmpeg.

## Relacionado

- [Channel Rack](/wiki/channel-rack/)
- [Automatización](/wiki/automation/)
- [Grabación](/wiki/recording/)
- [Exportación](/wiki/exporting/)
- [Disposiciones](/wiki/layouts/)
