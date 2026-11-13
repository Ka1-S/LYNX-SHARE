---
source: 3bf7e0c0ab
---
LYNX SHARE mantiene el flujo de trabajo que ya conoces: un Channel Rack, patrones, una Playlist y un Mixer, con las mismas teclas. Abre proyectos `.flp` directamente y puede escribir uno de vuelta.

LYNX SHARE es un programa independiente. No está afiliado a Image-Line, ni cuenta con su aval o patrocinio, y no contiene código de Image-Line. FL Studio es una marca registrada de Image-Line Software.

## Abrir un proyecto .flp

1. Elige `Archivo › Abrir...` (`Ctrl+O`) y selecciona el `.flp`.
2. LYNX SHARE resuelve los plugins y samples del proyecto y luego lo abre. El título muestra "(imported from FL Studio)".
3. Elige `Ayuda › Last FL Studio import report` para leer qué se aproximó, se dejó fuera o no se encontró.
4. Elige `Ayuda › Archivos que faltan...` para ver los samples y plugins que faltan.
5. Elige `Archivo › Guardar`. Pregunta dónde escribir un archivo `.lynx`. El `.flp` en sí nunca se escribe.

## Equivalencias

| En FL Studio | En LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Los mismos nombres, en `F5`, `F6`, `F7` y `F9` |
| Selector de plugins | `F8` |
| Browser | `Alt+F8` |
| Modo Pattern / Song | `L` |
| Deshacer | `Ctrl+Z` deshace, o rehace tras un deshacer. `Ctrl+Alt+Z` deshace hacia atrás |
| Guardar nueva versión | `Ctrl+Alt+S` |
| Exportar | `Ctrl+R`, MP3 en `Ctrl+Shift+R`, MIDI en `Ctrl+Shift+M` |
| Inserts, envíos y sidechain del Mixer | Igual, con 10 ranuras de efecto por insert |
| Fruity Wrapper | Los plugins de terceros se alojan directamente, mira [Plugins de terceros](/wiki/plugins/) |
| Generadores y efectos nativos | Suenan con motores que LYNX SHARE recreó, mira más abajo |
| Archivo de proyecto `.flp` | Archivo de proyecto `.lynx` |

`Ayuda › Atajos` lista todas las teclas.

## Qué se importa

- Canales, samples, patrones, notas slide y porta, todos los arreglos, marcadores, cambios de compás, automatización y vínculos a controladores.
- El Mixer: enrutamiento, niveles, sidechains, EQ de pista y efectos nativos con sus ajustes guardados.
- Plugins de terceros VST3, VST2 y CLAP dentro de Fruity Wrapper: se sustituyen por el mismo plugin instalado en tu ordenador, con su estado guardado.

## Plugins nativos de FL Studio

Los generadores y efectos propios de FL Studio no se cargan. LYNX SHARE los reproduce con motores que recreó, y cada uno tiene un nivel de calidad:

| Nivel | Significado |
|---|---|
| Exact | Idéntico bit a bit al original en todos los estados probados |
| Near-exact | Idéntico bit a bit en la mayoría de los estados; el resto difiere por debajo de unos -100 dB |
| Close | El mismo algoritmo, a menos de -60 dB del original en casi todos los estados probados |
| Approximate | Los ajustes guardados se asignan a un sintetizador o efecto integrado de LYNX SHARE |
| Placeholder | Se conserva el estado y el plugin queda en silencio o deja pasar el audio |

El nivel de cada motor, y lo que aún le falta a cada uno, está en [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Mira también [Instrumentos y efectos integrados](/wiki/built-in/).

## Qué necesita tu propia instalación de FL Studio

Abrir un `.flp` funciona sin FL Studio instalado. Una instalación en el mismo ordenador añade dos cosas:

- Los samples de fábrica de FL Studio y su base de plugins, para que los samples y plugins se encuentren sin escanear.
- Las ondas, samples, impulsos y tablas integrados que necesitan algunos motores (FL Keys, Soundgoodizer, Maximus y los demás marcados con una daga en PLUGINS.md). Se leen de tu instalación en tiempo de ejecución y no vienen con LYNX SHARE.

En Linux, LYNX SHARE busca un FL Studio de Windows bajo Wine. En macOS se busca el paquete de la aplicación, pero no está probado.

## Exportar de vuelta a .flp

1. Elige `Archivo › Exportar › Proyecto de FL Studio...`.
2. Elige `Proyecto de FL Studio (.flp)` o `Paquete comprimido (.zip, con todos los samples)`.
3. Elige qué pasa con lo que FL Studio no tiene: renderizarlo a audio (suena igual) o usar el plugin de FL más cercano (sigue siendo editable, puede sonar distinto, y los instrumentos exclusivos de LYNX se dejan fuera).

El archivo es un proyecto de FL Studio 2026. Los samplers, clips de audio, generadores y efectos propios de FL, plugins de terceros (dentro de Fruity Wrapper), patrones, la Playlist, el Mixer y la automatización pasan con sus ajustes. Un informe lista qué pasó tal cual y qué se renderizó, sustituyó o dejó fuera.

## FL Studio Mobile (.flm)

`Archivo › Abrir...` también acepta proyectos `.flm`. La importación es parcial.

- Se importa: tempo, compás, un insert del Mixer por canal FLM, una pista de la Playlist por carril, patrones (cada pasada del bucle como clip), clips de audio con desfase y tono, kits de batería, DW Sampler y programas `.dwp` y el Slicer (con los motores propios de FLM), los instrumentos MiniSynth, Transistor Bass, 3x Osc, GMSynth y SuperSaw, los efectos de FLM (como efectos Pocket), la automatización de nivel, pan, silencio, solo, pitch bend, envíos y parámetros de instrumento, y los sets de acordes y el bloqueo de tonalidad.
- No se importa: los demás instrumentos de FLM, que suenan con el sintetizador integrado.
- Los samples se buscan junto al `.flm` y en la copia que FL Studio tiene de los recursos de FLM. Un `.dwp` que no se encuentra suena con un sustituto.

## Conviene saber

- Los packs FLEX están cifrados y nunca se abren. Un canal FLEX recibe un patch de sintetizador elegido por el nombre de su preset.
- Los moduladores se dejan fuera de una exportación a `.flp`, y el informe de exportación lo dice.
- El informe de importación lista qué aproximó el sintetizador integrado, qué no tenía equivalente, qué plugins se encontraron o no están instalados, qué efectos se dejaron fuera o se reprodujeron en parte, y cuántos samples faltan.
- Un efecto de terceros que no está instalado se sustituye por un efecto de LYNX SHARE con la misma función. El original sigue en el proyecto.

## Relacionado

- [Instrumentos y efectos integrados](/wiki/built-in/)
- [Plugins de terceros](/wiki/plugins/)
- [Formatos de archivo](/wiki/file-formats/)
- [Atajos](/wiki/shortcuts/)
- [Lo que solo hace LYNX SHARE con los ajustes de FL](/why/#fl-settings)
