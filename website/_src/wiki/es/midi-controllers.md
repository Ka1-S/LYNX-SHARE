---
source: 3b54a39c97
---
![La página MIDI de la configuración: dispositivos de entrada, dispositivos de salida con sincronización maestra y sincronización externa](/assets/img/wiki/settings-midi.webp)

LYNX SHARE recibe notas y movimientos de controladores de cualquier dispositivo de entrada MIDI. Los dispositivos se configuran en la página MIDI de la configuración: `Opciones › Configuración MIDI...`, o `F10`. Sin hardware, el teclado del ordenador y el teclado táctil también tocan notas.

## Conectar un teclado

1. Enchufa el teclado y abre `Opciones › Configuración MIDI...` (`F10`).
2. Búscalo bajo `Dispositivos de entrada` y márcalo. Si no aparece, haz clic en `Actualizar lista de dispositivos`.
3. Deja las dos cajas en `Cualquier canal MIDI` y `Toca los canales seleccionados`.
4. Cierra la configuración y selecciona un canal en el [Channel Rack](/wiki/channel-rack/).
5. Toca. Suena el canal seleccionado.

## Dispositivos de entrada

| Ajuste | Qué hace |
|---|---|
| Casilla | Activa o desactiva el dispositivo |
| `Cualquier canal MIDI` | Escucha todos los canales MIDI, o solo uno |
| `Toca los canales seleccionados` | Las notas van a los canales seleccionados en el Channel Rack, o siempre a un canal fijo |
| `Velocidad` | `Tal como se toca`, `Suave (tocar ligero suena más fuerte)`, `Duro (requiere tocar más fuerte)` o `Fija (todas las notas a velocidad máxima)` |
| `Canal del modo performance` | Las notas de este canal MIDI lanzan clips de la Playlist mientras el modo Performance está activado |
| `Salto a marcador de la canción` | Las notas de este canal MIDI llevan la canción a un marcador en lugar de sonar |

## Vincular un mando a un control

1. Haz clic derecho en el mando o fader en LYNX SHARE.
2. Elige `Vincular a un controlador MIDI (mueve un control del controlador)`.
3. Mueve el mando o fader de tu hardware. El vínculo queda hecho.

El vínculo se guarda con el proyecto. Para quitarlo, haz clic derecho de nuevo en el control y elige `Desvincular controlador`. El elemento del menú muestra el número de CC en uso.

| Comando | Tecla | Qué hace |
|---|---|---|
| `Override global link...` | ninguna | Crea un vínculo que funciona en todos los proyectos. Se guarda en la configuración, no en el proyecto |
| `Quitar vínculo global` | ninguna | Lo quita de nuevo |
| `Multivínculo a controladores` | `Ctrl+J` | Mientras está activado, los controles que mueves se vinculan, en orden, a los controles de hardware que muevas a continuación. Está en el menú `Opciones` |
| `Pickup (modo de recogida)` | ninguna | En la página MIDI. Un controlador mueve un parámetro solo después de alcanzar su valor actual, así que nada da saltos |

La página MIDI lista los vínculos bajo `Vínculos (proyecto, globales)`. `Tipo de controlador` en la misma página dice `Controlador genérico`.

A los parámetros de un plugin de terceros se llega con `Automatizar parámetro del plugin` en el menú del canal o en el de la ranura del efecto. Cada parámetro tiene allí los mismos elementos de vínculo. Mira [Automatización](/wiki/automation/).

## El teclado del ordenador

`Ctrl+T`, o `Opciones › Teclado de escritura a piano`, convierte el teclado del ordenador en un piano. Las filas de `Z` a `M` y de `Q` a `P` tocan los canales seleccionados.

- Mientras está activado, las teclas de letras y números son notas. Las teclas de herramienta de una sola letra del Piano Roll y la Playlist funcionan solo cuando está desactivado.
- Siempre toca con la velocidad del Piano Roll. El ajuste `Velocidad` de la página MIDI es para dispositivos MIDI.
- El mismo interruptor está en `Opciones › Configuración general...`.

## El teclado táctil

![El teclado táctil: botones de octava, tamaño de tecla, modo de velocidad y una fila de teclas de piano desde C4](/assets/img/wiki/touch-keyboard.webp)

`Ver › Teclado táctil` (`Alt+F7`) abre un piano que tocas con el ratón o una pantalla táctil. Toca el canal seleccionado en el Channel Rack y muestra su nombre a la derecha.

| Ajuste | Qué hace |
|---|---|
| `-` y `+` | Una octava abajo o arriba |
| Tamaño de tecla | `Teclas pequeñas`, `Teclas medianas` o `Teclas grandes` |
| `Velocidad: según dónde toques` | Cerca de la parte superior de la tecla suena suave, cerca de la inferior suena fuerte |
| `Velocidad: fija` | Todas las notas con la velocidad fijada con el deslizador |
| `Nombres de notas` | Muestra los nombres en las teclas |

## Sincronización MIDI

`Dispositivos de salida` en la página MIDI envía el transporte a hardware y a otro software. Marca `Enviar sincronización maestra` en un dispositivo y elige `MIDI clock` (con Start, Continue, Stop y Song Position Pointer) o MIDI Time Code. `Desplazamiento (ms)` envía la sincronización más tarde con un valor positivo y más pronto con uno negativo. `Enviar all notes off` envía ese mensaje al dispositivo al iniciar y después de una exportación.

`Sincronizar con clock externo` funciona al revés: LYNX SHARE sigue el reloj MIDI de la entrada que elijas, con su tempo, Start, Continue, Stop y Song Position Pointer. `Desplazamiento de la sincronización externa (milisegundos)` hace sonar LYNX SHARE más tarde con un valor positivo y más pronto con uno negativo. `Estado` muestra si hay un reloj en marcha.

## Conviene saber

- Al elegir una entrada para `Sincronizar con clock externo`, ese dispositivo se activa bajo `Dispositivos de entrada`.
- Un tempo movido por un controlador MIDI vinculado no cambia el tamaño de los clips de audio. Escribe o fija el tempo final.
- Para grabar lo que tocas, mira [Grabación](/wiki/recording/). Las notas entrantes pueden ajustarse a una escala en el [Piano Roll](/wiki/piano-roll/).
- Los CC MIDI, el aftertouch de canal y el pitch bend enviados a un plugin pueden automatizarse con `Automatizar controlador MIDI (CC, aftertouch, pitch bend)`.

## Relacionado

- [Grabación](/wiki/recording/)
- [Automatización](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Configuración](/wiki/settings/)
- [Atajos](/wiki/shortcuts/)
