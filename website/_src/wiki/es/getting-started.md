---
source: a6d9063ec3
---
LYNX SHARE es una DAW gratuita y de código abierto para Windows, macOS y Linux. Trabaja con patrones: los programas en el Channel Rack y el Piano Roll, los arreglas en la Playlist y los mezclas en el Mixer. Abre proyectos de FL Studio (`.flp`) y sets de Ableton Live (`.als`), y guarda su propio formato `.lynx`.

Es una alfa. Espera errores y guarda a menudo.

## Ponerlo en marcha

1. Mira la tabla de abajo para comprobar que tu sistema es compatible.
2. Descárgalo e instálalo. [Instalar](/wiki/install/) tiene los pasos para cada sistema.
3. Ábrelo. Un breve asistente de configuración elige el idioma, prueba la salida de audio y lista las carpetas de plugins, que luego se escanean en segundo plano. Puedes omitirlo y volver a abrirlo después con `Opciones › Asistente de configuración (idioma, audio, carpetas de plugins)...`.
4. Si no oyes nada, ve a [Configuración de audio](/wiki/audio-setup/).
5. Lee [Recorrido por la ventana](/wiki/tour/) y haz [Tu primera canción](/wiki/first-song/).

## Qué necesitas

| Sistema | Requisito |
|---|---|
| Windows | Windows 10 u 11, de 64 bits |
| macOS | macOS 11 Big Sur o más reciente, Intel o Apple silicon |
| Linux | x86_64 con glibc 2.35 o más reciente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch o Manjaro actuales), X11 o XWayland |

No necesitas una interfaz de audio. LYNX SHARE suena por el dispositivo de sonido que ya usa tu sistema. Tampoco necesitas FL Studio. Si está instalado en el mismo ordenador, LYNX SHARE encuentra sus samples y usa los mismos archivos de plugins para los proyectos `.flp`.

Los plugins de terceros son opcionales. LYNX SHARE tiene sus propios instrumentos y efectos, listados en el [catálogo](/instruments-and-effects/).

## Guarda tu trabajo

`Ctrl+S` guarda, `Ctrl+Shift+S` guarda con un nombre nuevo y `Ctrl+Alt+S` guarda una versión nueva (Song_2, Song_3 y así sucesivamente). Hay un autoguardado, accesible desde `Archivo › Abrir carpeta de autoguardado` y `Archivo › Revertir a la última copia de seguridad...`.

`Archivo › Nuevo desde plantilla` tiene cuatro puntos de partida: Empty, Basic beat, Trap with 808 y House.

## El proyecto de demostración

El instalador y el zip de Windows, el `.dmg` de macOS y el `.tar.gz` de Linux incluyen un proyecto de demostración, `Demo.lynx`. Ábrelo con `Archivo › Abrir...` (`Ctrl+O`). En Windows está en la carpeta de instalación, junto a `LYNX.exe`.

También puedes generarlo desde un terminal. Consulta [Línea de comandos](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Adónde ir después

| Quieres | Lee |
|---|---|
| Aprender cada ventana | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Abrir un proyecto de otro programa | [Si vienes de FL Studio](/wiki/coming-from-fl-studio/), [Si vienes de Ableton Live](/wiki/coming-from-ableton/) |
| Usar tus plugins VST, CLAP, LV2 o AU | [Plugins](/wiki/plugins/) |
| Buscar una tecla | [Atajos](/wiki/shortcuts/) |
| Resolver un problema | [Solución de problemas](/wiki/troubleshooting/) y las [preguntas frecuentes](/faq/) |

## Conviene saber

- El archivo del programa se llama `LYNX.exe` en Windows y `LYNX` en macOS y Linux. La carpeta de configuración también se llama `LYNX`.
- En Windows y macOS, el primer arranque puede mostrar un aviso del sistema, porque la versión de Windows aún no está firmada y la de macOS no está notarizada. [Instalar](/wiki/install/) explica en qué hacer clic.
- La interfaz está en inglés por defecto en un sistema cuyo idioma LYNX SHARE no tiene. Puedes cambiarla en `Opciones › Configuración general...`. Un idioma nuevo se aplica tras reiniciar.
- Los paquetes FLEX de los proyectos de FL Studio están cifrados y nunca se abren. Esos canales reciben un patch de sintetizador elegido según el nombre del preset.

## Relacionado

- [Instalar](/wiki/install/)
- [Configuración de audio](/wiki/audio-setup/)
- [Recorrido por la ventana](/wiki/tour/)
- [Tu primera canción](/wiki/first-song/)
- [Descargar](/download/)
