---
source: d2bebc77b1
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
| `Cualquier canal MIDI` | Escucha todos los canales MIDI, o solo uno. `Controlador MPE` está al final de esta lista |
| `Toca los canales seleccionados` | Las notas van a los canales seleccionados en el Channel Rack, o siempre a un canal fijo |
| `Velocidad` | `Tal como se toca`, `Suave (tocar ligero suena más fuerte)`, `Duro (requiere tocar más fuerte)` o `Fija (todas las notas a velocidad máxima)` |
| `Canal del modo performance` | Las notas de este canal MIDI lanzan clips de la Playlist mientras el modo Performance está activado |
| `Salto a marcador de la canción` | Las notas de este canal MIDI llevan la canción a un marcador en lugar de sonar |
| `Tipo de controlador` | `Detectar automáticamente`, `Controlador genérico` o un perfil de controlador |
| `El pedal controla el final de las notas` | Activado por defecto. Un pedal de sustain (CC 64) o de sostenuto (CC 66) mantiene las notas. Desactivado, el pedal llega a los plugins como un controlador |

## Vincular un mando a un control

1. Haz clic derecho en el mando o fader en LYNX SHARE.
2. Elige `Vincular a un controlador MIDI (mueve un control del controlador)`.
3. Mueve el mando o fader de tu hardware. El vínculo queda hecho.

El vínculo se guarda con el proyecto. Para quitarlo, haz clic derecho de nuevo en el control y elige `Desvincular controlador`. El elemento del menú muestra el número de CC en uso.

| Comando | Tecla | Qué hace |
|---|---|---|
| `Vincular a controlador...` | ninguna | Abre la ventana `Configuración de control remoto` del control, donde ves y fijas su vínculo a mano (abajo) |
| `Reemplazar vínculo global...` | ninguna | Crea un vínculo que funciona en todos los proyectos. Se guarda en la configuración, no en el proyecto |
| `Sobrescribir vínculo global... (para todos los proyectos: mueve un control del controlador)` | ninguna | Crea un vínculo global en un solo paso: mueve a continuación el control de hardware. El elemento sin los paréntesis abre en su lugar la ventana `Configuración de vínculo global` |
| `Quitar vínculo global` | ninguna | Lo quita de nuevo |
| `Multivínculo a controladores` | `Ctrl+J` | Mientras está activado, los controles que mueves se vinculan, en orden, a los controles de hardware que muevas a continuación. Está en el menú `Opciones` |
| `Pickup (modo de recogida)` | ninguna | En la página MIDI. Un controlador mueve un parámetro solo después de alcanzar su valor actual, así que nada da saltos |

La página MIDI lista los vínculos bajo `Vínculos (proyecto, globales)`. Un dispositivo cuyo `Tipo de controlador` es `Controlador genérico` hace solo lo que vinculas a mano.

A los parámetros de un plugin de terceros se llega con `Automatizar parámetro del plugin` en el menú del canal o en el de la ranura del efecto. Cada parámetro tiene allí los mismos elementos de vínculo. Mira [Automatización](/wiki/automation/).

## La ventana Remote control settings

`Vincular a controlador...` abre esta ventana, como en FL Studio. `Configuración de vínculo global` (desde `Reemplazar vínculo global...`) tiene los mismos campos salvo la lista de vínculos, `Omni`, el controlador interno y `Quitar conflictos`. Un doble clic en un vínculo de la lista de la página MIDI también la abre.

| Ajuste | Qué hace |
|---|---|
| `Puerto`, `Canal`, `Ctrl` | El número del puerto de entrada MIDI, el canal MIDI y el número del controlador. Haz clic derecho en `Ctrl` para ver los controladores estándar por nombre. `Nota C0` en esa lista crea un vínculo de nota: sube el número desde ahí para otra tecla |
| `Detección automática` | Activado por defecto. Mueve un control de tu dispositivo y los números se rellenan |
| `Omni` | El vínculo sigue al canal seleccionado o, para un efecto en una ranura del mixer, a la pista del mixer seleccionada |
| `Modo` | Cómo actúa un vínculo de nota o de botón. `Predeterminado` y `Retención` siguen la tecla: su velocidad mientras está pulsada, cero al soltarla. `Retener` alterna entre los dos extremos con cada pulsación. `Inc` sube el valor un paso con cada pulsación. `Quedarse` fija la velocidad y la mantiene tras soltar |
| Controlador interno | Vincula el control a uno de los controladores internos del proyecto en su lugar. Solo se muestra cuando el proyecto tiene uno. `Grabar automatización` permite grabar como automatización los movimientos de ese controlador |
| `Fórmula de mapeo` | Cómo se asigna la entrada al control, con la variable `Entrada`. `Presets` tiene la lista de fórmulas de FL. Pulsa `Enter` para aplicar una fórmula que escribiste. La curva de al lado muestra el resultado |
| `Suavizado` | Suaviza los movimientos del control. `Tiempo` fija cuánto: 1 polo para valores negativos, 3 polos para positivos |
| `Quitar conflictos` | Activado por defecto. Quita otros vínculos que usan el mismo controlador al hacer clic en `Aceptar`. Se pone rojo mientras hay un vínculo así |
| `Reset`, `Omitir`, `Aceptar` | `Aceptar` guarda el vínculo (un paso de deshacer). Con varios controles en cola por multivínculo, `Omitir` pasa al siguiente |

## Perfiles de controlador

Un perfil de controlador configura los botones de transporte, faders, mandos, pads, luces y pantalla de un dispositivo, sin nada que vincular.

