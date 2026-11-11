---
source: 5291e1e4ad
---
![El Piano Roll](/assets/img/pianoroll.webp)

El Piano Roll muestra las notas de un canal en el patrón actual, con un teclado a la izquierda y un carril para valores por nota abajo. Pulsa `F7` para mostrarlo u ocultarlo, o elige `Piano Roll` en el menú de un canal en el [Channel Rack](/wiki/channel-rack/).

La caja de canal de la barra de herramientas elige qué canal editas. Los canales que tienen notas en este patrón están marcados. La rueda sobre la caja recorre los canales con notas, `Ctrl`+rueda recorre todos.

## Escribir unas notas

1. En el Channel Rack, haz clic derecho en el nombre de un canal y elige `Piano Roll`.
2. Pulsa `P` para el Lápiz.
3. Haz clic en la cuadrícula para añadir una nota. Mantén el botón pulsado y arrastra para colocarla.
4. Arrastra el final de la nota para alargarla o acortarla.
5. Haz clic derecho en una nota para eliminarla.
6. Pulsa `Space` para oír el patrón.

## Herramientas

![La barra de herramientas del Piano Roll con la caja de canal, las herramientas y la caja de ajuste](/assets/img/wiki/pianoroll-tools.webp)

Cada herramienta tiene una tecla de letra. Las letras funcionan mientras el piano del teclado del ordenador (`Ctrl+T`) está desactivado.

| Herramienta | Tecla | Qué hace |
|---|---|---|
| Lápiz | `P` | Un clic añade una nota, arrastrar la coloca |
| Pincel | `B` | Arrastrar pinta una tanda de notas. Con `Shift`, una a una |
| Pincel de batería | `N` | Un golpe por celda de la cuadrícula en la tecla bajo el ratón |
| Borrador | `D` | Clic o arrastrar elimina |
| Silenciar | `T` | Clic o arrastrar silencia o reactiva notas |
| Cuchilla | `C` | Arrastra una línea para cortar a través de las notas; un clic corta en ese punto |
| Lazo | `E` | Arrastrar dibuja un cuadro de selección. `Shift` añade |
| Zoom | `Z` | Un clic acerca, arrastrar un rango hace zoom a él, clic derecho aleja |
| Escuchar | `Y` | Clic o arrastrar reproduce las notas bajo el cursor |

## Trabajar con notas

- Arrastra una nota para moverla. `Shift`+arrastrar hace una copia.
- Arrastra el final de una nota para cambiar su tamaño. También se puede arrastrar el principio, con el final en su sitio.
- El clic derecho elimina una nota, con cualquier herramienta.
- Haz doble clic en una nota para abrir sus propiedades.
- `Alt`+rueda sobre una nota cambia su velocidad.
- `Ctrl`+arrastrar selecciona. `Ctrl`+clic añade una nota a la selección o la quita.
- Mantén `Alt` mientras arrastras para ignorar el ajuste. `Backspace` desactiva y activa el ajuste.
- Con varias notas seleccionadas, el tirador de estiramiento las estira o las comprime en el tiempo.

## Teclado y regla

Haz clic en una tecla del teclado para oírla. Cuanto más a la derecha de la tecla, más fuerte. `Ctrl`+clic en una tecla selecciona sus notas. Arrastrar con el botón central en el teclado hace las teclas más altas o más bajas.

Haz clic en la regla para fijar la posición. `Ctrl`+arrastrar, arrastrar con el botón derecho o doble clic y arrastrar en la regla selecciona las notas de un rango de tiempo. `Ctrl`+rueda hace zoom y `Shift`+rueda desplaza.

## Barra de herramientas

| Ajuste | Qué hace |
|---|---|
| Ajuste | "Main" sigue el ajuste de la barra de herramientas principal, o elige uno para esta ventana. |
| Acorde | Al hacer clic se crea el acorde elegido, con la nota pulsada como fundamental. `Shift+N` vuelve a las notas sueltas. |
| Escala | Resalta las notas de una escala. Un botón aparte ajusta a la escala elegida las notas nuevas y movidas, y el MIDI entrante. |
| Slide (`S`) | Las notas nuevas son notas slide. La nota que está sonando se desliza hacia ellas (glide 808). |
| Notas fantasma (`Alt+V`) | Muestra las notas de los otros canales detrás de las tuyas. |
| Color de nota | Las notas nuevas reciben este color. Hay 16. |
| Seguir | La vista se desplaza con el cursor de reproducción mientras suena el patrón. |
| Caja de carril | Elige qué edita el carril de abajo: Velocidad, Pan o Afinación fina. |

## Teclas

| Comando | Tecla | Qué hace |
|---|---|---|
| Seleccionar todo | `Ctrl+A` | Selecciona todas las notas |
| Deseleccionar | `Ctrl+D` o `Esc` | Borra la selección |
| Copiar, cortar, pegar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, corta o pega notas |
| Duplicar | `Ctrl+B` | Duplica la selección |
| Eliminar | `Del` | Elimina la selección |
| Transponer | `Up` / `Down` | Mueve la selección un semitono |
| Transponer una octava | `Ctrl+Up` / `Ctrl+Down` | Mueve la selección una octava |
| Empujar | `Left` / `Right` | Mueve la selección un paso de la cuadrícula |
| Cuantización rápida | `Ctrl+Q` | Cuantiza inicios y duraciones |
| Cuantizar inicios | `Shift+Q` | Cuantiza solo los inicios |
| Trocear | `Ctrl+U` | Trocea las notas según la cuadrícula |
| Unir | `Ctrl+G` | Une notas que se tocan |
| Legato | `Ctrl+L` | Hace que cada nota dure hasta la siguiente |
| Silenciar, reactivar | `Alt+M` / `Shift+Alt+M` | Silencia o reactiva la selección |
| Importar MIDI | `Ctrl+M` | Importa un archivo MIDI a este canal |
| Exportar MIDI | `Ctrl+Shift+M` | Exporta el patrón como archivo MIDI |
| Zoom | `PageUp` / `PageDown` | Acerca o aleja |

Las herramientas de notas (Arpegiar, Rasgueo, Invertir, Aleatorizar, Cuantizar, Trocear y otras) abren diálogos. Sus teclas están en la página [Atajos](/wiki/shortcuts/).

## Conviene saber

- El Piano Roll muestra un canal en un patrón. Si cambias el patrón actual, muestra las notas de ese patrón.
- Las letras de herramienta no hacen nada mientras el piano del teclado del ordenador (`Ctrl+T`) está activado.
- El clic derecho elimina con cualquier herramienta, así que no hace falta cambiar al Borrador para una sola nota.
- Aquí `S` es el interruptor Slide. En la Playlist la misma letra es la herramienta Slip.
- Las notas que no son pasos simples (otras teclas, notas más largas, slides) hacen que la fila del canal en el Channel Rack muestre una miniatura en lugar de botones de paso.

## Relacionado

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Atajos](/wiki/shortcuts/)
- [Formatos de archivo](/wiki/file-formats/)
