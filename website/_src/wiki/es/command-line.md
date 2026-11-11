---
source: 98829adcb8
---
LYNX SHARE también se ejecuta sin abrir su ventana. Una opción renderiza un proyecto a audio, otras lo convierten a un proyecto de FL Studio o a un set de Ableton Live, y varias sirven para hacer pruebas. Las comprobaciones del propio proyecto usan las mismas opciones.

El programa es `LYNX.exe` en Windows y `LYNX` en macOS y Linux. En macOS la ruta completa es `/Applications/LYNX.app/Contents/MacOS/LYNX`. Las opciones son las mismas en todos los sistemas.

## Renderizar un proyecto a un archivo

1. Abre un terminal en la carpeta que contiene el programa. En Windows es la carpeta de instalación o la carpeta donde extrajiste el zip.
2. Ejecuta `LYNX.exe --export project.flp out.flac`, con tu propio proyecto y archivo de salida.
3. Espera a que el programa termine. No se abre ninguna ventana.
4. Lee `out.flac.txt`, escrito junto al archivo de salida. En un proyecto importado, lista lo que se convirtió, lo que se aproximó y lo que quedó fuera, y qué plugins se encontraron o faltan.

## Abrir un proyecto

```
LYNX.exe project.lynx
```

Inicia LYNX SHARE con la ventana y abre el archivo. Acepta `.lynx`, `.flp`, `.als`, `.flm` y `.mid`. Una ruta relativa se toma desde la carpeta actual.

## Opciones de uso diario

| Opción | Qué hace |
|---|---|
| `--export <project> <out>` | Renderiza la canción entera con su cola, como la ventana de exportación. La extensión de `<out>` elige el formato: `.wav` (24 bits), `.flac`, `.ogg` o `.mp3` (320 kbps) |
| `--export ... --pattern` | Renderiza solo el patrón actual |
| `--export ... --insert N` | Renderiza solo la salida del insert N del Mixer |
| `--export ... --rate N` | Renderiza a otra frecuencia de muestreo, en Hz |
| `--stems <project> <base.wav>` | Escribe un WAV por cada insert del Mixer que tenga canales: `base - 1.wav`, `base - 2.wav` y así sucesivamente |
| `--export-flp <project> <out.flp>` | Escribe un proyecto de FL Studio. Con `out.zip` escribe un paquete comprimido de loops |
| `--export-flp ... --substitute` | Cambia los plugins exclusivos de LYNX por el plugin de FL más cercano en lugar de renderizarlos a audio |
| `--export-als <project> <folder>` | Escribe un set de Ableton Live en `<folder>/<name> Project/<name>.als`, para Live 12 |
| `--export-als ... --live11` | Escribe el set para Live 11 |
| `--devices <report.txt>` | Lista los tipos de driver de audio y los dispositivos de este ordenador |
| `--showcase <out.lynx>` | Escribe el proyecto de demostración de LYNX SHARE |
| `--lang <id>` | Usa otro idioma de interfaz en esta ejecución, por ejemplo `--lang pt-BR` |
| `--menu-entry on` o `desactivado` | Solo AppImage de Linux: añade LYNX SHARE al menú de aplicaciones, o lo quita |

`--export`, `--stems` y `--export-flp` aceptan un `.lynx`, `.flp` o `.als` como proyecto. `--export-als` acepta también `.flm`.

Ejemplos:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opciones pensadas para pruebas

Existen para comprobar LYNX SHARE en sí. Son útiles en un informe de error.

| Opción | Qué hace |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Ejecuta la autoprueba sin dispositivo de audio. El informe termina con `result=OK` si se supera |
| `--snapshot <prefix> [project]` | Dibuja la interfaz y sus ventanas en archivos PNG con el prefijo como nombre. Las imágenes de esta wiki se hacen así |
| `--render <project> <out.wav> <seconds>` | Renderiza esos segundos del patrón actual a un WAV de 24 bits |
| `--plugintest <plugin> <report.txt>` | Carga y prueba un plugin VST3 o CLAP |
| `--sandboxtest <plugin> <report.txt>` | Prueba un plugin en un proceso aparte, incluso uno que se cuelga |
| `--scantest <folder> <report.txt>` | Escanea una carpeta en busca de plugins, en procesos aparte |
| `--fldb <report.txt> [project.flp]` | Informa de la instalación de FL Studio que se encuentra aquí, su base de plugins y las coincidencias de plugins de un proyecto |
| `--devicetest <type> <device> <report.txt>` | Abre un driver de audio durante un segundo de silencio e informa del resultado |
| `--uiperf <project> <report.txt>` | Mide el tiempo de dibujo del Mixer, la Playlist y el Channel Rack |

Ejemplos:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Conviene saber

- Los resultados van a archivos, no al terminal. `--export` escribe `<out>.txt`, `--export-flp` escribe `<out.flp>.txt`, `--export-als` escribe `export-als.txt` en la carpeta y `--stems` escribe `<base.wav>.txt`.
- El programa lee la entrada estándar. En un bucle de shell, añade `< /dev/null` a cada llamada.
- `--snapshot` dibuja a 1280x720 salvo que la variable de entorno `LYNX_SNAPSHOT_SIZE` indique otra cosa, por ejemplo `1920x1080`.
- `--lang` acepta los ids de los idiomas que aparecen en `Opciones › Configuración general...`, como `en`, `pt-BR`, `de`, `es`, `fr` o `ja`. Un id desconocido se ignora.
- `--export` usa el mismo camino de render que la ventana de exportación, pero ninguna de las demás opciones de la ventana. Para dither, resolución en bits o normalización, usa la ventana.
- El código fuente tiene muchas más opciones de prueba. Son internas y pueden cambiar entre versiones.

## Relacionado

- [Exportación](/wiki/exporting/)
- [Formatos de archivo](/wiki/file-formats/)
- [Compilar desde el código fuente](/wiki/build-from-source/)
- [Solución de problemas](/wiki/troubleshooting/)
