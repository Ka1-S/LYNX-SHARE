---
source: 841fdd7d67
---
![La página General de la ventana Configuración, con las siete pestañas de página arriba](/assets/img/wiki/settings-general.webp)

La ventana Configuración tiene siete páginas: Audio, MIDI, General, Tema, Archivos, Proyecto e Información. Las cinco primeras pertenecen al programa. Proyecto e Información pertenecen al proyecto abierto y se guardan con él. Cada página tiene su propia entrada al principio del menú `Opciones`.

## Cambiar un ajuste

1. Abre el menú `Opciones` y elige una página, por ejemplo `Configuración general...`.
2. Haz clic en otra pestaña de arriba para pasar a otra página.
3. Cambia el ajuste. Se aplica al instante, salvo el idioma y la fuente de la interfaz, que se aplican cuando LYNX SHARE se reinicia.

`F10` abre la página MIDI y `F11` la página Información.

## Audio

`Opciones › Configuración de audio...`. Driver, dispositivos, frecuencia de muestreo, tamaño del búfer y grabación. [Configuración de audio](/wiki/audio-setup/) describe esta página.

## MIDI

![La página MIDI: dispositivos de entrada, dispositivos de salida con sincronización maestra y sincronización externa](/assets/img/wiki/settings-midi.webp)

`Opciones › Configuración MIDI...`

| Ajuste | Qué hace |
|---|---|
| Dispositivos de entrada | Cada dispositivo se puede desactivar, escuchar un solo canal MIDI y tocar un canal fijo del Channel Rack en lugar de los seleccionados |
| `Actualizar lista de dispositivos` | Vuelve a buscar dispositivos |
| `Enviar sincronización maestra` | Envía MIDI clock con Song Position Pointer, o MIDI Time Code, a un dispositivo de salida. `Desplazamiento (ms)` lo envía más tarde o más pronto |
| `Enviar all notes off` | Envía all notes off al dispositivo al iniciar y después de una exportación |
| `Sincronizar con clock externo` | LYNX SHARE sigue el reloj MIDI de la entrada elegida: tempo, inicio, continuar, detener y posición |
| `Velocidad` | Para notas de dispositivos MIDI: tal como se tocan, suave, fuerte o fija |
| `Canal del modo performance` | Las notas de este canal lanzan clips de la Playlist en modo Performance |
| `Salto a marcador de la canción` | Las notas de este canal llevan la canción a un marcador en lugar de sonar |
| `Pickup (modo de recogida)` | Un controlador mueve un parámetro solo después de alcanzar su valor actual |
| Vínculos | Los vínculos de controlador de este proyecto, luego los globales, con `Quitar vínculo` y `Quitar todo` |

## General

`Opciones › Configuración general...`

| Ajuste | Qué hace |
|---|---|
| `Estilo de diseño` | Flujo por patrones (por defecto) o flujo por lanzamiento de clips |
| `Clips de sesión de sets de Live` | Adónde van los clips de Session de un set de Live importado: la cuadrícula de clips, o la Playlist tras el arreglo |
| `Idioma`, `Fuente de la interfaz` | Se aplican en el siguiente arranque |
| `Tamaño de la interfaz`, `Densidad del menú` | Escala de toda la interfaz, y menús compactos o cómodos |
| `Formas de onda` | Limpias (el color del clip) o colores espectrales, opcionalmente con los canales estéreo por separado |
| `Nombres de notas`, `Teclas negras` | Nombres ingleses, germánicos o de solfeo, con sostenidos o bemoles |
| `Estilo de plugins` | El aspecto de las ventanas de los plugins integrados: automático, o un estilo para todos los plugins |
| `Animaciones`, `Frecuencia de animación` | Animaciones de la interfaz, y con qué frecuencia se mueven el cursor de reproducción y los medidores |
| `Dibujar con la GPU (Direct2D)` | Desactívalo si el driver gráfico falla o la pantalla parpadea |
| Inicio | Pantalla de inicio, sonido de inicio, y búsqueda de plugins nuevos en segundo plano en cada arranque |
| Edición | Teclado del ordenador como piano, mantener abiertos los editores de canal, recordar el tiempo de búsqueda, repetir en bucle los clips de patrón estirados |
| `Niveles máximos de deshacer` | De 25 a 1000 |
| Copia de seguridad | Con qué frecuencia se guarda una copia, cuántas se conservan por proyecto, y `Abrir carpeta de copias de seguridad` |
| `Ejecutar plugins en un proceso aparte` | Un plugin que falla no tumba LYNX SHARE. Desactivado por defecto |
| `Sustituir los plugins que faltan por equivalentes de LYNX SHARE` | Activado por defecto. El proyecto conserva el plugin original |
| `Informes de fallos` | Preguntar tras un fallo o cuelgue (por defecto), enviar siempre o no enviar nunca |
| `Buscar actualizaciones automáticamente` | La comprobación de actualizaciones al arrancar, con `Buscar ahora` |

