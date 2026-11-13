---
source: b6e9ef0dd0
---
![El Mixer](/assets/img/mixer.webp)

Cada canal suena a través de un insert del Mixer. Cada insert tiene un fader, pan, silenciar y solo, un medidor de picos, diez ranuras de efecto con su propia cantidad dry/wet, y un EQ integrado. Todos los inserts acaban en el Master salvo que los enrutes a otro sitio. Pulsa `F9` para mostrar u ocultar el Mixer.

Un proyecto puede tener hasta 127 inserts. El Mixer tiene una franja para añadir uno nuevo. La caja de la barra de título del Mixer elige uno de ocho diseños de franja, de `Compacto` a `Extra grande`.

## Poner un efecto en un canal

1. En el [Channel Rack](/wiki/channel-rack/), selecciona el canal y pulsa `Ctrl+L`. El canal va a un insert libre, que toma su nombre y su color.
2. Pulsa `F9` para mostrar el Mixer y haz clic en la franja de ese insert.
3. En el panel de efectos, haz clic en una ranura vacía y elige un efecto.
4. Haz clic en el nombre de la ranura para mostrar u ocultar la ventana del efecto.
5. Haz clic en la luz de la ranura para puentear el efecto y comparar.

## Enviar un canal a un insert

En el Channel Rack, arrastra la caja de ruta del canal (o usa la rueda sobre ella) para elegir el número de insert. Con doble clic en la caja se muestra ese insert en el Mixer.

`Ctrl+L` en el Channel Rack funciona con varios canales seleccionados a la vez. Cada uno va a su propio insert libre.

Con el foco en el Mixer, `Ctrl+L` vincula los canales seleccionados al insert seleccionado, y `Ctrl+Shift+L` los vincula a inserts consecutivos a partir de él.

## Seleccionar y mover inserts

- Haz clic en una franja para seleccionarla.
- `Ctrl`+clic, o arrastrar por varias franjas, selecciona un rango. `Ctrl+Shift`+clic añade o quita una.
- Arrastra una franja hacia un lado, o pulsa `Alt+Left` y `Alt+Right`, para mover los inserts seleccionados.
- Haz clic derecho en una franja para ver el menú del insert. `F2` cambia el nombre y el color del insert seleccionado.
- `Del`, o `Delete` en el menú del insert, elimina los inserts seleccionados. LYNX SHARE pregunta antes. Marca `Recordar mi elección` para saltarte la pregunta la próxima vez.

Mover, insertar o eliminar inserts mantiene los envíos, sidechains y la automatización en su insert.

La sección `Mixer` del menú del insert y `Opciones › Mixer` tienen dos comandos más. `Ordenar inserts como la Playlist` mueve al principio los inserts vinculados a pistas de la Playlist, en el orden de la Playlist. Con `Conservar enlaces a inserts eliminados, en gris (LYNX)` activado, los envíos que iban a un insert eliminado se quedan, en gris, para que puedas conectarlos de nuevo.

## Silenciar y solo

Haz clic en la luz de silenciar para silenciar un insert. Clic derecho o `Ctrl`+clic para solo. `S` pone en solo los inserts seleccionados, y `Alt+S` los pone en solo junto con los inserts que los alimentan.

Un insert silenciado mantiene su medidor en movimiento. En proyectos nuevos y en proyectos de FL Studio importados también sigue ejecutando sus efectos, y solo se corta su salida al Master y sus envíos. Para que un insert silenciado deje de procesar, activa `Los inserts silenciados no procesan (LYNX, ahorra CPU; este proyecto)` en la sección `Mixer` del menú del insert o en `Opciones › Mixer`. Entonces su medidor se detiene y sus efectos empiezan de cero al quitar el silencio.

## Efectos

El panel de efectos muestra las diez ranuras del insert seleccionado.

- Arrastra una ranura, o usa la rueda sobre ella, para subirla o bajarla.
- Haz clic derecho en una ranura para ver sus opciones (reemplazar, mover, eliminar).
- Mientras el menú de una ranura está abierto, `Del` elimina el plugin de esa ranura.
- `Agrupar en rack de efectos de audio` en el menú de la ranura mete el efecto en un rack nuevo, y `Agrupar todo en rack de efectos de audio` lo hace con toda la cadena. `Desagrupar` devuelve a las ranuras los efectos de un rack de una sola cadena.
- El interruptor de la cabecera del panel activa o desactiva todos los efectos del insert.
- Puedes arrastrar un efecto desde la base de plugins del [Browser](/wiki/browser/) a un insert o a una ranura de efecto.
- `Voz limpia` en el menú del insert pone en el insert una cadena de limpieza de voz ya preparada.

## Edison en una ranura

Edison, el editor y grabador de audio, puede estar en cualquier ranura de efecto, varios por insert. Su ventana es el editor de audio con una fila de controles de grabación.

- `Abrir editor de audio` (`Ctrl+E`) en el menú del insert carga Edison en la primera ranura vacía del insert y lo abre. `Abrir el editor de audio y grabar` (`Shift+E`) lo carga listo para grabar cuando suena la canción.
- Los modos de grabación son `Ahora`, `Con entrada` (el predeterminado: espera a que la entrada supere un umbral), `Entrada` y `Al reproducir`. `Anexar` pone una grabación nueva después del audio que ya hay. El tiempo máximo de grabación es `Sin límite` o 1, 3, 5, 10, 30 o 60 minutos: una ranura nueva empieza en 30, `Abrir el editor de audio y grabar` fija 5.
- `Sync` reproduce el audio de Edison al tiempo de la canción mientras esta suena.
- Edison sigue grabando después de cerrar su ventana.
- `Herram. › Macros › Quitar todas las instancias de Edison` vacía todas las ranuras Edison del proyecto, tras una pregunta.
- Las ranuras Edison de los proyectos de FL Studio (.flp) se abren con su audio.

