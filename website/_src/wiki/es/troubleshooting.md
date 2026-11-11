---
source: a3748d4a2a
---
Cada título de abajo es un problema. Si el tuyo no está, infórmalo como se describe al final.

## Al iniciar el programa

### Windows: SmartScreen avisa en el primer arranque

El ejecutable aún no está firmado, así que SmartScreen puede mostrar un aviso la primera vez que lo ejecutas.

1. Haz clic en `More info`.
2. Haz clic en `Run anyway`.

El instalador no necesita derechos de administrador.

### macOS: "no se puede abrir"

Apple no ha notarizado la app y no tiene firma de Developer ID, así que el primer arranque necesita un paso extra.

1. Arrastra LYNX SHARE a `Applications`.
2. Haz clic derecho (Control-clic) y elige `Abrir`.
3. Haz clic en `Abrir` otra vez.

En macOS 15 Sequoia o más reciente, intenta abrirla una vez, ve a `System Settings › Privacy & Security` y haz clic en `Open Anyway`. O, en el Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS pide acceso al micrófono la primera vez que grabas.

### Linux: el AppImage no arranca

LYNX SHARE necesita un sistema x86_64 con glibc 2.35 o más reciente (Ubuntu 22.04, Debian 12, Fedora 36, Arch o Manjaro actuales) y X11 o XWayland.

1. Haz el archivo ejecutable y lánzalo, con los dos comandos de abajo.
2. Si sigue sin arrancar, instala libfuse2. El paquete es `libfuse2` en Ubuntu 22.04, `libfuse2t64` en Ubuntu 24.04, `fuse2` en Arch y `fuse-libs` en Fedora.
3. Si prefieres no instalarlo, descarga en su lugar el `.tar.gz`, extráelo y ejecuta `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` instala LYNX SHARE para tu usuario en `~/.local` con una entrada de menú. `./install.sh --uninstall` lo quita.

## Sonido

### No hay sonido, o el dispositivo de audio es el equivocado

1. Abre `Opciones › Configuración de audio...`.
2. Elige un driver.
3. Elige un dispositivo de salida.

| Sistema | Drivers |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO y el driver LYNX ASIO integrado |
| macOS | Core Audio |
| Linux | PipeWire (nativo), JACK y ALSA. En el primer arranque, LYNX SHARE abre el primero de estos que tenga dispositivos |

ASIO es un modelo de driver de Windows y existe solo en Windows. `Opciones › Asistente de configuración (idioma, audio, carpetas de plugins)...` también prueba el dispositivo de audio. Consulta [Configuración de audio](/wiki/audio-setup/).

### Linux: el sonido se corta

El hilo de audio debería ejecutarse con prioridad de tiempo real.

1. Abre `Opciones › Configuración de audio...` y lee `Estado`. Muestra si el hilo obtuvo prioridad de tiempo real.
2. Si no la obtuvo, instala rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` o `sudo pacman -S rtkit`), o añádete al grupo `audio` con un límite de tiempo real, como en las configuraciones de JACK.
3. Cierra sesión y vuelve a iniciarla.

Los detalles están en [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## Plugins

### Un plugin falla

Ejecútalo en su propio proceso, para que solo se detenga el plugin.

1. Abre `Opciones › Configuración general...`.
2. Activa `Ejecutar plugins en un proceso aparte (protege a LYNX SHARE de los cierres inesperados)`.
3. Vuelve a abrir el proyecto.

Un plugin que falló o dejó de responder en su propio proceso queda silenciado. Guarda el proyecto y ábrelo de nuevo para cargar otra vez el plugin. Consulta [Plugins de terceros](/wiki/plugins/).

### Un plugin no está en la lista

1. Elige `Opciones › Buscar plugins`.
2. Si sigue sin aparecer, abre `Opciones › Gestor de plugins...`, haz clic en `Carpetas de plugins...` y añade la carpeta del plugin.
3. Mira la pestaña `Con errores`. Un archivo de plugin que falla en el escaneo se omite. En Windows, el motivo está en `%APPDATA%\LYNX\scan-failures.log`.
4. Haz clic en `Reescanear todo...` para volver a intentarlo con los archivos que fallaron.

### Un proyecto usa un plugin que no está instalado

Un efecto se sustituye por un efecto de LYNX SHARE con la misma función, y sus ajustes se conservan en el proyecto. `Ayuda › Archivos que faltan...` lista lo que falta. Consulta [Instrumentos y efectos integrados](/wiki/built-in/).

### El vídeo no funciona en Linux

Las pistas de vídeo y `Export with video` necesitan FFmpeg. LYNX SHARE indica cómo instalarlo.

## Informaciónrmes de fallos

![La pregunta del informe de fallos que aparece en el siguiente arranque, con Enviar, Mostrar lo que se enviará y No enviar](/assets/img/wiki/crash-report.webp)

Los informes de fallos son opcionales. Tras un fallo o un bloqueo, el siguiente arranque pregunta si quieres enviar un informe.

| Comando | Qué hace |
|---|---|
| `Mostrar lo que se enviará` | Muestra el texto exacto |
| `Informar en GitHub en su lugar` | Abre el formulario de incidencias de GitHub, ya rellenado. Los archivos los adjuntas tú |
| `Abrir la carpeta con el informe` | Muestra los archivos del informe en tu ordenador |
| `Include file names (projects, samples)` | Desactivado, solo se envía la extensión de cada archivo |
| `Enviar siempre sin preguntar` | Envía los informes futuros sin preguntar |

`Informes de fallos` en `Opciones › Configuración general...` fija `Ask` (el valor por defecto), `Enviar siempre` o `No enviar nunca`.

Un informe contiene la versión de LYNX SHARE, el sistema operativo, la CPU, las tarjetas gráficas y sus drivers, el tamaño de pantalla, el driver y dispositivo de audio, el archivo del fallo o bloqueo, los últimos 60 pasos de la sesión y los nombres y versiones de los plugins de terceros que se cargaron.

No contiene audio, proyectos, samples, el nombre de tu ordenador ni tu nombre de usuario. El servidor no guarda ninguna dirección IP. Los ids enviados se listan en `crash-reports\sent.log` en la carpeta de configuración. Para que se borre un informe, abre una incidencia con su id. El texto completo está en la [política de privacidad](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Informaciónrmar de un problema

![La ventana Informar de un problema: un cuadro de descripción, una dirección de correo opcional y el botón Enviar](/assets/img/wiki/report-problem.webp)

1. Elige `Ayuda › Informar de un problema...`.
2. Describe lo que pasó. La dirección de correo es opcional, por si quieres una respuesta.
3. Deja marcado `Adjuntar los archivos de fallo / bloqueo más recientes y los pasos de esta sesión` si el problema fue un fallo.
4. Haz clic en `Mostrar lo que se enviará` para revisar el texto.
5. Haz clic en `Enviar`. No se envía nada antes de eso.

También puedes abrir una incidencia en [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Conviene saber

- Tras un fallo, las copias de seguridad de tu proyecto están en la carpeta de autoguardado de LYNX SHARE.
- Cuando un plugin intervino en el fallo, la pregunta ofrece ejecutar ese plugin en un proceso aparte a partir de ahora.
- La carpeta de configuración es `%APPDATA%\LYNX` en Windows, `~/Library/Application Support/LYNX` en macOS y `~/.config/LYNX` en Linux.

## Relacionado

- [Configuración de audio](/wiki/audio-setup/)
- [Plugins de terceros](/wiki/plugins/)
- [Instalar](/wiki/install/)
- [Preguntas frecuentes](/faq/)
- [Protección contra fallos](/why/#crash)