## Tema

`Opciones › Tema (colores, fondo)...`

| Ajuste | Qué hace |
|---|---|
| Caja de tema | Elige un preset. `Importar...` y `Exportar...` leen y escriben archivos `.lynxtheme`, `Guardar preset...` guarda el tema actual, `Predeterminado` lo restablece |
| `Color de acento`, `Fondo`, `Espacio de trabajo` | Los tres colores de un tema. El color del espacio de trabajo puede seguir al fondo automáticamente |
| Imagen del espacio de trabajo | Un PNG, JPEG o GIF detrás de las ventanas, con `Ajuste de imagen`, `Opacidad de la imagen` y `Atenuar` |
| Sonido de inicio | Un sonido tuyo de 30 segundos o menos, o el propio de LYNX SHARE |

## Archivos

`Opciones › Configuración de archivos (carpetas de búsqueda)...`

| Ajuste | Qué hace |
|---|---|
| Carpetas de búsqueda de samples que faltan | La ventana Archivos que faltan busca por sí sola en estas carpetas y sus subcarpetas |
| Carpetas del Browser | Se muestran en la lista de carpetas del Browser, y también se buscan samples que faltan |
| Carpetas de plugins | `Opciones › Buscar plugins` las escanea en busca de plugins |

## Proyecto

![La página Proyecto: ajustes de tiempo, groove y afinación, base de tiempo y opciones de la Playlist](/assets/img/wiki/settings-project.webp)

`Opciones › Configuración del proyecto (tempo, compás)...`

| Ajuste | Qué hace |
|---|---|
| `Configuración de tiempo` | División del tiempo (tiempos por compás, pasos por tiempo) o un compás tradicional (numerador y denominador) |
| `Tempo`, `Tiempos por compás` | El tempo del proyecto y los tiempos de un compás |
| `Swing`, `Tono maestro` | La cantidad de swing global y el tono master en cents |
| `Fundidos cruzados automáticos` | Los clips de audio que se solapan en una pista hacen crossfade durante el solapamiento |
| `Fundido del comp` | La duración del crossfade al montar tomas, en milisegundos |
| `Reproducir notas truncadas en clips` | Una nota que empieza antes de un clip de patrón suena desde el inicio del clip |
| `Desclic rápido para grupos de corte` | Rampa más rápida cuando los cut groups cortan notas |
| `Ley de paneo` | Circular (centro a 0 dB, +3 dB en el lado al que se hace pan) o Triangular (el lado más fuerte conserva su nivel). Para inserts del Mixer y canales Sampler |

## Información

`Opciones › Información del proyecto...`. Título, autor, género, enlace web y comentarios del proyecto, y `Mostrar esta página al abrir el proyecto`. El pie muestra cuándo se creó el proyecto y el tiempo de trabajo.

## Conviene saber

- La base de tiempo está fijada en 960 ticks por negra. Los proyectos de FL Studio se convierten a ella.
- Los cambios de compás a lo largo de la canción no están en la página Proyecto. Añade un marcador de compás en la regla de la Playlist.
- `Ejecutar plugins en un proceso aparte` se aplica a los plugins que se abran a partir de entonces. Reabre el proyecto para mover los demás.
- Los ajustes de actualización no aparecen en la versión de Microsoft Store, porque la Store la actualiza.
- Los atajos están en `Opciones › Gestionar atajos...`, no en una página de Configuración.

## Relacionado

- [Configuración de audio](/wiki/audio-setup/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Atajos](/wiki/shortcuts/)
- [Plugins](/wiki/plugins/)
- [Disposiciones](/wiki/layouts/)
