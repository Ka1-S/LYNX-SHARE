---
source: b96330a448
---
![El editor de Automatización con una curva que sube a lo largo de seis compases](/assets/img/wiki/automation.webp)

Un clip de automatización es una curva en la [Playlist](/wiki/playlist/) que mueve un control mientras suena la canción: el volumen de un canal, un fader del mixer, un parámetro de un plugin, el tempo. El clip tiene su propia ventana de edición, el editor de Automatización.

Hay otras tres formas de mover un control. Los eventos son valores guardados dentro de un patrón. Un vínculo hace que un mando MIDI o un controlador interno maneje el control. Un modulador hace oscilar un mando alrededor de su propio valor.

## Automatizar un mando

1. Lleva el cursor de reproducción a donde debe empezar la automatización, o selecciona un rango de tiempo en la Playlist.
2. Haz clic derecho en el mando o fader y elige `Crear clip de automatización`.
3. El clip cae en la primera pista vacía de la Playlist y se abre el editor de Automatización. La curva empieza como una línea plana al valor actual del control.
4. Haz clic en el editor para añadir un punto y arrástralo a su sitio.
5. Mantén `Alt` y arrastra un segmento hacia arriba o abajo para curvarlo.
6. Reproduce la canción en modo Canción. El control sigue la curva.

Otras formas de obtener el mismo clip:

- `Añadir › Automatización del último parámetro modificado` crea un clip para el último control que moviste.
- El indicador del último modificado y `Herram. › Último modificado` abren el mismo menú que un clic derecho en el control.
- Para un plugin de terceros, abre el menú del canal en el Channel Rack o el menú de la ranura del efecto en el Mixer y usa `Automatizar parámetro del plugin`. `Automatizar controlador MIDI (CC, aftertouch, pitch bend)` está a su lado.

Para volver a abrir el editor, haz doble clic en el clip, o clic derecho y elige `Editar puntos...`. `Editar clip de automatización...` en el menú del propio control hace lo mismo.

## El editor de Automatización

| Comando | Tecla | Qué hace |
|---|---|---|
| Punto nuevo | Clic o clic derecho, y arrastrar | Añade un punto y lo mueve mientras mantienes el botón |
| Mover un punto | Arrastrar | `Shift` mantiene su tiempo, `Ctrl` mantiene su valor, `Alt` ignora el ajuste |
| Curvar un segmento | `Alt`+arrastrar arriba o abajo | Cambia la tensión del segmento bajo el ratón |
| Tirador de tensión | Arrastrar arriba o abajo | Curva el segmento. `Ctrl` es más fino, el clic derecho lo deja recto |
| Punto sobre la curva | `Shift`+clic derecho | Añade un punto sobre la línea |
| Menú del punto | Clic derecho en un punto | `Escribir valor...`, `Copiar valor`, `Pegar valor`, `Eliminar punto`, y `Curva completa` para escalar, voltear o enderezar todo |
| Zoom | `Ctrl`+rueda | Hace zoom en la vista |
| Desplazar | Arrastrar con el botón central | Mueve la vista |

## Modo LFO

![El editor de Automatización en modo LFO, con una onda triangular y los mandos SPD, TNS, SKEW, PW y AMNT](/assets/img/wiki/automation-lfo.webp)

Marca `LFO` en la parte superior del editor. Ahora una forma de onda da forma al valor del clip, y los puntos que dibujas fijan la amplitud del LFO en lugar del valor en sí.

| Ajuste | Qué hace |
|---|---|
| `SPD` | La duración de un ciclo, en tiempos |
| `TNS` | Tensión de la onda |
| `SKEW` | Asimetría de la onda |
| `PW` | Ancho de pulso |
| `AMNT` | Cuánto mueve el LFO el valor |
| `MUL` | Activado: el LFO se multiplica por el nivel de la envolvente. Desactivado: se suma |

## Eventos en un patrón

![El editor de eventos con eventos de volumen dibujados como barras](/assets/img/wiki/eventeditor.webp)

Los eventos pertenecen a un patrón y suenan dondequiera que suene ese patrón. Haz clic derecho en un control y elige `Editar eventos en el patrón` para abrir el editor de eventos de ese control.

- `Dibujar` fija valores mientras arrastras, y arrastrar con el botón derecho borra. `Línea` dibuja una rampa recta. `Seleccionar` marca un rango de tiempo para las herramientas de abajo.
- `Ajuste` va de 1/2 paso a 1 compás.
- `Escala`, `Desplazamiento` y `Aleatorizar` cambian los eventos de la selección, o de todo el patrón.
- `LFO` escribe una onda sinusoidal, triangular o cuadrada de 1 tiempo a 4 compases.
- `Borrar` elimina los eventos.
- `A automatización...` convierte los eventos en un clip de automatización y los quita del patrón.
- `Archivo` abre y guarda los eventos como archivo `.fsc`, e importa un archivo MIDI.

`Iniciar la canción con esta posición`, en el mismo menú de clic derecho, guarda el valor al que se restablece el control cada vez que arranca la canción.

## Grabar tus movimientos

Activa la grabación con `Automatización` marcado en el filtro de grabación, pulsa reproducir y mueve el control. En modo Canción, los movimientos se convierten en un clip de automatización. En modo Patrón, se convierten en eventos del patrón actual. Mira [Grabación](/wiki/recording/).

## Vínculos y moduladores

- `Vincular a un controlador MIDI (mueve un control del controlador)` se explica en [Controladores MIDI](/wiki/midi-controllers/).
- `Vincular a controlador interno` deja que una Envolvente, una Fórmula o un Peak Controller del proyecto maneje el control. Añade uno de ellos primero.
- `Modular` conecta un modulador, un LFO o una fuente aleatoria que mueve un mando de LYNX SHARE alrededor de su propio valor. `Herram. › Moduladores...` los lista.

## Carriles de automatización

Los carriles vienen desactivados. Activa `Carriles de automatización` en el menú de la Playlist. Entonces cada cabecera de pista tiene un botón que añade un carril debajo de la pista, y el menú del control gana `Automatizar en carril`. En un carril, haz clic para añadir o arrastra un punto, clic derecho para borrarlo, y `Alt`+arrastrar para curvar la línea. El menú del carril tiene `A clip de automatización`.

## Conviene saber

- Un clip nuevo empieza en la selección de tiempo si la hay; si no, en el cursor de reproducción en modo Canción. Desactiva `Crear clips de automatización en la posición de reproducción` en `Opciones › Configuración general...` para empezar al principio de la canción.
- Varios clips en un mismo control suenan cada uno en su sitio. Antes de su primer clip, un control conserva su propio valor.
- Al inicio de un clip, el control alcanza el nuevo valor en unos 8 ms. FL Studio lo alcanza deslizándose, así que un proyecto importado puede diferir un poco en esos puntos.
- Donde se solapan clips del mismo control, el que elige LYNX SHARE puede ser distinto del que elige FL Studio.
- Los moduladores solo funcionan en los mandos propios de LYNX SHARE, y se omiten al exportar a `.flp`.

## Relacionado

- [Playlist](/wiki/playlist/)
- [Grabación](/wiki/recording/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Atajos](/wiki/shortcuts/)
