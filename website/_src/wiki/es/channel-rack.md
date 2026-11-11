---
source: f08a3da66d
---
![El Channel Rack con filas de pasos](/assets/img/rack.webp)

El Channel Rack lista todos los instrumentos (canales) del proyecto, uno por fila, y muestra los pasos del patrón actual. Pulsa `F6` para mostrarlo u ocultarlo. Con el clic derecho en el botón Patrón del transporte también aparece.

## Hacer un patrón de batería

1. Pulsa `F6` para mostrar el Channel Rack.
2. Haz clic en el cuadro `+ añadir canal` bajo la última fila y elige un instrumento del menú. También puedes hacer doble clic en un sample del [Browser](/wiki/browser/) para crear un canal de sampler nuevo.
3. Haz clic en los pasos de la fila del canal para activarlos. Arrastra para dibujar una tanda de pasos.
4. Haz clic derecho en un paso para borrarlo.
5. Pulsa `Space` para reproducir. En el modo Patrón, el patrón se repite en bucle.
6. Arrastra el control de volumen de la fila para fijar el nivel.

## Programar pasos

- Haz clic en un paso para activarlo. Si arrastras, dibujas una tanda de pasos. Si el arrastre empieza en un paso encendido, borra en lugar de dibujar.
- El clic derecho o el arrastre con el botón derecho borra.
- Hacer clic después del final del patrón añade un paso y alarga el patrón.
- Una fila cuyas notas no son pasos simples (otras teclas, notas más largas, slides) muestra una miniatura de las notas. Al hacer clic en ella se abren en el [Piano Roll](/wiki/piano-roll/).
- Puedes soltar archivos de audio en el rack para crear canales.

El menú del canal (clic derecho en el nombre del canal) tiene `Rellenar cada 2 pasos`, `Rellenar cada 4 pasos`, `Rellenar cada 8 pasos`, `Relleno avanzado`, y `Rotar a la izquierda` y `Rotar a la derecha`.

## El carril de gráfico

![El carril de gráfico abierto bajo el canal Kick, con una barra de velocidad por paso](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` muestra el carril de gráfico bajo el canal seleccionado. Los botones de su izquierda eligen qué edita: Velocidad, Pan, Liberación, Afinación fina, Mod X o Mod Y. Al arrastrar se fija un valor por paso, al arrastrar con el botón derecho se dibuja una rampa, y `Alt`+clic restablece un paso.

## Las partes de una fila

| Parte | Qué hace |
|---|---|
| Luz de silencio | El clic silencia. `Ctrl`+clic o clic derecho pone en solo. |
| Controles de pan y volumen | Arrastra o usa la rueda. |
| Cuadro de ruta | Arrastrar o la rueda elige el insert del Mixer. El doble clic muestra ese insert en el Mixer. El clic derecho toma el siguiente insert libre. |
| Botón de nombre | El clic abre o cierra el editor del canal. `Alt`+clic mantiene abiertos los demás editores. `Ctrl`+clic preescucha. Arrastrar arriba o abajo reordena. El clic derecho abre el menú del canal. |
| Selector | El clic selecciona un solo canal. `Ctrl`+clic o clic derecho añade o quita. `Shift`+clic selecciona un rango. El doble clic selecciona todos. |

## Patrones

El rack siempre edita el patrón actual. El selector de patrón del rack muestra su nombre: haz clic en el nombre para ver la lista, o usa las flechas. `Shift`+rueda sobre el título del rack también cambia el patrón.

El cuadro de longitud del patrón del rack fija la longitud en compases (arrástralo o usa la rueda). Haz doble clic en él para una longitud automática según las notas.

El control de swing de la barra de herramientas principal se aplica a todos los canales. `Mezcla de swing` en el menú del canal fija cuánto lo sigue un canal.

| Comando | Tecla | Qué hace |
|---|---|---|
| Patrón siguiente | `Numpad +` | Convierte el patrón siguiente en el actual |
| Patrón anterior | `Numpad -` | Convierte el patrón anterior en el actual |
| Siguiente patrón vacío | `F4` | Va al siguiente patrón vacío y pide un nombre |
| Renombrar y colorear | `F2` | Renombra y colorea el patrón actual |

## Tocar notas con el teclado del PC

`Ctrl+T` convierte el teclado de escritura en un piano: `Z` a `M` y `Q` a `P` tocan los canales seleccionados. Mientras está activado, los atajos de una sola letra de los editores quedan en pausa.

## Teclas

Funcionan mientras el Channel Rack tiene el foco.

| Comando | Tecla | Qué hace |
|---|---|---|
| Seleccionar canal | `Up` / `Down` | Selecciona el canal de arriba o de abajo |
| Ampliar la selección | `Shift+Up` / `Shift+Down` | Añade a la selección el canal de arriba o de abajo |
| Mover canales | `Alt+Up` / `Alt+Down` | Mueve los canales seleccionados hacia arriba o abajo |
| Seleccionar todo | `Ctrl+A` | Selecciona todos los canales |
| Cortar, copiar, pegar | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Corta, copia o pega los pasos de los canales seleccionados |
| Rotar pasos | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rota los pasos de los canales seleccionados |
| Clonar | `Alt+C` | Clona los canales seleccionados |
| Eliminar | `Alt+Del` | Elimina los canales seleccionados |
| Agrupar | `Alt+G` | Pone los canales seleccionados en un grupo de filtro nuevo |
| Enrutar al Mixer | `Ctrl+L` | Envía los canales seleccionados a inserts libres del Mixer |
| Silenciar | `1` a `0` | Silencia o activa los canales 1 a 10 |
| Solo | `Ctrl+1` a `Ctrl+0` | Pone en solo los canales 1 a 10 |
| Carril de gráfico | `Ctrl+K` | Muestra u oculta el carril de gráfico |

## Otras cosas del menú del canal

`Piano Roll`, `Renombrar y colorear`, `Reemplazar` (cambiar el instrumento), `Clonar`, `Delete`, `Arpegiador`, `Punto de bucle`, `Grabar MIDI en`, `Ordenar canales`, `Seleccionar canales sin usar` y `Configuración del canal`.

## Conviene saber

- Solo el cuadro `+ añadir canal` abre el menú de añadir.
- El rack no tiene un patrón propio. Cambiar el patrón actual en cualquier sitio cambia lo que muestra el rack.
- Una fila que muestra una miniatura de notas no se puede editar como pasos. Usa el Piano Roll para ella.
- Las teclas numéricas solo llegan a los diez primeros canales.
- Las teclas de la tabla necesitan que el rack tenga el foco. Haz clic primero en el rack.

## Relacionado

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Atajos](/wiki/shortcuts/)
