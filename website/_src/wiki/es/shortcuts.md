---
source: 98cfcfdbfe
---
![La ventana Gestionar atajos: una lista con búsqueda de acciones con las teclas de cada una](/assets/img/wiki/shortcuts.webp)

Estas son las teclas por defecto de la 0.1.26 alfa. `Ayuda › Atajos` (`F1`) muestra la hoja de atajos dentro de la app. `Opciones › Gestionar atajos...` abre el editor de la imagen, donde los cambias.

> En macOS la app muestra las teclas con nombres de Mac: Ctrl se muestra como ⌘ y Alt como ⌥.

## Cambiar un atajo

1. Elige `Opciones › Gestionar atajos...`.
2. Escribe una acción o una tecla, como `Ctrl+L`, en el cuadro de búsqueda. La lista de al lado limita la búsqueda a un lugar, y `Solo modificados` muestra los que has cambiado.
3. Haz clic en la acción.
4. Haz clic en `Cambiar` para reemplazar sus teclas, o en `Añadir atajo` para darle una segunda, y luego pulsa las teclas nuevas. `Esc` cancela.
5. Si las teclas ya se usan en el mismo lugar, un mensaje nombra la otra acción y te deja moverlas.

`Quitar` borra las teclas de la acción. `Valores por defecto` restablece una acción y `Restablecer todo...` las restablece todas. Tus cambios se guardan en `keys.json` en la carpeta de configuración (`%APPDATA%\LYNX` en Windows, `~/Library/Application Support/LYNX` en macOS, `~/.config/LYNX` en Linux). `Abrir keys.json` abre ese archivo.

## Transporte y grabación

| Tecla | Qué hace |
|---|---|
| `Space` | Reproduce, o detiene y vuelve al punto donde empezó la reproducción |
| `Ctrl+Space` | Reproducir o pausar |
| `Shift+Space` | Reproduce, o se detiene al inicio de la canción o del patrón |
| `L` | Cambia entre el modo Patrón y el modo Canción |
| `R` | Activa o desactiva la grabación |
| `Shift+R` | Graba audio ahora: arma el insert seleccionado y graba |
| `Ctrl+M` | Activa o desactiva el metrónomo |
| `Ctrl+P` | Activa o desactiva la cuenta previa antes de grabar |
| `Ctrl+I` | Activa o desactiva que la grabación espere a la primera nota |
| `Ctrl+B` | Activa o desactiva mezclar las notas grabadas |
| `Ctrl+E` | Activa o desactiva la edición por pasos |
| `Ctrl+H` | Detiene el sonido (all notes off) |
| `Ctrl+T` | Activa o desactiva el teclado de escritura como piano |
| `Alt+Numpad *` / `Alt+Numpad /` | Salta al marcador de canción siguiente o anterior |

Algunas de estas teclas hacen otra cosa dentro de un editor que tiene el foco. En el Piano Roll, `Ctrl+M` importa un archivo MIDI y `Ctrl+B` duplica. En la Playlist, `Ctrl+P` es el modo Performance.

## Archivos y edición

| Tecla | Qué hace |
|---|---|
| `Ctrl+N` | Nuevo proyecto |
| `Ctrl+O` | Abrir proyecto |
| `Ctrl+S` | Guardar |
| `Ctrl+Shift+S` | Guardar como |
| `Ctrl+Alt+S` | Guardar nueva versión |
| `Ctrl+R` | Exportar audio |
| `Ctrl+Shift+R` | Exportar un archivo MP3 |
| `Ctrl+Shift+M` | Exportar un archivo MIDI |
| `Ctrl+Z` | Deshacer. Si se pulsa otra vez, rehace ese paso |
| `Ctrl+Alt+Z` | Deshacer hacia atrás, paso tras paso |
| `Ctrl+Y` o `Ctrl+Shift+Z` | Rehacer |
| `Ctrl+Alt+H` | Historial de deshacer |
| `Ctrl+F` | Buscar en el Browser |

## Ventanas

