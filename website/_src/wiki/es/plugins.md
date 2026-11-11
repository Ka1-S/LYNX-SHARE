---
source: 45c1275215
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

## Ejecutar plugins en un proceso aparte

Por defecto, un plugin se ejecuta dentro de LYNX SHARE, así que un plugin que falla puede tumbar el programa. Como opción, cada plugin se ejecuta en su propio proceso.

1. Abre `Opciones › Configuración general...`.
2. Activa `Ejecutar plugins en un proceso aparte (protege a LYNX SHARE de los cierres inesperados)`.
3. Vuelve a abrir el proyecto para aplicarlo a los plugins que ya están abiertos.

Si uno de esos plugins falla o deja de responder, se silencia, aparece un mensaje y LYNX SHARE sigue funcionando. Guarda el proyecto y ábrelo de nuevo para cargar otra vez el plugin. El editor del plugin se abre en su propia ventana.

Para hacerlo solo con un plugin, usa `Ejecutar en un proceso aparte` o `Always run <plugin> in a separate process` en el menú del wrapper de la ventana del plugin. Cuando falla el editor de un plugin, el mensaje que aparece ofrece también la segunda.

Esto funciona en Windows y Linux, y desde la 0.1.25 en macOS.

## Plugins de 32 bits (solo Windows)

En Windows, los plugins VST2 y VST3 de 32 bits se ejecutan a través de un puente (`LYNX-bridge32.exe`). El puente no añade latencia. Un plugin que falla en él se detiene por sí solo mientras LYNX SHARE sigue reproduciendo. El editor de un plugin con puente se abre en su propia ventana. Cuando hay instaladas una versión de 32 bits y otra de 64 bits de un plugin, `Tamaño en bits` en `Configuración del wrapper` de la ventana del plugin elige cuál se carga.

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

## Cuando un plugin no está instalado

LYNX SHARE busca primero el mismo plugin por su identificador, luego por archivo y luego por nombre. Un proyecto guardado con el VST2 se abre con el VST3 del mismo producto y al revés, pero entonces el plugin suena con sus ajustes por defecto.

Si un efecto no está instalado en absoluto, un efecto de LYNX SHARE con la misma función ocupa su ranura y el original se queda en el proyecto. Consulta [Instrumentos y efectos integrados](/wiki/built-in/). `Ayuda › Archivos que faltan...` lista los plugins que se reemplazaron.

## Conviene saber

- La ventana de un plugin alojado tiene una barra de presets: los programas del propio plugin, tus presets, `Guardar preset...`, `Cargar archivo de preset...` y `Exportar archivo de preset...`. Los archivos `.fxp`, `.fxb` y `.vstpreset` solo se cargan en un plugin que se ejecuta dentro de LYNX SHARE, no en un proceso aparte.
- El monitor mide solo mientras su ventana está abierta.
- Los instrumentos que faltan todavía no se sustituyen. Un plugin de instrumento que falta se convierte en un sintetizador integrado elegido según el nombre del canal.

## Relacionado

- [Instrumentos y efectos integrados](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Solución de problemas](/wiki/troubleshooting/)
