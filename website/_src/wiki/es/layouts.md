---
source: e15b0e3ba5
---
![El flujo de lanzamiento de clips: una cuadrícula de clips y escenas a la izquierda, el Selector y la Playlist a la derecha](/assets/img/cliplaunch.webp)

`Ver › Estilo de diseño` tiene tres entradas: `Flujo por patrones (predeterminado)`, `Flujo por lanzamiento de clips` y `Flujo simple`. La misma opción está en `Opciones › Configuración general`, bajo Flujo de trabajo. La elección se conserva al cerrar el programa.

El flujo por patrones es el Channel Rack, los patrones y la Playlist. El flujo por lanzamiento de clips añade una cuadrícula de clips con escenas junto a la Playlist. Los patrones y el Channel Rack siguen disponibles.

## Lanzar clips en la cuadrícula

1. Elige `Ver › Estilo de diseño › Flujo por lanzamiento de clips`. La cuadrícula de clips aparece junto a la Playlist.
2. Haz clic derecho en una ranura vacía, elige `Poner un patrón aquí` y selecciona un patrón. Con doble clic en una ranura vacía se pone allí el patrón actual.
3. Haz clic en el triángulo a la izquierda del clip para lanzarlo. Si el transporte está detenido, arranca.
4. Haz clic en el triángulo de una escena, en la columna `Escenas`, para lanzar todos los clips de esa fila.
5. Haz clic en `Detener todos los clips` para detenerlos.
6. Haz clic en `Volver a la Playlist` para que las pistas vuelvan a reproducir sus clips de la Playlist.

## Qué cambia en pantalla

- La cuadrícula de clips se muestra junto a la Playlist. Las pistas de la Playlist son las columnas y las escenas son las filas.
- La barra de título de la Playlist gana un selector `Ambos`, `Cuadrícula`, `Canción`. `Cuadrícula` muestra solo la cuadrícula, `Canción` solo la Playlist.
- La primera vez, el Channel Rack queda oculto. `F6` o su botón de la barra de herramientas lo muestra.
- Se activan los carriles de automatización de la Playlist.
- El diseño de la ventana se guarda por estilo, así que cada estilo vuelve como lo dejaste.
- Bajo la cuadrícula, el botón `Mixer` muestra u oculta el mixer de Session, como en Ableton Live: `Entrada/Salida` (`Audio desde`, `Monitor`, `Audio hacia`), `Envíos`, `Retardo de pista`, y el volumen de cada pista con su activador, solo, botón de armado y medidor, además de la tira `Principal`.
- `Grooves`, bajo la cuadrícula, muestra el Groove Pool. `Aplicar groove a las notas` escribe un groove en las notas de un clip MIDI.
- Los botones `MIDI` y `Tonalidad` inician el MIDI Map Mode y el Key Map Mode. Mira [Controladores MIDI](/wiki/midi-controllers/).

## La cuadrícula

Un clip lanzado no arranca de inmediato. Espera al siguiente punto de cuantización de lanzamiento, y su triángulo parpadea hasta entonces. La caja de abajo de la cuadrícula fija la cuantización de lanzamiento global, que usan todos los clips puestos en `Global`. Una detención espera de la misma manera.

Haz clic derecho en un clip para ver sus ajustes.

| Ajuste | Qué hace |
|---|---|
| `Cuantización de lanzamiento` | Cuándo arranca este clip: `Global`, `Ninguno`, `8 compases` hasta `1/32` |
| `Modo de lanzamiento` | `Disparo`, `Gate`, `Alternar` o `Repetir` |
| `Legato (mantener la posición)` | El clip toma la posición de reproducción del clip que sonaba antes en esa pista |
| `Bucle` | Activado: el clip se repite. Desactivado: suena una vez y luego la pista se detiene |
| `Acción siguiente` | Qué ocurre tras el clip: dos acciones con una probabilidad cada una, y el tiempo tras el cual actúan |
| `Delete` | Vacía la ranura |

Haz clic derecho en una ranura vacía para ver `Botón de parada (una escena detiene esta pista aquí)`. Al hacer clic en el cuadrado a la izquierda de una ranura vacía, esa pista se detiene. Haz clic derecho en una escena para ver `Insertar escena`, `Añadir escena debajo`, `Eliminar escena`, `Renombrar escena...` y el tempo de la escena.