## EQ del insert

Cada insert tiene su propio EQ. Arrastra una banda para fijar su frecuencia y nivel, usa la rueda para el ancho, y haz doble clic en una banda para devolverla a 0 dB. Haz clic derecho en el título del EQ para puentearlo. Si quieres un Mixer sin EQ integrado, los proyectos nuevos pueden configurarse para empezar con un mixer limpio.

## Enrutado, envíos y sidechain

![El Mixer con el insert Kick seleccionado, sus envíos dibujados como cables bajo las franjas y el panel de efectos a la derecha](/assets/img/wiki/mixer-sends.webp)

Selecciona el insert que quieres enrutar. Entonces cada una de las demás franjas tiene un interruptor de ruta:

- Haz clic en el interruptor de otra franja para enviar allí el insert seleccionado. Si arrastras tras activarlo, fijas el nivel de envío.
- `Ctrl`+clic enruta solo a ese insert.
- `Shift`+clic crea un envío de sidechain. `Ctrl+Shift`+clic lo crea solo de sidechain.
- El clic derecho abre las mismas opciones como menú.
- Para un envío pre-fader, haz clic derecho en el interruptor de ruta o en el mando de nivel del envío y marca `Envío pre-fader (el fader de esta pista no lo mueve)`. El envío se marca entonces como `PRE`.

Los efectos con entrada lateral, como un compresor, tienen un selector `Sidechain desde` que lista los inserts que se pueden escuchar. Al elegir uno se crea la ruta por ti.

## Grupos y faders VCA

Los grupos del mixer se pliegan con su chevron (o `Alt`+clic para todos los grupos). Un fader VCA mueve el nivel de sus inserts miembros mientras estos conservan sus propios faders. Créalo desde el menú del insert: en Grupo, elige `VCA › Nuevo VCA con los seleccionados`.

## Grabación

Cada insert tiene un botón de armado de grabación. Con un insert armado, Rec y Play graban su entrada de audio en un clip de la Playlist. `Shift+R` arma el insert seleccionado y empieza a grabar en un solo paso. Mira [Grabación](/wiki/recording/).

Cuando eliges una entrada externa para un insert que también graba otro audio, LYNX SHARE pregunta si quieres cambiarlo a `Solo entrada externa`, para que la toma contenga solo la entrada. `Mixer › Grabación en disco › Compensación de latencia` en el menú del insert está activado por defecto. Si se desactiva, las tomas grabadas se colocan sin ninguna compensación de latencia.

## Teclas

`Ctrl+L` y `Ctrl+Shift+L` tienen este significado solo cuando el foco está en el Mixer.

| Comando | Tecla | Qué hace |
|---|---|---|
| Vincular canales | `Ctrl+L` | Vincula los canales seleccionados al insert seleccionado |
| Vincular a inserts consecutivos | `Ctrl+Shift+L` | Vincula los canales seleccionados a inserts a partir del seleccionado |
| Mover inserts | `Alt+Left` / `Alt+Right` | Mueve los inserts seleccionados |
| Renombrar y colorear | `F2` | Cambia el nombre y el color del insert seleccionado |
| Solo | `S` | Pone en solo los inserts seleccionados |
| Solo con fuentes | `Alt+S` | Pone en solo los inserts seleccionados y los inserts que los alimentan |
| Eliminar inserts | `Del` | Elimina los inserts seleccionados, tras una pregunta |
| Armar y grabar | `Shift+R` | Arma el insert seleccionado y empieza a grabar |
| Open audio editor | `Ctrl+E` | Carga Edison en la primera ranura vacía del insert y lo abre |
| Open audio editor and record | `Shift+E` | Lo mismo, listo para grabar cuando suena la canción |

## Conviene saber

- `Medidor de sonoridad junto al Master (LYNX)`, en `Opciones › Mixer`, muestra la sonoridad a corto plazo e integrada (LUFS) y el true peak de la salida del Master, con un botón `Reset`.
- Un plugin de instrumento con varias salidas puede reproducir cada salida en su propio insert. Consulta [Plugins de terceros](/wiki/plugins/).
- `Ctrl+L` hace cosas distintas en el Channel Rack y en el Mixer. En el rack elige inserts libres. En el Mixer usa el insert seleccionado.
- Los interruptores de ruta aparecen en las demás franjas, no en la seleccionada.
- El límite es de 127 inserts por proyecto y diez ranuras de efecto por insert.
- La grabación de Edison se guarda como archivo WAV en la carpeta del proyecto, no dentro del archivo del proyecto.
- El mixer de Session bajo la cuadrícula de clips pertenece solo al flujo Clip-launch. Muestra los mismos inserts que este Mixer. Mira [Diseños](/wiki/layouts/).

## Relacionado

- [Channel Rack](/wiki/channel-rack/)
- [Grabación](/wiki/recording/)
- [Automatización](/wiki/automation/)
- [Plugins de terceros](/wiki/plugins/)
- [Instrumentos y efectos](/instruments-and-effects/)