| Tecla | Qué hace |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Selector de plugins |
| `Shift+F8` | Base de plugins (en el Browser) |
| `Enter` | Maximiza o restaura la Playlist |
| `F10` | Configuración MIDI |
| `F11` | Información del proyecto |
| `F12` | Cierra todas las ventanas (plugins, ajustes de canal, herramientas) |
| `Alt+F12` | Cierra todas las ventanas de plugins |
| `Ctrl+F12` | Cierra todas las ventanas menos la que tiene el foco |
| `Alt+F7` | Teclado táctil |
| `Ctrl+F5` | Tap tempo |
| `F1` | Hoja de atajos |

## Clip View y Device View

Estas teclas funcionan solo en el flujo por lanzamiento de clips.

| Tecla | Qué hace |
|---|---|
| `Ctrl+Alt+L` | Muestra u oculta la vista de detalle |
| `Ctrl+Alt+3` | Clip View |
| `Ctrl+Alt+4` | Device View |
| `Shift+Tab` | Cambia entre Clip View y Device View |
| `Left` / `Right` | Device View: selecciona el dispositivo anterior, siguiente |
| `Del` o `Backspace` | Device View: elimina el efecto seleccionado |

## Patrones

| Tecla | Qué hace |
|---|---|
| `Numpad +` / `Numpad -` | Patrón siguiente, anterior |
| `Numpad 1` a `Numpad 9` | Selecciona el patrón 1 a 9 |
| `F4` | Siguiente patrón vacío, y le pone nombre |
| `Ctrl+F4` | Siguiente patrón vacío, sin nombrarlo |
| `Shift+F4` | Primer patrón vacío, y le pone nombre |
| `F2` | Renombra y colorea el patrón |
| `Ctrl+Shift+C` | Clona el patrón |
| `Ctrl+Shift+Ins` | Inserta un patrón vacío antes del actual |
| `Ctrl+Shift+Del` | Elimina el patrón |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Mueve el patrón hacia arriba, abajo |

## Channel Rack

| Tecla | Qué hace |
|---|---|
| `Up` / `Down` | Selecciona el canal de arriba, de abajo |
| `Shift+Up` / `Shift+Down` | Amplía la selección |
| `Alt+Up` / `Alt+Down` | Mueve los canales seleccionados |
| `Ctrl+A` | Selecciona todos los canales |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Corta, copia, pega los pasos de los canales seleccionados |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rota los pasos a la izquierda, a la derecha |
| `Alt+C` | Clona los canales seleccionados |
| `Alt+Del` | Elimina los canales seleccionados |
| `Alt+G` | Agrupa los canales seleccionados |
| `Ctrl+L` | Envía los canales seleccionados a inserts libres del Mixer |
| `Ctrl+K` | Activa o desactiva los carriles de gráfico de pasos |
| `1` a `0` | Silencia o activa los canales 1 a 10 |
| `Ctrl+1` a `Ctrl+0` | Pone en solo los canales 1 a 10 |

## Piano Roll

