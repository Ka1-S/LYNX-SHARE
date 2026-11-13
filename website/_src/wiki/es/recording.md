---
source: 094cbefd18
---
![La ventana Grabación pregunta qué grabar: audio, notas y automatización, todo, o audio en el editor de audio](/assets/img/wiki/record-dialog.webp)

El botón de grabar del transporte, o la tecla `R`, activa la grabación. La primera vez, una ventana pregunta qué quieres grabar. El audio va a la [Playlist](/wiki/playlist/) como clips de audio. Las notas van al patrón actual. Los controles que mueves se convierten en automatización.

## Grabar un micrófono o un instrumento

1. Abre `Opciones › Configuración de audio...` y elige un dispositivo de entrada.
2. Abre el [Mixer](/wiki/mixer/) (`F9`) y selecciona un insert vacío.
3. Haz clic en la caja de entrada sobre las ranuras de efecto del insert y elige una entrada. Los pares estéreo van primero, los canales sueltos están marcados con `(mono)`.
4. Haz clic en el botón de armado de grabación junto a la caja de entrada.
5. Pulsa `R` y elige `Audio, en la Playlist como clip de audio`.
6. Pulsa reproducir si la reproducción no empezó sola, y toca.
7. Pulsa detener. La toma es un clip de audio en la Playlist.

`Shift+R` hace los pasos 4 a 6 con una sola tecla: arma el insert seleccionado, graba solo audio e inicia la reproducción. Púlsala de nuevo para detener.

Cuando eliges una entrada para un insert que graba más que esa entrada (también se graban otros canales que le envían), LYNX SHARE pregunta, como hace FL Studio, si quieres cambiar a `Solo entrada externa`. Responde `Sí` para grabar solo la entrada en ese insert.

## Grabación de audio con un clic

`Herram. › Grabación de audio con un clic` inicia una toma en un solo paso, como en FL Studio. Actúa sobre el insert del mixer seleccionado. Con el Master o nada seleccionado, toma el primer insert con una entrada, o si no un insert libre.

| Comando | Qué hace |
|---|---|
| `En el editor/grabador de audio Edison` | Pone Edison en el insert, fijado para grabar cuando empieza la reproducción, e inicia la reproducción. Mira [Mixer](/wiki/mixer/) |
| `En la playlist como clip de audio` | Cambia al modo Song, arma el insert, pide su entrada si no la tiene, e inicia la grabación y la reproducción. Los inserts que ya están armados graban tal como están |

## Qué ofrece la ventana Grabación

| Opción | Qué hace |
|---|---|
| `Audio, en la Playlist como clip de audio` | Arma el insert del mixer seleccionado y le pide una entrada si no tiene |
| `Notas y automatización` | Graba notas de un teclado MIDI o del teclado del ordenador, y los controles que mueves |
| `Todo` | Notas, automatización y audio de los inserts armados |
| `Audio, en el editor de audio` | Graba la entrada en un editor de audio nuevo mientras suena la canción |

Marca `No volver a preguntar` para conservar tu elección. Después, haz clic derecho en el botón de grabar para ver el filtro de grabación: `Automatización`, `Notas`, `Audio`, `Clips`, y `Preguntar qué grabar al empezar a grabar` para recuperar la ventana.

## La caja de entrada de un insert

![El Mixer con el insert 3 armado y su caja de entrada puesta en las entradas 1 y 2](/assets/img/wiki/mixer-input.webp)

| Ajuste | Qué hace |
|---|---|
| `Fuente de entrada de audio` | La entrada del dispositivo que este insert reproduce y graba, o `(ninguno)` |
| `Grabar` | Dónde se toma la señal de la toma: `Solo entrada externa`, `Entrada externa y del mezclador`, `Post efectos`, `Post EQ`, `Post nivel y paneo` o `Post pista` |
| `Grabar también la entrada seca como segunda toma (LYNX)` | Conserva la entrada sin procesar junto a una toma procesada |
| `Monitorizar entrada externa` | `Desact.`, `Cuando está armado`, `Auto (cuando está armado, no durante la reproducción)` o `Act.` |
| `Protegido contra grabación (no se puede armar)` | Protege el insert de armarse por accidente |

## Grabar notas

Las notas vienen de un teclado MIDI, del teclado del ordenador (`Ctrl+T`) o del teclado táctil, y van a los canales seleccionados en el patrón actual. Mira [Controladores MIDI](/wiki/midi-controllers/).

- `Mezclar notas grabadas` (`Ctrl+B`) decide entre superponer y reemplazar. Activado, las notas nuevas se suman a las del patrón. Desactivado, se borran las notas antiguas de un canal grabado.
- `Cuantizar las notas grabadas al ajuste global` está en las opciones de grabación.
- `Edición por pasos` (`Ctrl+E`) funciona con la reproducción detenida: cada nota que tocas entra en el patrón un paso de ajuste tras otro.
- `Capturar MIDI` (`Ctrl+Shift+Alt+R`) pone en el patrón las notas que acabas de tocar, aunque la grabación estuviera desactivada.
- El pedal de sustain (CC 64) y el pedal de sostenuto (CC 66) mantienen las notas que tocas, y una nota grabada dura hasta que el pedal se suelta. Un canal Layer pasa el pedal a sus canales. Con `El pedal controla el final de las notas` desactivado en `Opciones › Configuración MIDI...`, el pedal llega a los plugins como un controlador.
- En el modo Song, las notas grabadas en un clip de patrón más largo que su patrón se quedan donde las tocaste, y el patrón crece. La toma no se pliega sobre los primeros compases del patrón. Con `Los clips de patrón estirados repiten su patrón (LYNX)` activado, solo las notas tocadas sobre compases que ya tenían notas entran en el bucle.

