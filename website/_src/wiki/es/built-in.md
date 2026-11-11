---
source: 7ca5e689db
---
![La paleta de inserción rápida: instrumentos a la izquierda, efectos a la derecha, cada uno marcado como integrado o motor FL integrado](/assets/img/wiki/quickadd.webp)

LYNX SHARE incluye sus propios instrumentos y efectos, así que un proyecto suena sin ningún plugin de terceros. Hay tres tipos: originales de LYNX SHARE (un sintetizador, un sampler, un reproductor de SoundFont, baterías, ecualizadores, compresores, limitadores, reverbs, delays), motores escritos para tocar los ajustes de los plugins nativos de FL Studio, y efectos que sustituyen a plugins de terceros conocidos.

Cada uno tiene su propia página en el [catálogo](/instruments-and-effects/). `Ayuda › Plugins integrados (lista en línea)...` abre la lista con el estado de cada motor.

## Añadir un instrumento y un efecto

1. Pulsa `F6` para mostrar el Channel Rack.
2. Haz clic en el cuadro `+ añadir canal` bajo la última fila y elige un instrumento. El menú los agrupa por categoría.
3. Pulsa `F9` para mostrar el Mixer y selecciona el insert por el que suena el canal.
4. Haz clic en una ranura vacía del panel de efectos y elige un efecto. Se abre su ventana.
5. Haz clic en el nombre de la ranura para ocultar o volver a mostrar esa ventana.

## El selector de plugins

Pulsa `F8` para abrir el selector de plugins. Cubre la pantalla y muestra cada generador y efecto como una imagen de su propio editor, con las categorías como pestañas en la parte inferior.

| Comando | Tecla | Qué hace |
|---|---|---|
| Buscar | Escribir | Atenúa los plugins que no coinciden |
| Recorrer los plugins | `Up` / `Down` | Mueve el resaltado |
| Añadir | `Enter`, doble clic o clic derecho | Un generador se convierte en un canal nuevo. Un efecto va al insert del Mixer seleccionado |
| Arrastrar una imagen | | Al Channel Rack para un canal nuevo, a un canal para reemplazar su instrumento, o a un insert o ranura del Mixer |
| Cerrar | `Esc` o `F8` | Cierra el selector sin añadir nada |

Las imágenes de los plugins propios de LYNX SHARE las dibuja LYNX SHARE en segundo plano. Un plugin de terceros muestra un mosaico liso con el nombre de su fabricante hasta que se abre su editor una vez.

`Editar › Paleta de inserción rápida...` abre en su lugar una lista sencilla: instrumentos a la izquierda, efectos a la derecha, filtrados mientras escribes. `Tab` cambia de lista y `Enter` añade el elemento resaltado. La base de plugins del [Browser](/wiki/browser/) (`Shift+F8`) lista los mismos plugins en forma de árbol.

![La base de plugins del Browser con la carpeta de motores de FL abierta](/assets/img/wiki/browser-plugins.webp)

## Presets

La ventana de un efecto integrado tiene una barra de presets en la cabecera.

| Control | Qué hace |
|---|---|
| Flechas | Preset anterior y siguiente |
| Nombre del preset | Abre el menú: `Presets de LYNX SHARE`, `Tus presets`, `Presets de FL Studio` donde existan, `Guardar preset...` y `Mostrar la carpeta de presets` |
| `A` / `B` | Dos ajustes para comparar. El botón a su lado copia el que oyes al otro |

El nombre del preset y los ajustes A y B se guardan con el proyecto.

## Qué significa "abre los ajustes de FL"

Los plugins nativos de FL Studio nunca se cargan. LYNX SHARE tiene su propio motor para cada uno, y ese motor lee los ajustes que guardó el plugin de FL. Funciona en dos sitios:

- Un proyecto `.flp`. Cada generador y efecto de FL se abre con su motor de LYNX SHARE y los ajustes guardados, y sus controles siguen siendo editables y automatizables.
- Un preset `.fst`. Haz doble clic en un preset de sintetizador en el Browser para crear un canal nuevo, o arrastra un preset de efecto a un insert del Mixer. Haz clic derecho en una ranura de efecto ocupada para ver `Presets de FL`, que lista los presets encontrados en tu instalación de FL Studio.

Lo cerca que está cada motor del original se indica por motor, de Exact a Placeholder. Los niveles se explican en [Si vienes de FL Studio](/wiki/coming-from-fl-studio/).

## Motores que necesitan tu instalación de FL Studio

LYNX SHARE no contiene código, audio ni datos de ondas de Image-Line. Algunos motores necesitan las ondas, samples, impulsos o tablas que pertenecen al plugin original, y las leen de tu propia instalación de FL Studio mientras funcionan.

| Tipo | Motores |
|---|---|
| Instrumentos | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Efectos | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Si no se encuentran los archivos, añadir uno de estos motores puede fallar con un mensaje que dice que no se encontraron o no se pudieron leer los archivos de datos de una instalación de FL Studio.

## Sustitutos de plugins de terceros

Cuando un proyecto usa un efecto de terceros que no está instalado, un efecto de LYNX SHARE con la misma función ocupa su ranura: una reverb para una reverb, un delay para un delay.

- La ranura muestra el nombre del plugin original con una insignia de LYNX SHARE.
- Si se puede leer el estado guardado, sus ajustes se traducen. Si no, el sustituto parte de un preset por defecto.
- El plugin original y sus ajustes se quedan en el proyecto. Cuando instales el plugin, el proyecto se abrirá con él otra vez.
- Haz clic derecho en una ranura con sustituto y elige `Omitir todos los sustitutos de LYNX SHARE de este proyecto` para oír el proyecto sin ellos.

El interruptor es `Sustituir los plugins que faltan por equivalentes de LYNX SHARE` en `Opciones › Configuración general...`, activado por defecto. En un proyecto nuevo, los mismos efectos están en el menú de la ranura vacía, en `Equivalentes de LYNX SHARE`.

## Conviene saber

- Los instrumentos que faltan todavía no se sustituyen. Un plugin de instrumento que falta se convierte en un sintetizador integrado elegido según el nombre del canal.
- La automatización de los parámetros del plugin original se conserva, pero no controla al sustituto.
- Los paquetes FLEX están cifrados y nunca se abren. Un canal FLEX recibe un patch de sintetizador elegido según el nombre de su preset.
- Un insert tiene 10 ranuras de efecto.

## Relacionado

- [Catálogo de instrumentos y efectos](/instruments-and-effects/)
- [Plugins de terceros](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Si vienes de FL Studio](/wiki/coming-from-fl-studio/)