| Tecla | Qué hace |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Lápiz, Pincel, Pincel de batería, Borrador, Silenciar, Cuchilla, Lazo, Zoom, Escuchar |
| `S` | Activa o desactiva las notas slide (notas nuevas) |
| `G` / `K` | Canal anterior, siguiente |
| `H` / `J` | Canal anterior, siguiente con notas |
| `Ctrl+A` | Seleccionar todo |
| `Ctrl+D` o `Esc` | Deseleccionar |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copiar, cortar, pegar |
| `Ctrl+B` | Duplicar |
| `Del` | Elimina la selección |
| `Up` / `Down` | Mueve la selección un semitono |
| `Ctrl+Up` / `Ctrl+Down` | Mueve la selección una octava |
| `Left` / `Right` | Mueve la selección un paso de la cuadrícula |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rota las notas dentro del compás |
| `Backspace` | Desactiva o activa el ajuste |
| `Ctrl+Q` | Cuantización rápida (inicios y longitudes) |
| `Shift+Q` | Cuantiza los inicios |
| `Ctrl+U` | Trocea según la cuadrícula |
| `Ctrl+G` | Une las notas que se tocan |
| `Ctrl+L` | Legato |
| `Shift+D` | Acorta las notas a la cuadrícula |
| `Ctrl+P` | Activa o desactiva porta en la selección |
| `Alt+M` / `Shift+Alt+M` | Silencia, activa la selección |
| `Alt+V` | Activa o desactiva las notas fantasma |
| `Shift+I` | Invierte la selección |
| `Shift+G` / `Alt+G` | Agrupa, desagrupa las notas seleccionadas |
| `Alt+C` | Colorea las notas seleccionadas con el color de nota |
| `Shift+C` | Selecciona las notas del mismo color |
| `Shift+N` | Sello de acorde: ninguno (notas sueltas) |
| `Ctrl+Ins` / `Ctrl+Del` | Inserta, elimina espacio (la selección de tiempo) |
| `Ctrl+M` | Importa un archivo MIDI |
| `Ctrl+Shift+M` | Exporta como archivo MIDI |
| `PageUp` / `PageDown` | Acerca, aleja |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom a 1, 4, 16 compases |
| `Shift+4` | Zoom para mostrar todo |
| `Shift+5` | Zoom a la selección |
| `Shift+0` | Centra la vista en el cursor de reproducción |

### Herramientas de notas

Cada tecla abre el cuadro de diálogo de una herramienta de notas en el Piano Roll.

| Tecla | Qué hace |
|---|---|
| `Alt+A` | Arpegiar |
| `Alt+S` | Rasgueo |
| `Alt+Y` | Invertir |
| `Alt+R` | Aleatorizar |
| `Alt+X` | Escalar niveles |
| `Alt+K` | Limitar a un rango |
| `Alt+Q` | Cuantizar |
| `Alt+U` | Trocear |
| `Alt+F` | Flam |
| `Alt+W` | Máquina de garra |
| `Alt+O` | LFO |
| `Alt+L` | Articular |
| `Alt+E` | Máquina de riffs |
| `Alt+P` | Generar progresión de acordes |
| `Ctrl+Alt+Y` | Ejecuta de nuevo el último script |

## Playlist

| Tecla | Qué hace |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Lápiz, Pincel, Borrador, Silenciar, Cuchilla, Lazo, Zoom, Escuchar, Slip |
| `Ctrl+A` | Seleccionar todo |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copiar, cortar, pegar |
| `Ctrl+B` | Duplicar |
| `Del` | Elimina los clips seleccionados |
| `Shift+Del` | Elimina los clips seleccionados y cierra los huecos |
| `Shift+Left` / `Shift+Right` | Mueve los clips seleccionados un paso de ajuste a la izquierda, a la derecha |
| `Shift+Up` / `Shift+Down` | Mueve los clips seleccionados una pista hacia arriba, hacia abajo |
| `Alt+Left` / `Alt+Right` | Empuja los clips seleccionados a la izquierda, a la derecha con un paso fino, sin ajuste. `Shift+Alt` hace lo mismo |
| `Ctrl+D` | Deselecciona clips y tiempo |
| `Shift+I` | Invierte la selección |
| `Shift+C` | Selecciona los clips de la fuente seleccionada |
| `Shift+S` | Activa o desactiva la edición con desplazamiento |
| `Backspace` | Desactiva o activa el ajuste |
| `Alt+M` / `Shift+Alt+M` | Silencia, activa los clips seleccionados |
| `Shift+M` | Activa o desactiva estirar el audio al cambiar el tamaño de clips de audio |
| `Ctrl+Shift+V` | Hace únicos los clips seleccionados |
| `Ctrl+G` | Fusiona clips de patrón |
| `Ctrl+Shift+G` | Fusiona clips de patrón similares |
| `Ctrl+Alt+G` | Fusiona clips de automatización |
| `Ctrl+Shift+B` | Clona las pistas de la Playlist seleccionadas |
| `Ctrl+Q` | Cuantiza los inicios de los clips |
| `7` / `8` | Baja, sube un semitono los clips de audio seleccionados |
| `9` | Invierte los clips de audio seleccionados |
| `Ctrl+Ins` / `Ctrl+Del` | Inserta, quita la longitud de la selección de tiempo en todas las pistas |
| `Ctrl+Alt+Ins` | Corta los clips e inserta espacio |
| `Ctrl+Alt+Home` | Activa o desactiva el cambio de tamaño de clips desde la izquierda |
| `Ctrl+Left` / `Ctrl+Right` | Selecciona el tiempo anterior, siguiente |
| `Ctrl+Enter` | Selecciona el tiempo alrededor de la selección |
| `Alt+T` | Añade un marcador de tiempo |
| `Ctrl+T` | Añade un marcador automáticamente |
| `Shift+Alt+T` | Añade un cambio de compás |
| `Shift+T` | Coloca un marcador de bucle |
| `Ctrl+Alt+C` | Consolida la selección de la Playlist |
| `Ctrl+P` | Activa o desactiva el modo Performance |
| `PageUp` / `PageDown` | Acerca, aleja |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom a 1, 4, 16 compases |
| `Shift+4` | Zoom para mostrar todo |
| `Shift+5` | Zoom a la selección |
| `Shift+0` | Centra la vista en el cursor de reproducción |

