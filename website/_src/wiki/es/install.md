---
source: 8a120ab7de
---
Cada versión tiene compilaciones para Windows, macOS y Linux en la [página de versiones](https://github.com/Ka1-S/LYNX-SHARE/releases). Los archivos se compilan a partir del código fuente etiquetado con GitHub Actions y aparecen con su SHA-256 en `SHA256SUMS.txt`. La página de [Descargar](/download/) enlaza con las actuales.

## Instalar en Windows

Windows 10 u 11, de 64 bits.

1. Descarga `LYNX-<version>-setup-x64.exe`.
2. Ejecútalo. Se instala solo para tu usuario y no necesita derechos de administrador.
3. SmartScreen puede mostrar un aviso, porque el ejecutable aún no está firmado. Elige `More info` y luego `Run anyway`.
4. Inicia LYNX SHARE desde el menú Inicio o el acceso directo del escritorio.

El instalador hace que LYNX SHARE sea el programa que abre los proyectos `.lynx`. Ofrece LYNX SHARE en *Abrir con* para los archivos `.flp` sin cambiar el programa predeterminado de FL Studio.

Hay otras dos formas:

| Forma | Archivo o lugar | Qué hace |
|---|---|---|
| Zip portátil | `LYNX-<version>-windows-x64.zip` | Extráelo donde quieras y ejecuta `LYNX.exe`. No se instala nada. |
| Microsoft Store | [LYNX SHARE en la Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | La Store lo instala y lo mantiene actualizado. |

## Instalar en macOS

macOS 11 Big Sur o más reciente. Un único `LYNX-<version>-macos-universal.dmg` universal cubre Intel y Apple silicon.

1. Abre el `.dmg` y arrastra LYNX SHARE a *Aplicaciones*.
2. Apple no ha notarizado la app y no tiene firma de Developer ID, así que el primer arranque necesita un paso extra. Haz clic derecho (o Control-clic) en LYNX SHARE dentro de *Aplicaciones*, elige *Abrir* y luego *Abrir* otra vez.
3. En macOS 15 Sequoia o más reciente, intenta abrirla una vez, ve a *Ajustes del Sistema › Privacidad y seguridad* y haz clic en *Abrir igualmente*.

La alternativa con el Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS pide acceso al micrófono la primera vez que grabas.

## Instalar en Linux

x86_64 con glibc 2.35 o más reciente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch o Manjaro actuales). LYNX SHARE necesita X11 o XWayland. El audio pasa por PipeWire, JACK o ALSA, y no hace falta instalar nada más para eso.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Algunas distribuciones necesitan libfuse2 para ejecutar AppImages (`libfuse2` en Ubuntu 22.04, `libfuse2t64` en 24.04, `fuse2` en Arch, `fuse-libs` en Fedora). En el primer arranque, el asistente de configuración ofrece añadir LYNX SHARE al menú de aplicaciones y abrir con él los archivos `.lynx`. Puedes cambiarlo después en `Opciones › Configuración general...`, o desde un terminal con `./LYNX-*.AppImage --menu-entry on` (u `desactivado`).

Si prefieres no instalar libfuse2, usa `LYNX-<version>-linux-x86_64.tar.gz`: extráelo y ejecuta `./install.sh`. Eso instala LYNX SHARE para tu usuario en `~/.local` con una entrada de menú, un icono y el tipo de archivo `.lynx`.

## Dónde se guarda la configuración

| Sistema | Carpeta de configuración |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (o dentro de `$XDG_CONFIG_HOME`) |

La carpeta contiene tus opciones, el idioma elegido (`language.txt`), tus atajos (`keys.json`), tus carpetas de plugins (`plugin-folders.txt`) y la lista de plugins escaneados (`plugins.xml`).

## Actualizar

Al arrancar, LYNX SHARE pregunta a GitHub por el número de la versión más reciente. No se envía nada sobre ti. Cuando hay una versión nueva, una barra muestra `LYNX SHARE <version> is available` con un botón `Novedades` y un botón de actualizar. `Ayuda › Buscar actualizaciones...` pregunta en cualquier momento.

| Compilación | Cómo se actualiza |
|---|---|
| Instalador de Windows | `Actualizar` descarga el instalador nuevo, pregunta por los cambios sin guardar, instala y reinicia |
| Zip portátil de Windows, macOS | `Actualizar` descarga el archivo nuevo y lo muestra en su carpeta |
| AppImage de Linux | `Actualizar` actualiza el archivo AppImage en su sitio. Gear Lever y AppImageUpdate pueden hacer lo mismo desde fuera |
| `.tar.gz` de Linux | `Descargar...` abre la página de descarga |
| Microsoft Store | La Store lo actualiza. Esta compilación no tiene su propia comprobación de actualizaciones |

Para desactivar la comprobación, abre `Opciones › Configuración general...` y desmarca `Buscar actualizaciones automáticamente`.

## Desinstalar

- Instalador de Windows: desinstala LYNX SHARE desde la lista *Aplicaciones y características* de Windows.
- Zip portátil: borra la carpeta que extrajiste.
- `.tar.gz` de Linux: ejecuta `./install.sh --uninstall`.

## Conviene saber

- Las compilaciones de Windows no están firmadas hasta que se acepte la solicitud de firma de código del proyecto. La app de macOS solo tiene una firma ad hoc. Los paquetes de Linux no están firmados.
- Para comprobar una descarga, compara su SHA-256 con la línea de `SHA256SUMS.txt` en la página de la versión.
- El instalador, el zip, el `.dmg` y el `.tar.gz` incluyen el proyecto de demostración `Demo.lynx`.
- Cerrar la barra de actualización con su botón de cerrar omite esa versión.

## Relacionado

- [Empieza aquí](/wiki/getting-started/)
- [Configuración de audio](/wiki/audio-setup/)
- [Solución de problemas](/wiki/troubleshooting/)
- [Descargar](/download/)
- [Actualizaciones](/updates/)
