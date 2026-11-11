---
source: 8df354b49e
---
![El Browser a la izquierda con sus tres pestañas, el cuadro de búsqueda, el árbol de carpetas y la tira de preescucha](/assets/img/wiki/browser-tree.webp)

El Browser muestra tus carpetas de samples, el contenido del proyecto actual y los plugins que conoce LYNX SHARE. Pulsa `Alt+F8` para mostrarlo u ocultarlo. `F8` solo abre el selector de plugins, una vista aparte a pantalla completa con todos los instrumentos y efectos.

## Cargar un sample en el proyecto

1. Pulsa `Alt+F8` para mostrar el Browser.
2. En la pestaña `TODO`, haz clic en una carpeta para abrirla.
3. Haz clic en un archivo de audio para oírlo. `Up` y `Down` recorren los archivos y preescuchan cada uno.
4. Haz doble clic en el archivo para crear un canal nuevo con el sample en el [Channel Rack](/wiki/channel-rack/).
5. O arrastra el archivo a la [Playlist](/wiki/playlist/) para crear un clip de audio.

## Pestañas

| Pestaña | Muestra |
|---|---|
| TODO | El proyecto actual, la base de plugins y todas las carpetas de samples |
| PROYECTO | Solo lo que usa el proyecto actual: patrones, efectos, generadores, automatización, archivos de audio |
| FAVORITOS | Los archivos y carpetas que marcaste como favoritos (clic derecho y luego `Favoritos`) |

## Explorar y preescuchar

- Haz clic en una carpeta para abrirla o cerrarla. Con el clic derecho se abre un menú (contraer todo, mostrar en el explorador de archivos).
- Haz clic en un archivo de audio para oírlo. La tira de preescucha bajo la lista muestra su forma de onda. Haz clic en la tira para oír el archivo otra vez, o arrastra desde ella como si fuera el archivo.
- El panel sobre la tira de preescucha muestra la fecha, el tamaño y la ruta del archivo seleccionado.

## Usar un sample

| Acción | Qué hace |
|---|---|
| Doble clic | Canal nuevo con el sample |
| Arrastrar al Channel Rack | Canal nuevo, o suéltalo sobre una fila para cargarlo ahí |
| Arrastrar a la Playlist | Clip de audio |
| `Enter` | Carga el archivo en el canal seleccionado |
| `Shift+Up` / `Shift+Down` | Pasa al archivo anterior o siguiente y lo carga en el canal seleccionado |

La última te permite probar distintos samples en el sitio mientras suena el patrón.

## Otros tipos de archivo

- **Proyecto**: el doble clic lo abre.
- **Archivo MIDI**: el doble clic lo abre como proyecto.
- **SoundFont o SFZ**: el clic lo preescucha, el doble clic crea un canal LYNX Soundfont (o lo carga en el seleccionado).
- **Preset del Mixer**: el doble clic lo carga en los inserts del Mixer seleccionados, o arrástralo a un insert.
- **Preset de efecto**: arrástralo a un insert del Mixer.

## Base de plugins

La entrada Base de plugins lista los instrumentos y efectos integrados de LYNX SHARE y los plugins instalados. `Shift+F8` salta a ella.

- Haz doble clic en un instrumento para añadirlo como canal nuevo, o arrástralo al Channel Rack.
- Haz doble clic en un efecto para añadirlo al insert del [Mixer](/wiki/mixer/) seleccionado, o arrástralo a un insert o a una ranura de efecto.
- `F2` renombra el plugin seleccionado.
- Haz clic derecho en un plugin y elige `Mover a carpeta` para ordenar los plugins en carpetas. El menú de ranuras del Mixer y el menú de añadir del Channel Rack listan entonces cada carpeta superior.

## La pestaña PROYECTO

Los patrones que aparecen aquí se pueden pulsar para convertirlos en el patrón actual, abrir con doble clic en el [Piano Roll](/wiki/piano-roll/) o arrastrar a la Playlist. La sección del proyecto actual también lista el historial de deshacer: haz clic en un paso para volver a él.

## Búsqueda

Pulsa `Ctrl+F` para poner el cursor en el cuadro de búsqueda. Busca en la carpeta seleccionada (o en todas) y por debajo. En la pestaña PROYECTO busca entre los elementos del proyecto.

Tienen que coincidir todas las palabras. El cuadro también admite `OR`, un signo menos para excluir una palabra y comodines `*`:

```
kick -loop
snare OR clap
hat*.wav
```

Las carpetas se indexan en segundo plano. `Actualizar índice de búsqueda` en el menú del Browser reconstruye el índice.

## Tus propias carpetas

Usa `Añadir carpeta` en la lista de carpetas del Browser para añadir una carpeta de samples, proyectos o SoundFonts. `Opciones › Configuración de archivos (carpetas de búsqueda)` tiene las carpetas de búsqueda.

## Teclas

Las flechas, `Enter` y `F2` necesitan que el Browser tenga el foco.

| Comando | Tecla | Qué hace |
|---|---|---|
| Mostrar el Browser | `Alt+F8` | Muestra u oculta el Browser |
| Selector de plugins | `F8` | Abre el selector de plugins a pantalla completa |
| Base de plugins | `Shift+F8` | Salta a la entrada Base de plugins |
| Búsqueda | `Ctrl+F` | Pone el cursor en el cuadro de búsqueda |
| Recorrer y preescuchar | `Up` / `Down` | Selecciona el archivo anterior o siguiente y lo reproduce |
| Cerrar carpeta | `Left` | Cierra una carpeta o va a la carpeta superior |
| Abrir carpeta | `Right` | Abre una carpeta |
| Cargar | `Enter` | Carga el archivo en el canal seleccionado |
| Recorrer y cargar | `Shift+Up` / `Shift+Down` | Pasa al archivo anterior o siguiente y lo carga en el canal seleccionado |
| Renombrar plugin | `F2` | Renombra el plugin seleccionado en la base de plugins |

## Conviene saber

- Quitar una carpeta de la lista no la borra del disco.
- `F8` y `Alt+F8` son cosas distintas. `F8` es el selector de plugins, `Alt+F8` es el Browser.
- Los resultados de la búsqueda salen de un índice que se construye en segundo plano. `Actualizar índice de búsqueda` en el menú del Browser lo reconstruye.
- `Enter` carga el archivo en el canal seleccionado. El doble clic crea un canal nuevo.

## Relacionado

- [Channel Rack](/wiki/channel-rack/)
- [Plugins de terceros](/wiki/plugins/)
- [Formatos de archivo](/wiki/file-formats/)
- [Deshacer e historial](/wiki/undo-history/)
- [Instrumentos y efectos](/instruments-and-effects/)