Los clips se pueden cortar, copiar, pegar y duplicar (`Ctrl+D`) desde su menú, y arrastrar entre ranuras y hacia o desde la Playlist. Un patrón o un sample arrastrado desde el Browser, o archivos de audio soltados desde fuera, rellenan la ranura en la que caen. El menú de una escena también tiene `Duplicar` y `Capturar e insertar escena`, que pone los clips que suenan ahora en una escena nueva. `Ver › Tempo y compás de las escenas` muestra un tempo y un compás para cada escena.

El menú de la ranura también tiene dos opciones para toda la cuadrícula: `Devolver las pistas a la Playlist al detener el transporte` y `Acciones siguientes en toda la cuadrícula`.

| Comando | Tecla | Qué hace |
|---|---|---|
| Mover | Teclas de flecha | Selecciona otra ranura |
| Lanzar clip | `Enter` | Lanza el clip seleccionado |
| Lanzar escena | `Ctrl+Enter` | Lanza la escena seleccionada |
| Borrar clip | `Del` | Vacía la ranura seleccionada |
| Draw Mode | `B` | Un arrastre en un carril de automatización dibuja pasos tan anchos como la cuadrícula, o a mano alzada con el ajuste desactivado o `Alt` pulsado, como en Ableton Live. También en `Opciones` |

## Sets de Ableton Live

Cuando abres un set de Live, sus clips y escenas de Session pasan a la cuadrícula de clips, con sus ajustes de lanzamiento. `Archivo › Exportar › Set de Ableton Live (.als)` escribe la cuadrícula de vuelta como clips de Session. `Opciones › Configuración general` tiene `Clips de sesión de sets de Live`, donde `Playlist, después del arreglo` los coloca en la Playlist.

## El flujo Simple

`Ver › Estilo de diseño › Flujo simple` es una sola ventana con las pistas y la línea de tiempo, para una primera canción. `Ver › Panel de la pista` muestra bajo la línea de tiempo un panel para la pista seleccionada, con las pestañas `Efectos`, `Instrumento`, `Teclado` y `Editor`. En este flujo, el menú `Ayuda` tiene `Tour de la primera canción` y `Pantalla de bienvenida...`, que ofrece una primera canción en siete pasos, una canción de demostración o un proyecto vacío. Mira [Diseño Simple](/wiki/simple-layout/).

`Letra y notas` y `Igualar a la referencia...` en el menú `Ver`, y `Voz limpia` en el menú de un insert del Mixer, están en todos los diseños. En el flujo Simple, `Voz limpia` también está en el menú de una pista.

## Volver atrás

Elige `Ver › Estilo de diseño › Flujo por patrones (predeterminado)`. Vuelve el diseño de ventana que tenías en el flujo por patrones, y los carriles de automatización recuperan el ajuste que tenían antes. Los clips de la cuadrícula se quedan en el proyecto.

## Conviene saber

- Una pista que lanzó un clip queda en silencio en la Playlist hasta que pulsas `Volver a la Playlist`. La opción `Devolver las pistas a la Playlist al detener el transporte` lo hace por ti en cada detención.
- Las teclas nuevas de la cuadrícula (`B`, `Q`, `Ctrl+M`, `Ctrl+K` y otras) actúan así solo en el flujo Clip-launch. En el flujo Pattern conservan el significado de FL Studio.
- Una escena en 6/8 toca un compás de tres tiempos, porque el metro cuenta negras.
- Las follow actions existen para clips, no para escenas.
- Los carriles de automatización activados a mano en el flujo por lanzamiento de clips no se conservan al volver.
- Con el flujo por patrones seleccionado, no se carga nada de la cuadrícula.

## Relacionado

- [Playlist](/wiki/playlist/)
- [Si vienes de Ableton Live](/wiki/coming-from-ableton/)
- [Importar proyectos](/wiki/importing-projects/)
- [Lo que solo hace LYNX: flujos de trabajo](/why/#workflows)

La tercera entrada es una ventana para una primera canción. Mira [Diseño sencillo](/wiki/simple-layout/).
