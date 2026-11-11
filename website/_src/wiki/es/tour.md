---
source: edf15b371e
---
![La ventana principal con el proyecto de demostración: barra de herramientas arriba, Browser a la izquierda, la Playlist detrás y el Channel Rack flotando encima](/assets/img/wiki/desktop.webp)

LYNX SHARE tiene una sola ventana principal. Una barra de herramientas recorre la parte superior. Debajo hay un escritorio que contiene cinco ventanas: el Browser, la Playlist, el Channel Rack, el Piano Roll y el Mixer. En un arranque nuevo, el Browser está a la izquierda, la Playlist ocupa el resto y el Channel Rack flota encima. El Piano Roll y el Mixer están ocultos hasta que los llamas.

## Orientarse

1. Apunta con el ratón a cualquier control. La barra de ayuda bajo los menús dice qué hace y qué botones del ratón y teclas acepta.
2. Pulsa `F6` para ocultar el Channel Rack, y `F6` otra vez para mostrarlo.
3. Pulsa `F9` para mostrar el Mixer y luego `F7` para el Piano Roll.
4. Arrastra una ventana por su franja de título para moverla. Haz doble clic en el título para maximizarla, y otra vez para restaurarla.
5. Elige `Ver › Restablecer diseño` para devolverlo todo a su sitio.

## La barra de herramientas

![La barra de herramientas: menús y barra de ayuda, transporte, controles del master, opciones de grabación, ajuste y patrón, botones de ventanas, botones de archivo, indicador de acordes, medidores y monitor de salida](/assets/img/wiki/toolbar-1920.webp)

De izquierda a derecha, con el diseño por defecto:

| Grupo | Qué contiene |
|---|---|
| Menú y barra de ayuda | Los menús `Archivo`, `Editar`, `Añadir`, `Patrones`, `Ver`, `Opciones`, `Herram.` y `Ayuda`, con la barra de ayuda debajo. Siempre primero |
| Transporte | Los botones de modo `Patrón` y `Canción`, Reproducir, Detener y Grabar, el tempo en BPM y la posición como compás, tiempo y paso |
| Volumen y tono del master | El control de volumen `MASTER` y el control `TONO`, en cents |
| Opciones de grabación | Metrónomo, cuenta previa, grabación en bucle, edición por pasos y los demás interruptores de grabación |
| Ajuste y patrón | El ajuste principal (`Línea (sigue el zoom)` por defecto), el patrón actual, un botón para un patrón nuevo y el menú de patrones |
| Atajos de ventanas | Un botón por ventana, y el selector de plugins |
| Archivo y deshacer | Nuevo, Abrir, Guardar, Exportar, Deshacer y Rehacer |
| Detección de acordes | Nombra el acorde que se toca en un dispositivo MIDI o en el teclado de escritura |
| CPU, memoria y polifonía | Carga de CPU del audio, memoria en uso y número de voces |
| Monitor de salida | Un osciloscopio de la salida. Un clic recorre sus otros modos |

También existe un grupo llamado `Multivínculo a controladores`, oculto por defecto.

`Opciones › Barra de herramientas` cambia la barra de herramientas: `Editar barra de herramientas (arrastra los paneles para moverlos, añadirlos o quitarlos)`, `Separadores`, `Ocultar los paneles de la barra de herramientas`, `Presets`, un interruptor para cada grupo, y `Iconos` (pequeño, mediano o grande).

## Transporte

| Control | Tecla | Qué hace |
|---|---|---|
| Modo Patrón | `L` | Reproduce el patrón actual en bucle. Haz clic derecho en el botón para mostrar el Channel Rack |
| Modo Canción | `L` | Reproduce el arreglo de la Playlist. Haz clic derecho en el botón para mostrar la Playlist |
| Reproducir | `Space` | Reproduce. Si se pulsa otra vez, se detiene y vuelve al punto de partida. `Ctrl+Space` pausa |
| Detener | | Se detiene y vuelve al inicio. El doble clic envía además all notes off |
| Grabar | `R` | Activa o desactiva la grabación. Consulta [Grabación](/wiki/recording/) |

## La barra de ayuda

En reposo, la barra de ayuda dice `Point at anything for help`, con `F1` para la hoja de atajos, `F5` a `F9` para las ventanas y `Space` para reproducir y detener. Sobre un control, nombra el control y lista lo que hacen ahí arrastrar, `Ctrl`, `Alt` y el clic derecho. Las tareas largas, como el escaneo de plugins en segundo plano, informan de su progreso en ella.

## Las cinco ventanas

| Ventana | Tecla | Para qué sirve |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Arreglar clips de patrón, de audio y de automatización para formar una canción |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Una fila por instrumento, con los pasos del patrón actual |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Las notas de un canal en el patrón actual |
| [Mixer](/wiki/mixer/) | `F9` | Inserts con faders, efectos y enrutado |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, presets, el contenido del proyecto y la base de plugins |

Cada tecla muestra la ventana y le da el foco, o la oculta si ya lo tiene. `F8` solo abre el selector de plugins, que no es una de las cinco. Los botones de ventana de la barra de herramientas hacen lo mismo que las teclas. Con el clic derecho en uno se muestra esa ventana maximizada. `Enter` maximiza o restaura la Playlist.

## Cómo se mueven y se acoplan las ventanas

Cada ventana tiene una franja de título. A su izquierda hay un botón de menú con las opciones de la propia ventana, luego `Separado (ventana propia)`, `Maximizado` y `Cerrar`. A su derecha están los botones de maximizar y cerrar.

- Arrastra la franja de título para mover una ventana. Se ajusta a las demás ventanas y a los bordes. Mantén `Alt` para mover sin ajuste.
- Arrastra un borde para cambiar el tamaño.
- `Separado (ventana propia)` convierte la ventana en una ventana del sistema independiente, para un segundo monitor. Una ventana separada tiene un botón `Acoplar` que la devuelve. `Ver › Paneles separados (ventana propia)` lista las cinco.
- `Ver › Recuperar paneles fuera de la pantalla` recupera una ventana que quedó fuera del escritorio.
- `Ver › Guardar diseño como...` guarda el diseño con un nombre, y `Ver › Cargar diseño` recupera uno.

El Browser es el fondo del escritorio: se queda acoplado al lado y las demás ventanas siempre quedan por encima.

## Conviene saber

- Hasta que muevas, cambies de tamaño o separes algo, las ventanas recuperan sus posiciones por defecto cada vez que la ventana principal cambia de tamaño. Después, tu diseño se recuerda y se escala con la ventana.
- `F12` cierra las ventanas de plugins, de ajustes de canal y de herramientas. No cierra las cinco ventanas principales.
- Mientras el piano del teclado de escritura está activado (`Ctrl+T`), las teclas de letras tocan notas y la mayoría de los atajos de una sola letra quedan en pausa.
- `Ver › Estilo de diseño` cambia entre el flujo por patrones (el predeterminado) y el flujo por lanzamiento de clips, que añade una cuadrícula de clips junto a la Playlist.

## Relacionado

- [Tu primera canción](/wiki/first-song/)
- [Disposiciones](/wiki/layouts/)
- [Atajos](/wiki/shortcuts/)
- [Configuración](/wiki/settings/)