1. Abre `Opciones › Configuración MIDI...` (`F10`) y marca el dispositivo en `Dispositivos de entrada`.
2. Deja su `Tipo de controlador` en `Detectar automáticamente`. El nombre entre paréntesis es el perfil encontrado a partir del nombre de puerto del dispositivo.
3. Si muestra `Controlador genérico`, elige tú mismo el perfil de la lista.
4. Lee la línea junto al cuadro. `Feedback to` nombra el puerto de salida que recibe las luces, los faders motorizados y el texto de la pantalla.

| Perfil | Qué asigna |
|---|---|
| `Mackie Control Universal` | Faders a los volúmenes de los inserts y al Master. V-Pots al pan, a los parámetros del plugin del canal seleccionado o a los volúmenes del Channel Rack. Armar, solo, silenciar y seleccionar por tira, bancos de faders, reproducir, detener, grabar, rebobinar, avanzar, modo Pattern o Song, metrónomo, deshacer y la rueda jog. La pantalla muestra nombres y valores |
| `Mackie Control Extender (XT)` | Lo mismo para 8 tiras más. Las unidades comparten un banco, en el orden de la lista `Dispositivos de entrada` |
| `Novation Launchkey MK3 (DAW mode)` | Potenciómetros al pan, al volumen o a parámetros de plugin, faders al volumen, los botones de fader seleccionan un insert. Botones de banco, reproducir, detener, grabar, bucle, metrónomo y deshacer. Los pads de Session son los 16 pasos del canal seleccionado. Úsalo en el segundo puerto del teclado |
| `Akai APC Key 25 mk2` | Mandos al volumen, al pan o a parámetros de plugin. Las dos filas de pads superiores son los 16 pasos del canal seleccionado y las tres de abajo tocan notas. Los botones de pista seleccionan un insert. Detener, reproducir y grabar. Úsalo en el segundo puerto |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | El mapa de Mackie Control, en el puerto MCU/HUI del teclado |
| `Korg nanoKONTROL2 (Mackie Control mode)` | El mapa de Mackie Control. No se detecta: inicia la unidad en un modo Mackie Control y elige este tipo a mano |

Los mandos y faders que envían una posición usan soft takeover: solo mueven un control después de alcanzar su valor. Los controles que vinculaste a mano conservan sus vínculos. `Carpeta de perfiles de controlador` abre la carpeta para tus propios perfiles (`*.lynxctl`), y `Actualizar lista de dispositivos` los vuelve a leer.

Los perfiles integrados se escribieron a partir de la documentación de cada fabricante. Todavía no se han probado en el hardware.

## Controladores MPE

MPE está en una primera etapa. Un controlador MPE toca cada nota con su propio pitch bend, timbre (CC 74) y presión.

1. En la página MIDI, abre el cuadro del dispositivo que dice `Cualquier canal MIDI` y elige `Controlador MPE`.
2. En el cuadro que aparece junto a él, elige los canales miembro, de `Canal 2` a `Channels 2-16`.
3. Toca. Para grabar, consulta [Grabación](/wiki/recording/).

- Una nota grabada conserva un valor de cada uno: su bend como afinación fina, su timbre como Mod X y su presión más fuerte como Mod Y. No es una curva.
- De los instrumentos integrados, el Sampler y el sintetizador integrado siguen estos valores.
- Un plugin de terceros recibe MPE cuando `MPE` está activado en `MIDI` dentro de `Configuración del wrapper` de la ventana del plugin. Cada nota sale entonces por su propio canal MIDI, del 2 al 16.
- Todavía no se ha probado con un controlador MPE real.

## MIDI Map Mode y Key Map Mode

En el [flujo Clip-launch](/wiki/layouts/), los clips, las escenas y los botones se pueden asignar como en Ableton Live. Los botones `MIDI` y `Tonalidad` junto al selector `Ambos`, `Cuadrícula`, `Canción` activan los modos, igual que `Ctrl+M` y `Ctrl+K`.

1. Pulsa `Ctrl+M` para el MIDI Map Mode, o `Ctrl+K` para el Key Map Mode. Cada elemento que se puede asignar se tiñe, de azul para MIDI y de naranja para teclas.
2. Haz clic en un elemento: una ranura de clip, una escena, el botón de parada de una pista, `Detener todos los clips`, reproducir, detener, grabar, `Grabación de sesión`, el metrónomo, o un mando del mixer de Session o de la Device View.
3. Mueve un control o pulsa una tecla de tu dispositivo MIDI, o pulsa una tecla del ordenador en el Key Map Mode. El elemento muestra su asignación, por ejemplo `1/19` (canal y controlador).
4. Las asignaciones se listan en lugar del Browser: Channel, Note/Control o Key, Path, Name, Min y Max. Arrastra Min o Max, o haz doble clic para escribir un valor. Haz clic derecho en una fila para ver `Invertir rango` y `Delete`.
5. Pulsa la misma tecla de nuevo para terminar el modo.

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
- Una nota mantenida por un pedal se graba hasta el momento en que el pedal se suelta.
- Un clip de automatización no recupera un control mientras lo mueves desde un controlador. El control vuelve a seguir su automatización la próxima vez que empiece la reproducción.
- Los vínculos de nota creados en `Configuración de control remoto` no se escriben en una exportación .flp.
- `Ctrl+M` y `Ctrl+K` inician los modos de asignación solo en el flujo Clip-launch. El Map Mode aún no tiene asignación de macros ni rangos de notas.

## Relacionado

- [Grabación](/wiki/recording/)
- [Automatización](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Configuración](/wiki/settings/)
- [Atajos](/wiki/shortcuts/)
