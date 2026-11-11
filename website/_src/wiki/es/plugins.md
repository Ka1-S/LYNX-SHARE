---
source: be0fc22373
---
![La parte inferior del Gestor de plugins, con su nota de que cada archivo de plugin se examina en un proceso aparte](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE aloja plugins de instrumentos y efectos de otros fabricantes. Los encuentra escaneando las carpetas de plugins, y `Opciones › Gestor de plugins...` muestra lo que encontró. Los instrumentos y efectos propios de LYNX SHARE se describen en [Instrumentos y efectos integrados](/wiki/built-in/).

## Escanear y añadir un plugin

1. Elige `Opciones › Buscar plugins`. El progreso aparece en la barra de ayuda y puedes seguir trabajando.
2. Si el plugin está en una carpeta aparte, abre `Opciones › Gestor de plugins...`, haz clic en `Carpetas de plugins...`, añade la carpeta y luego haz clic en `Buscar plugins nuevos`.
3. Pulsa `F8` para abrir el selector de plugins y escribe parte del nombre del plugin.
4. Haz doble clic en él o pulsa `Enter`. Un instrumento se convierte en un canal nuevo. Un efecto va al insert del Mixer seleccionado.

En el primer arranque, el asistente de configuración lista las carpetas de plugins y las escanea en segundo plano. `Opciones › Asistente de configuración (idioma, audio, carpetas de plugins)...` lo abre de nuevo. LYNX SHARE también busca plugins nuevos unos segundos después de cada arranque.

## Formatos por sistema

| Sistema | Formatos alojados |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 no se aloja en macOS. En Linux, los plugins de Windows funcionan con [yabridge](https://github.com/robbert-vdh/yabridge) y Wine, que instalas tú mismo: añade como carpeta de plugins la carpeta con la que se sincroniza yabridge.

## El Gestor de plugins

| Control | Qué hace |
|---|---|
| Cuadro de búsqueda | Filtra por nombre, fabricante, formato o archivo |
| `Todos`, `Instrumentos`, `Efectos`, `Favoritos`, `Con errores` | Elige qué plugins se listan |
| Columna de estrella | Marca un favorito |
| Columna `Bits` | 32, 64 o 32+64 (Windows) |
| Columna `Estado` | `Aceptar`, `No encontrado` o `Error al cargar` |
| `Buscar plugins nuevos` | Escanea los archivos que aún no están en la lista |
| `Reescanear todo...` | Olvida los archivos con error y examina todos los archivos de nuevo |
| `Carpetas de plugins...` | Edita las carpetas que se escanean |
| `Añadir plugin...` | Añade un archivo de plugin |

Cada archivo de plugin se examina en un proceso aparte. Un archivo que falla o se cuelga durante 30 segundos se marca como erróneo y se omite en adelante. En Windows, los archivos con error se listan con el motivo en `%APPDATA%\LYNX\scan-failures.log`.

Se escanean las carpetas estándar, más las que añadas:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` y `Steinberg\VSTPlugins` dentro de Program Files, y las carpetas que usa FL Studio si está instalado.
- macOS: `VST3`, `Components` y `CLAP` en `~/Library/Audio/Plug-Ins` y `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` y las mismas carpetas bajo `/usr/lib` y `/usr/local/lib`.

## Ventanas de plugin acopladas y separadas

El editor de un plugin se abre dentro de la ventana principal, como los demás paneles. `Separado` en el menú de la ventana del plugin da a ese plugin una ventana propia, y `Fijar encima` en el mismo menú mantiene una ventana separada por encima de las demás ventanas de LYNX SHARE.

`Separar todos los plugins nuevos` en `Opciones › Configuración general...` abre separados todos los plugins que añadas. `Herram. › Cambiar todos los editores de plugins a` establece `Acoplado` o `Separado` para todos los plugins del proyecto.

## Ejecutar plugins en un proceso aparte

Por defecto, un plugin se ejecuta dentro de LYNX SHARE, así que un plugin que falla puede tumbar el programa. Como opción, cada plugin se ejecuta en su propio proceso.

1. Abre `Opciones › Configuración general...`.
2. Activa `Ejecutar plugins en un proceso aparte (protege a LYNX SHARE de los cierres inesperados)`.
3. Vuelve a abrir el proyecto para aplicarlo a los plugins que ya están abiertos.

Si uno de esos plugins falla o deja de responder, se silencia, aparece un mensaje y LYNX SHARE sigue funcionando. `Recargar` en ese mensaje vuelve a iniciar el plugin con los ajustes del último guardado o copia de seguridad, sin reabrir el proyecto. El editor del plugin se abre en su propia ventana y no se puede acoplar ni fijar.

Para hacerlo solo con un plugin, usa `Ejecutar en un proceso aparte` o `Always run <plugin> in a separate process` en el menú del wrapper de la ventana del plugin. Cuando falla el editor de un plugin, el mensaje que aparece ofrece también la segunda.

Esto funciona en Windows y Linux, y desde la 0.1.25 en macOS.

Un plugin que falla dentro de LYNX SHARE se desactiva, y su mensaje también ofrece `Recargar`. Más tarde, usa `Recargar el plugin que falló` en el menú del canal o en el menú de la ranura del efecto.

## Plugins de 32 bits (solo Windows)

En Windows, los plugins VST2 y VST3 de 32 bits se ejecutan a través de un puente (`LYNX-bridge32.exe`). El puente no añade latencia. Un plugin que falla en él se detiene por sí solo mientras LYNX SHARE sigue reproduciendo. El editor de un plugin con puente se abre en su propia ventana y no se puede acoplar ni fijar. Cuando hay instaladas una versión de 32 bits y otra de 64 bits de un plugin, `Tamaño en bits` en `Configuración del wrapper` de la ventana del plugin elige cuál se carga.

## Monitor de rendimiento de plugins

![El monitor de rendimiento de plugins: una fila por instrumento y efecto con su parte del tiempo de procesamiento](/assets/img/wiki/plugin-monitor.webp)

`Herram. › Monitor de rendimiento de plugins...` lista cada instrumento y efecto con el tiempo de procesamiento que consume: ahora, de media y en su pico. 100% significa un núcleo del procesador ocupado durante todo el tiempo que dura el audio.

| Ajuste | Qué hace |
|---|---|
| `Buscar` | Filtra por plugin, canal o insert |
| `Solo plugins` | Oculta los instrumentos y efectos propios de LYNX SHARE |
| `Congelar` | Deja de actualizar los números |
| `Reiniciar al reproducir` | Reinicia las medias y los picos cada vez que empieza la reproducción |
| `Reset` | Borra ahora las medias y los picos |

## Enviar las salidas de un instrumento a inserts separados

Un plugin de instrumento con varias salidas puede reproducir cada una en su propio insert del Mixer. En la ventana del plugin, haz clic en `Configuración del wrapper` y luego en la pestaña `Procesamiento`. En `Conexiones`, cada salida tiene una luz que la activa o desactiva y un número: cuántos inserts después del insert del propio canal suena. `Asignar salidas automáticamente` da a cada salida el siguiente insert, y `Reset` vuelve a los valores por defecto.

## La carpeta Plugin database

Las carpetas de la Plugin database del [Browser](/wiki/browser/), del selector de plugins y de los menús de plugins son una carpeta en el disco: `LYNX/Plugin database` en tu carpeta Documentos. Para abrirla, haz clic derecho en `Base de plugins` en el Browser y elige `Abrir`.

- `Efectos` y `Generadores` contienen un archivo `.lynxplugin` por plugin. Crea carpetas y subcarpetas (hasta 4 niveles) y mueve los archivos a ellas.
- Cambia el nombre de un archivo para renombrar el plugin en los menús. Elimina un archivo para quitar el plugin de los menús. No se desinstala.
- `Instalados` tiene una entrada por cada plugin. Copia una en `Efectos` o `Generadores` para recuperar un plugin.

## Abrir un proyecto sin sus plugins

`Archivo › Abrir sin plugins de terceros...` abre un proyecto de LYNX SHARE con todos los plugins de terceros desactivados, para un proyecto que no se abre o que falla al cargar. Los ajustes guardados de los plugins se conservan. `Archivo › Volver a activar plugins...` los vuelve a activar.

## Cuando un plugin no está instalado

LYNX SHARE busca primero el mismo plugin por su identificador, luego por archivo y luego por nombre. Un proyecto guardado con el VST2 se abre con el VST3 del mismo producto y al revés, pero entonces el plugin suena con sus ajustes por defecto.

Si un efecto no está instalado en absoluto, un efecto de LYNX SHARE con la misma función ocupa su ranura y el original se queda en el proyecto. Cuando se pueden leer los ajustes guardados del plugin, el sustituto empieza con ajustes equivalentes. El interruptor es `Sustituir los plugins que faltan por equivalentes de LYNX SHARE` en `Opciones › Configuración general...`. Consulta [Instrumentos y efectos integrados](/wiki/built-in/). `Ayuda › Archivos que faltan...` lista los plugins que se reemplazaron.

## Conviene saber

- La ventana de un plugin alojado tiene una barra de presets: los programas del propio plugin, tus presets, `Guardar preset...`, `Cargar archivo de preset...` y `Exportar archivo de preset...`. Los archivos `.fxp`, `.fxb` y `.vstpreset` solo se cargan en un plugin que se ejecuta dentro de LYNX SHARE, no en un proceso aparte.
- El monitor mide solo mientras su ventana está abierta.
- Mientras la ventana de un plugin tiene el foco, una tecla que el plugin no usa toca el piano del teclado de escritura. En Windows, con el botón de teclado del encabezado de la ventana desactivado, LYNX SHARE procesa las teclas antes que el plugin, atajos incluidos.
- El menú de parámetros propio de un plugin VST3 empieza con los elementos de LYNX SHARE, como `Crear clip de automatización`. Consulta [Automatización](/wiki/automation/).
- `MPE` en `MIDI` dentro de `Configuración del wrapper` de un plugin envía cada nota por su propio canal MIDI. Consulta [Controladores MIDI](/wiki/midi-controllers/).
- Los instrumentos que faltan todavía no se sustituyen. Un plugin de instrumento que falta se convierte en un sintetizador integrado elegido según el nombre del canal.

## Relacionado

- [Instrumentos y efectos integrados](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Solución de problemas](/wiki/troubleshooting/)
