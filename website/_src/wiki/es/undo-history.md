---
source: 9f36c60cc7
---
![La ventana del historial de deshacer: seis pasos de Start a Add notes, con Mute: Kick como el actual](/assets/img/wiki/history.webp)

Cada edición del proyecto es un paso del historial de deshacer: una nota que añades, un clip que mueves, un fader, un cambio de enrutado, el tempo. `Editar › Historial de deshacer...` (`Ctrl+Alt+H`) muestra los pasos como una lista. Aparte, LYNX SHARE guarda copias de seguridad del proyecto mientras trabajas.

## Volver a un paso anterior

1. Elige `Editar › Historial de deshacer...`, o pulsa `Ctrl+Alt+H`.
2. La lista muestra los pasos con el más reciente abajo. La primera fila, `Inicio`, es el proyecto tal como se abrió.
3. Haz clic en un paso. El proyecto vuelve al estado justo después de él, y los pasos posteriores se ponen grises.
4. Haz clic en un paso gris para avanzar de nuevo.
5. Haz una edición nueva para continuar desde el paso que elegiste. Los pasos grises desaparecen entonces.

## Las teclas de deshacer

`Ctrl+Z` no funciona como en la mayoría de los programas. Deshace el último paso y, si lo pulsas otra vez, lo rehace, así que alterna entre los dos últimos estados. Para retroceder más, usa `Ctrl+Alt+Z`.

| Comando | Tecla | Qué hace |
|---|---|---|
| `Deshacer` | `Ctrl+Z` | Deshace el último paso. Tras un deshacer, rehace el siguiente paso, y el elemento del menú dice `Deshacer (rehacer)` |
| `Deshacer hacia atrás` | `Ctrl+Alt+Z` | Siempre retrocede, un paso por pulsación |
| `Rehacer` | `Ctrl+Y` o `Ctrl+Shift+Z` | Avanza |
| `Historial de deshacer...` | `Ctrl+Alt+H` | Abre la lista |

Después de cada deshacer o rehacer, la barra de ayuda nombra el paso y muestra el nivel, por ejemplo `Level 2/3`.

Si prefieres el comportamiento habitual, activa `Modo de deshacer alternativo (deshacer clásico, en lugar de deshacer/rehacer con el mismo atajo)` en `Opciones › Configuración general...`. Entonces `Ctrl+Z` siempre retrocede.

## Qué es un paso y qué no

- Las ediciones del proyecto son pasos: notas, patrones, clips, canales, niveles y enrutado del mixer, efectos, automatización, tempo, silenciados.
- Una grabación es un paso. También lo es fusionar una parte en una [sesión online](/wiki/online-sessions/), y `Crear comp con estas tomas`.
- Lo que no cambia el proyecto no son pasos: hacer zoom, desplazarse, mover ventanas y los ajustes propios del programa.
- Guardar y exportar no se deshacen. Los archivos que escribieron se quedan.
- Las ventanas de plugins propios de LYNX SHARE tienen un deshacer y rehacer aparte para los cambios de ese plugin, junto a los ajustes A y B.

## Configuración

Los dos están en `Opciones › Configuración general...`.

| Ajuste | Qué hace |
|---|---|
| `Niveles máximos de deshacer` | Cuántos pasos se conservan: 25, 50, 100, 200, 500 o 1000. Por defecto, 100 |
| `Modo de deshacer alternativo` | Hace de `Ctrl+Z` un deshacer clásico |

## Copias de seguridad

LYNX SHARE guarda por defecto una copia de seguridad del proyecto abierto cada 2 minutos, y conserva las 10 más recientes por proyecto. Una copia es un archivo `.lynx` normal en la carpeta `autosave` dentro de la carpeta de configuración, con el nombre del proyecto más `(autosaved on ...)` y la fecha y hora.

Cada vez que guardas sobre un archivo de proyecto existente, la versión que se reemplaza se copia a la misma carpeta con `(overwritten on ...)` en su nombre.

| Comando | Qué hace |
|---|---|
| `Archivo › Revertir a la última copia de seguridad...` | Reemplaza el proyecto abierto por su copia más reciente, tras preguntar. El archivo del proyecto solo cambia cuando guardas |
| `Archivo › Abrir carpeta de autoguardado` | Muestra las copias en el gestor de archivos |
| `Archivo › Guardar nueva versión` (`Ctrl+Alt+S`) | Guarda como `Song_2`, `Song_3` y así sucesivamente, junto al archivo actual |

La sección `Copia de seguridad` de `Opciones › Configuración general...` tiene estos ajustes:

| Ajuste | Qué hace |
|---|---|
| `Guardar una copia de seguridad` | `Nunca`, o un intervalo en minutos |
| `Copias de seguridad por proyecto` | 1, 3, 5, 10, 20 o 50 |
| `Esperar para hacer copias de seguridad mientras suena la canción o se toca un teclado MIDI` | Retiene una copia para no interrumpirte |
| `Espera máxima entre copias` | De 5 a 60 minutos. Se hace una copia pasado este tiempo incluso mientras tocas |

Si la última sesión terminó de forma inesperada, el siguiente arranque ofrece abrir la copia más reciente que guardó esa sesión.

## Conviene saber

- El historial no se guarda con el proyecto. Al abrir un proyecto o empezar uno nuevo, se vacía.
- Cuando el historial se llena, se descartan los pasos más antiguos.
- En una sesión online en vivo, deshacer y rehacer solo recuperan tus propias ediciones, nunca las de otras personas.
- Con `Guardar una copia de seguridad` en `Nunca`, las copias que se hacen al guardar sobre un archivo se siguen conservando.
- La carpeta de configuración es `%APPDATA%\LYNX` en Windows, `~/Library/Application Support/LYNX` en macOS y `~/.config/LYNX` en Linux.

## Relacionado

- [Atajos](/wiki/shortcuts/)
- [Configuración](/wiki/settings/)
- [Formatos de archivo](/wiki/file-formats/)
- [Solución de problemas](/wiki/troubleshooting/)
- [Lo que solo hace LYNX SHARE: seguridad ante fallos](/why/#crash)
