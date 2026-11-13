---
source: d5cc625221
---
![El diseño de lanzamiento de clips: una cuadrícula de clips con escenas junto a la Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE arranca en el flujo por patrones: un Channel Rack, patrones y una Playlist. El flujo por lanzamiento de clips es opcional y añade una cuadrícula de clips con escenas junto a la Playlist. LYNX SHARE lee y escribe sets `.als`. No está afiliado a Ableton.

## Abrir un set de Live

1. Elige `Archivo › Abrir...` (`Ctrl+O`) y selecciona el `.als`. Se leen los sets guardados con Live 11 y Live 12.
2. Elige `Ver › Estilo de diseño › Flujo por lanzamiento de clips` para trabajar con la cuadrícula de clips, donde van los clips de sesión y las escenas del set.
3. Usa el selector `Grid / Both / Song` de la barra de título de la Playlist para mostrar la cuadrícula, la Playlist o ambas.
4. Elige `Archivo › Guardar`. Pregunta dónde escribir un archivo `.lynx`. El `.als` no se modifica.

`Ver › Estilo de diseño › Flujo por patrones (predeterminado)` vuelve atrás. La misma opción está en `Opciones › Configuración general...`. Los patrones y el Channel Rack siguen disponibles en ambos diseños.

## Qué equivale a qué

| En Live | En LYNX SHARE |
|---|---|
| Vista Session | La cuadrícula de clips del flujo por lanzamiento de clips |
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
| Una pista MIDI por canal, con sus notas como clips MIDI | Los instrumentos y efectos propios de LYNX SHARE. Se conservan sus pistas y notas |
| Pistas de audio con stretch, transposición, ganancia, fundidos y modo de warp | Automatización de parámetros de efectos |
| Grupos, retornos y envíos | Rutas de insert a insert |
| Plugins VST2 y VST3 con su estado | |
| Marcadores, y automatización de volumen, pan y tempo | |
| La cuadrícula de clips como clips de sesión y escenas, racks y Drum Racks | |

Un informe tras la exportación lista lo que quedó fuera.

## Conviene saber

- Muchos de los dispositivos propios de Live entran como propios de LYNX SHARE, con sus ajustes y automatización: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift y Operator (sin FM). Los demás se descartan y se listan en el informe de importación.
- Los Instrument Racks y los Audio Effect Racks entran con sus cadenas, zonas y macros, y un Drum Rack pasa a ser el Drum Rack de LYNX SHARE. Los MIDI Effect Racks de Live todavía no se convierten y aparecen en el informe de importación.
- El informe de importación lista lo que quedó fuera.
- Live recorta los clips de audio que se solapan en un set exportado.
- Los marcadores de warp de un set de Live se conservan al importar y se escriben de nuevo al exportar.

## Relacionado

- [Playlist](/wiki/playlist/)
- [Disposiciones](/wiki/layouts/)
- [Importar proyectos](/wiki/importing-projects/)
- [Formatos de archivo](/wiki/file-formats/)
- [Dos flujos de trabajo en un solo programa](/why/#workflows)