Los controles que mueves durante la grabación se escriben como clips de automatización en modo Canción y como eventos del patrón en modo Patrón. Mira [Automatización](/wiki/automation/).

## Opciones de grabación

Las opciones están en `Opciones › Grabación` y en el menú de clic derecho del botón de grabar.

| Comando | Tecla | Qué hace |
|---|---|---|
| `Cuenta previa` | `Ctrl+P` | Off, 1 compás, 2 compases o 4 compases de metrónomo antes de empezar a grabar. `Ctrl+P` alterna entre desactivado y la duración que elegiste en último lugar. La elección se conserva al cerrar el programa; una instalación nueva empieza con 1 compás, como FL Studio |
| `Metrónomo` | `Ctrl+M` | Clic durante la reproducción |
| `Esperar a la primera nota para empezar` | `Ctrl+I` | La grabación espera hasta que toques |
| `Grabación en bucle` | ninguna | La grabación se repite en bucle en lugar de seguir de largo |
| `Audio: punch in/out en la región del bucle` | ninguna | Reproduce toda la región del bucle y conserva solo el audio de dentro |
| `Audio: pre-roll` y `post-roll` | ninguna | Desactivado, 1, 2 o 4 compases reproducidos antes del punch-in y después del punch-out |
| `Grabar inicia la reproducción` | ninguna | Al activar la grabación también se pulsa reproducir |
| `Desarmar los inserts al detener la grabación` | ninguna | Los inserts armados se liberan tras cada toma |

`Baja latencia al grabar` está en el menú de clic derecho del botón de grabar y en el menú del insert del Mixer, en `Grabación en disco`. Está desactivado por defecto. Mientras un insert con entrada está armado, o su monitorización está en `Act.`, se omiten los efectos que añaden latencia en su camino al Master. Esto cambia solo lo que oyes: un insert conserva todos sus efectos cuando la toma se captura después de ellos, por ejemplo con `Post efectos`. Los efectos vuelven cuando el insert se desarma.

`Compensación de latencia` está en el mismo menú `Grabación en disco` y está activado por defecto, como en FL Studio. Mueve cada toma hacia atrás según el viaje de ida y vuelta del dispositivo de audio, el valor `Ajuste fino (muestras)` de la configuración de audio y el retardo de compensación de los plugins. Desactívalo para dejar las tomas donde se grabaron.

`Herram. › Cancelar grabación` descarta lo que escribió la pasada de grabación actual (sus notas, automatización y tomas de audio) y desactiva la grabación. La reproducción continúa y los inserts armados siguen armados.

## Grabar en ranuras de clip

En el [flujo Clip-launch](/wiki/layouts/), las tomas pueden ir a la cuadrícula de clips en lugar de a la Playlist, como en Ableton Live.

- Arma una pista con su botón de armado en el mixer de Session. Sus ranuras vacías muestran entonces un botón de grabación redondo en lugar del cuadrado de parada. Haz clic en una para grabar en esa ranura.
- `Grabación de sesión`, bajo la cuadrícula, graba las pistas armadas en la escena seleccionada. Mientras suenan sus clips, se añaden notas nuevas a ellos (overdub).
- `Duración de grabación fija`, en el menú de una ranura vacía, termina una grabación en una ranura tras 1 o más compases, y el clip sigue sonando. `Desact.` graba hasta que lo detengas.

## Grabación en bucle y tomas

Con `Grabación en bucle` activado, la grabación se repite sobre la región del bucle en modo Canción, o sobre el patrón en modo Patrón. Desactivado, la grabación sigue de largo.

Cada pasada de una grabación de audio en bucle cae en la siguiente pista libre de la Playlist, y las pistas forman un grupo. `Mezclar notas grabadas` decide qué suena al detener: desactivado silencia todas las pasadas, activado las reproduce todas. Haz clic derecho en una pista del grupo y elige `Crear comp con estas tomas` para montar una sola toma con las pasadas. `Tras grabar en bucle, solo suena la última pasada` en las opciones de grabación vuelve a una sola pista.

## Dónde se guardan las grabaciones

- Un proyecto guardado conserva sus tomas en `Audio/Recorded`, junto al archivo del proyecto.
- Un proyecto sin guardar graba en `LYNX/Recordings` dentro de tu carpeta Música.
- Las tomas son archivos WAV a la frecuencia de muestreo del dispositivo de audio, de 16 bits, 24 bits o 32 bits flotante. El formato (`Formato de archivo`) y los nombres de las tomas se fijan en `Grabación` dentro de `Opciones › Configuración de audio...`.
- `Abrir carpeta de grabaciones` está en el menú de clic derecho del botón de grabar. `Archivo › Eliminar grabaciones sin usar y guardar...` elimina las tomas que el proyecto ya no usa.

## Conviene saber

- Un insert armado sin entrada no graba nada. LYNX SHARE te lo dice y señala la caja de entrada.
- La grabación se niega a empezar cuando el disco tiene menos de 30 segundos de espacio, y avisa por debajo de 10 minutos.
- `Opciones › Grabar audio de entrada` graba los dos primeros canales de entrada en un insert nuevo cuando no hay nada armado.
- En macOS el sistema pide acceso al micrófono la primera vez que grabas.
- Si LYNX SHARE se cierra durante la grabación, el siguiente arranque ofrece poner en la Playlist las tomas recuperadas.

## Relacionado

- [Configuración de audio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automatización](/wiki/automation/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Solución de problemas](/wiki/troubleshooting/)
