---
source: b2de12e24d
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

Mover, insertar o eliminar inserts mantiene los envíos, sidechains y la automatización en su insert.

## Silenciar y solo

Haz clic en la luz de silenciar para silenciar un insert. Clic derecho o `Ctrl`+clic para solo. `S` pone en solo los inserts seleccionados, y `Alt+S` los pone en solo junto con los inserts que los alimentan.

## Efectos

El panel de efectos muestra las diez ranuras del insert seleccionado.

- Arrastra una ranura, o usa la rueda sobre ella, para subirla o bajarla.
- Haz clic derecho en una ranura para ver sus opciones (reemplazar, mover, eliminar).
- `Del` elimina el plugin de la ranura seleccionada.
- El interruptor de la cabecera del panel activa o desactiva todos los efectos del insert.
- Puedes arrastrar un efecto desde la base de plugins del [Browser](/wiki/browser/) a un insert o a una ranura de efecto.

## EQ del insert

Cada insert tiene su propio EQ. Arrastra una banda para fijar su frecuencia y nivel, usa la rueda para el ancho, y haz doble clic en una banda para devolverla a 0 dB. Haz clic derecho en el título del EQ para puentearlo. Si quieres un Mixer sin EQ integrado, los proyectos nuevos pueden configurarse para empezar con un mixer limpio.

## Enrutado, envíos y sidechain

![El Mixer con el insert Kick seleccionado, sus envíos dibujados como cables bajo las franjas y el panel de efectos a la derecha](/assets/img/wiki/mixer-sends.webp)

Selecciona el insert que quieres enrutar. Entonces cada una de las demás franjas tiene un interruptor de ruta:

- Haz clic en el interruptor de otra franja para enviar allí el insert seleccionado. Si arrastras tras activarlo, fijas el nivel de envío.
- `Ctrl`+clic enruta solo a ese insert.
- `Shift`+clic crea un envío de sidechain. `Ctrl+Shift`+clic lo crea solo de sidechain.
- El clic derecho abre las mismas opciones como menú.

Los efectos con entrada lateral, como un compresor, tienen un selector `Sidechain desde` que lista los inserts que se pueden escuchar. Al elegir uno se crea la ruta por ti.

## Grupos y faders VCA

Los grupos del mixer se pliegan con su chevron (o `Alt`+clic para todos los grupos). Un fader VCA mueve el nivel de sus inserts miembros mientras estos conservan sus propios faders. Créalo desde el menú del insert: en Grupo, elige `VCA › Nuevo VCA con los seleccionados`.

## Grabación

Cada insert tiene un botón de armado de grabación. Con un insert armado, Rec y Play graban su entrada de audio en un clip de la Playlist. `Shift+R` arma el insert seleccionado y empieza a grabar en un solo paso. Mira [Grabación](/wiki/recording/).

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
| Eliminar efecto | `Del` | Elimina el plugin de la ranura seleccionada |
| Armar y grabar | `Shift+R` | Arma el insert seleccionado y empieza a grabar |

## Conviene saber

- `Renderizar a audio` (un insert convertido en un clip de audio) suena unos 5 dB más fuerte que el insert que reemplaza. Es un problema conocido de las notas de la versión 0.1.25.
- `Ctrl+L` hace cosas distintas en el Channel Rack y en el Mixer. En el rack elige inserts libres. En el Mixer usa el insert seleccionado.
- Los interruptores de ruta aparecen en las demás franjas, no en la seleccionada.
- El límite es de 127 inserts por proyecto y diez ranuras de efecto por insert.

## Relacionado

- [Channel Rack](/wiki/channel-rack/)
- [Grabación](/wiki/recording/)
- [Automatización](/wiki/automation/)
- [Plugins de terceros](/wiki/plugins/)
- [Instrumentos y efectos](/instruments-and-effects/)