## Mixer

| Tecla | Qué hace |
|---|---|
| `Alt+Left` / `Alt+Right` | Mueve los inserts seleccionados a la izquierda, a la derecha |
| `F2` | Renombra y colorea el insert seleccionado |
| `S` | Pone en solo los inserts seleccionados |
| `Alt+S` | Pone en solo los inserts seleccionados junto con los inserts que los alimentan |
| `Ctrl+L` | Vincula los canales seleccionados al insert seleccionado |
| `Ctrl+Shift+L` | Vincula los canales seleccionados a inserts a partir del seleccionado |
| `Alt+L` | Selecciona los canales vinculados al insert seleccionado |
| `Del` | Elimina los inserts seleccionados. LYNX SHARE pregunta antes |
| `Ctrl+E` | Abre el editor de audio |
| `Shift+E` | Abre el editor de audio y graba |
| `Alt+R` | Renderiza los inserts armados a archivos WAV |
| `Shift+Alt+R` | Renderiza los inserts seleccionados a archivos WAV |

## Browser

| Tecla | Qué hace |
|---|---|
| `Up` / `Down` | Archivo anterior, siguiente (con preescucha) |
| `Shift+Up` / `Shift+Down` | Archivo anterior, siguiente en el canal seleccionado |
| `Enter` | Carga el archivo en el canal seleccionado |
| `Left` | Cierra la carpeta, o va a la carpeta superior |
| `Right` | Abre la carpeta |
| `F2` | Renombra el plugin seleccionado (base de plugins) |

## Conviene saber

- Mientras el piano del teclado de escritura está activado, sus teclas tocan notas y la mayoría de los atajos de una sola letra quedan en pausa (`L` sigue funcionando). Desactívalo con `Ctrl+T`.
- Un atajo funciona en todas partes o en una sola ventana. El que pertenece a una ventana gana a las mismas teclas que funcionan en todas partes, mientras esa ventana tiene el foco.
- Algunas acciones no tienen tecla por defecto, por ejemplo la paleta de inserción rápida. Asígnales una en el editor.
- En la Playlist, las teclas que mueven clips mueven todos los clips cuando no hay ninguno seleccionado. El paso fino de `Alt+Left` y `Alt+Right` es un píxel con el zoom actual.
- Los atajos de LYNX SHARE también funcionan mientras una ventana de plugin tiene el foco, por ejemplo `Space`. Las letras y los dígitos sueltos van primero al plugin, y `Esc` cierra su ventana.

## Relacionado

- [Recorrido por la ventana](/wiki/tour/)
- [Configuración](/wiki/settings/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
