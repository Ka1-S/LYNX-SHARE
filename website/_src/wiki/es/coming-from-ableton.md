---
source: 37920663db
---
![El diseño de lanzamiento de clips: una cuadrícula de clips con escenas junto a la Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE arranca en el diseño Patrón: un Channel Rack, patrones y una Playlist. El diseño de lanzamiento de clips es opcional y añade una cuadrícula de clips con escenas junto a la Playlist. LYNX SHARE lee y escribe sets `.als`. No está afiliado a Ableton.

Esta página también es para quienes usan Bitwig Studio: la cuadrícula de clips funciona igual, y los archivos DAWproject de Bitwig (`.dawproject`) se abren con `Archivo › Abrir...` y se escriben con `Archivo › Exportar › DAWproject (.dawproject)...`.

## Tus primeros pasos

1. **Elige el diseño.** La primera vez que arranca LYNX SHARE, una tarjeta de bienvenida pregunta de dónde vienes. Haz clic en `Ableton Live o Bitwig`: se abre el Diseño de lanzamiento de clips. Más tarde, `Ver › Estilo de diseño › Diseño de lanzamiento de clips` cambia a él en cualquier momento.
2. **Llena una ranura.** Arrastra un sample o un patrón del Browser a una ranura, o haz clic derecho en una ranura vacía y elige `Poner un patrón aquí`.
3. **Lanza.** Haz clic en el triángulo a la izquierda de un clip, o en el triángulo de una escena en la columna `Escenas` para lanzar toda la fila. O abre uno de tus sets `.als` (abajo).

Qué es distinto de Live:

- La Playlist es el arreglo, y sus pistas son las columnas de la cuadrícula. Una pista que lanzó un clip se queda en la cuadrícula hasta que pulsas `Volver a la Playlist`.
- Fuera de la cuadrícula, las teclas siguen a FL Studio. `Ctrl+Z` deshace un paso y, pulsado otra vez, lo rehace.
- Solo se convierten algunos de los dispositivos propios de Live (consulta Conviene saber). Los plugins de terceros pasan con su estado.
- El Diseño Patrón, la forma de trabajar de FL Studio con Channel Rack y patrones, funciona sobre el mismo proyecto.

## Abrir un set de Live

1. Elige `Archivo › Abrir...` (`Ctrl+O`) y selecciona el `.als`. Se leen los sets guardados con Live 11 y Live 12.
2. Elige `Ver › Estilo de diseño › Diseño de lanzamiento de clips` para trabajar con la cuadrícula de clips, donde van los clips de sesión y las escenas del set.
3. Usa el selector `Grid / Both / Song` de la barra de título de la Playlist para mostrar la cuadrícula, la Playlist o ambas.
4. Elige `Archivo › Guardar`. Pregunta dónde escribir un archivo `.lynx`. El `.als` no se modifica.

`Ver › Estilo de diseño › Diseño Patrón (predeterminado)` vuelve atrás. La misma opción está en `Opciones › Configuración general...`. Los patrones y el Channel Rack siguen disponibles en ambos diseños.

## Qué equivale a qué

| En Live | En LYNX SHARE |
|---|---|
| Vista Session | La cuadrícula de clips del diseño de lanzamiento de clips |
| Vista Arrangement | La Playlist |
| Pistas | Pistas de la Playlist, que son las columnas de la cuadrícula de clips |
| Escenas | Escenas, las filas de la cuadrícula de clips. Una escena puede fijar el tempo al lanzarse |
| Cuantización de lanzamiento | `Global`, `Ninguno`, de 8 compases a 1 compás, o un valor de nota de 1/2 a 1/32, por clip si quieres |
| Modos de lanzamiento | `Disparo`, `Gate`, `Alternar` y `Repetir` |
| Acciones siguientes | Acción A y Acción B con una probabilidad para cada una |
| Botones de parada de clip | Una ranura vacía puede tener un botón de parada |
| Volver a Arrangement | `Volver a la Playlist` |
| Modos de warp | `Tiempos`, `Tonos`, `Textura` y `Complejo` en `Configuración del canal › Estirar el tiempo` |
| Simpler, Sampler y racks de batería | Samplers de LYNX SHARE |
| Set `.als` | Archivo de proyecto `.lynx` |

## La cuadrícula de clips

Una ranura contiene un clip de patrón o un clip de audio.

- Una pista reproduce un clip a la vez. Un clip lanzado sustituye al que está sonando.
- Un lanzamiento espera al siguiente paso de la cuantización de lanzamiento.
- `Legato` inicia el clip nuevo en la posición que había alcanzado el clip sustituido. `Bucle` se define por clip.
- Las escenas se pueden insertar, eliminar y renombrar.
- Una pista que lanzó un clip queda en silencio en la Playlist hasta que pulsas `Volver a la Playlist`. Una opción devuelve las pistas cuando se detiene el transporte.

Las acciones siguientes se ejecutan tras un número de compases o de pasadas del clip. Las acciones son `Sin acción`, `Detener`, `Reproducir de nuevo`, `Anterior`, `Siguiente`, `Primero`, `Último`, `Cualquiera` y `Otros`. Un interruptor activa o desactiva las acciones siguientes en toda la cuadrícula.

## Qué se importa de un .als

- Pistas MIDI, de audio, de grupo y de retorno, tempo, compás, locators, el bucle, ajustes del mezclador (volumen, pan, silencio, solo, envíos) y su automatización.
- Plugins VST2 y VST3 con su estado, si está instalado el mismo plugin. Consulta [Plugins de terceros](/wiki/plugins/).
- Clips de sesión y escenas, en la cuadrícula de clips, con su modo de lanzamiento, cuantización, legato, bucle, acciones siguientes y botones de parada. Para colocarlos en la Playlist, una sección por escena, cambia `Clips de sesión de sets de Live` en `Opciones › Configuración general...`.
- Marcadores de warp.

## Exportar un .als

1. Elige `Archivo › Exportar › Set de Ableton Live (.als)...`.
2. En `Guardar para`, elige `Live 12` o `Live 11 (también se abre en Live 12)`.
3. En `Samples`, cópialos a la carpeta del proyecto del set o haz referencia a ellos donde están.
4. Elige una carpeta. El set se guarda como `<name> Project/<name>.als`.

| Se escribe | No se escribe |
|---|---|
| Una pista MIDI por canal, con sus notas como clips MIDI | Los instrumentos y efectos propios de LYNX SHARE, salvo Helix, que pasa a ser Wavetable. Se conservan sus pistas y notas |
| Pistas de audio con stretch, transposición, ganancia, fundidos y modo de warp | Automatización de parámetros de efectos |
| Grupos, retornos y envíos | Rutas de insert a insert |
| Plugins VST2 y VST3 con su estado | |
| Marcadores, y automatización de volumen, pan y tempo | |
| La cuadrícula de clips como clips de sesión y escenas, racks y Drum Racks | |

Un informe tras la exportación lista lo que quedó fuera.

## Conviene saber

- Muchos de los dispositivos propios de Live entran como propios de LYNX SHARE, con sus ajustes y automatización: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable y Meld. Los demás se descartan y se listan en el informe de importación.
- Los Instrument Racks y los Audio Effect Racks entran con sus cadenas, zonas y macros, y un Drum Rack pasa a ser el Drum Rack de LYNX SHARE. El Arpeggiator de Live pasa a ser el arpegiador del canal. Pitch, Velocity, Chord, Note Length y los MIDI Effect Racks se escriben en las notas.
- El informe de importación lista lo que quedó fuera.
- En la vista de dispositivo, un rack muestra en línea los dispositivos de sus cadenas, y el modo Macro Map (los botones `Map` del rack) da un parámetro de un dispositivo a una macro, como en Live.
- El mezclador de sesión tiene el interruptor Solo / Cue de Live, con un `Cue Out` para tus auriculares y un `Volumen de escucha`.
- Live recorta los clips de audio que se solapan en un set exportado.
- Los marcadores de warp de un set de Live se conservan al importar y se escriben de nuevo al exportar.

## Relacionado

- [Playlist](/wiki/playlist/)
- [Disposiciones](/wiki/layouts/)
- [Importar proyectos](/wiki/importing-projects/)
- [Formatos de archivo](/wiki/file-formats/)
- [Dos diseños en un solo programa](/why/#workflows)
- [Aprende: tutoriales](/learn/)
