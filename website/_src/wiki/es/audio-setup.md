---
source: 3801d5daff
---
![La página Audio de la ventana Configuración, sin ningún dispositivo de salida elegido todavía](/assets/img/wiki/settings-audio.webp)

La página Audio de la ventana Configuración elige adónde va el sonido y de dónde vienen las grabaciones. Ábrela con `Opciones › Configuración de audio...`. El asistente de configuración (`Opciones › Asistente de configuración (idioma, audio, carpetas de plugins)...`) también prueba la salida de audio.

## Sacar sonido

1. Abre `Opciones › Configuración de audio...`.
2. Elige un `Driver`. La nota bajo la caja dice para qué sirve ese driver.
3. Elige un `Dispositivo de salida`.
4. Haz clic en `Reproducir un sonido de prueba`.
5. Mira la sección `Estado`. Muestra el driver en marcha, la frecuencia de muestreo y la latencia de salida, o `No hay ningún dispositivo de audio abierto: elige un dispositivo de salida.`

## Drivers

La caja `Driver` lista los tipos de driver de tu sistema.

| Sistema | Driver | Qué hace |
|---|---|---|
| Windows | Windows Audio (shared) | Los demás programas conservan su sonido. Windows mezcla todos los programas en pasos fijos, así que el búfer no puede bajar de ese paso |
| Windows | Windows Audio (shared, low-latency period) | Compartido, pero pide a Windows un periodo de mezcla más corto. Solo algunos drivers lo admiten |
| Windows | Windows Audio (exclusive, lowest latency) | LYNX SHARE usa el dispositivo en solitario. Los demás programas se quedan sin sonido mientras suena |
| Windows | DirectSound (legacy) | Vía de audio antigua de Windows, con latencia alta. Para cuando fallan las demás |
| Windows | ASIO | Drivers de baja latencia. El driver ASIO de un fabricante funciona con su propio dispositivo |
| Windows | LYNX ASIO (built in) | El driver ASIO propio de LYNX SHARE, para cualquier dispositivo, en reproducción y grabación. `Configuración del driver...` elige el modo compartido o exclusivo y los dispositivos |
| macOS | Core Audio | El audio propio del sistema, compartido con los demás programas |
| Linux | PipeWire (native) | LYNX SHARE habla con PipeWire directamente. `System default` sigue la salida elegida en los ajustes de sonido del sistema |
| Linux | JACK / PipeWire | Para un servidor JACK en marcha, o PipeWire con `pipewire-jack` |
| Linux | ALSA | Habla con la tarjeta de sonido. `default` o `pipewire` comparten el dispositivo, y un dispositivo `hw:` toma la tarjeta solo para LYNX SHARE |

En Linux, el primer arranque abre el primero de PipeWire, JACK y ALSA que tenga dispositivos. Una elección guardada se conserva.

## Los ajustes de la página

| Ajuste | Qué hace |
|---|---|
| `Driver` | El tipo de driver, de la tabla de arriba |
| `Dispositivo de salida` | Dónde suena el Master |
| `Dispositivo de entrada` | La fuente para grabar audio y para las entradas de los inserts del Mixer. `(ninguno)` por defecto |
| `Frec. muestreo` | Las frecuencias que ofrece el dispositivo |
| `Tamaño del búfer` | Los tamaños que ofrece el dispositivo, cada uno con su latencia en milisegundos. Los búferes pequeños responden más rápido al tocar. Los grandes dan más margen a la CPU, con menos chasquidos |
| `Configuración del driver...` | Abre la ventana propia del driver (drivers ASIO) |
| `Reproducir un sonido de prueba` | Reproduce un sonido de prueba por el dispositivo de salida |
| `Grabar audio de entrada` | Graba los dos primeros canales de entrada en un nuevo insert cuando no hay ningún insert armado |
| `Formato de archivo` | Las grabaciones son WAV de 24 bits o WAV de 32 bits flotante, a la frecuencia de muestreo del dispositivo |
| `Medir la ida y vuelta` | Reproduce 8 clics desde el `Salida de ping` hasta el `Entrada de ping` por un cable o un loopback, y mide el retardo para que las tomas caigan donde las tocaste |
| `Ajuste fino (muestras)` | Mueve todas las tomas nuevas. Un valor positivo las adelanta en la Playlist |
| `Pista del metrónomo` | La pista del Mixer por la que suena el metrónomo. `---` es el Master. El clic nunca va en una exportación |
| `Reproducir notas truncadas en el transporte` | Reproduce las notas que cruzan la posición de inicio |
| `Reiniciar plugins en el transporte` | Borra las colas de los efectos cuando empieza o salta la reproducción |

## Cuando no hay sonido

- Mira primero `Estado`. Si no hay ningún dispositivo abierto, elige un dispositivo de salida.
- Prueba otro driver. En Windows, `Windows Audio (compartido)` deja que los demás programas conserven su sonido, y `DirectSound (heredado)` está para cuando fallan los demás.
- Revisa el fader del Master en el [Mixer](/wiki/mixer/) y el mando `MASTER` de la barra de herramientas.
- En modo Patrón solo suena el patrón actual. Si está vacío, no oyes nada. Pulsa `L` para el modo Canción.
- En Linux, si el sonido se corta, `Estado` muestra si el hilo de audio obtuvo prioridad de tiempo real. [Solución de problemas](/wiki/troubleshooting/) tiene el arreglo.

## Conviene saber

- ASIO solo existe en Windows.
- Con `Windows Audio (compartido)`, el búfer más pequeño es el paso de mezcla del sistema. La nota bajo la caja `Driver` da la cifra para tu dispositivo. Para menos latencia usa el modo exclusivo o un driver ASIO.
- Con PipeWire, el búfer va de 32 a 2048 muestras y la frecuencia de muestreo de 44,1 a 192 kHz. Si PipeWire mantiene su propia frecuencia, remuestrea el flujo de LYNX SHARE.
- Con PipeWire, cuando se desenchufa un dispositivo con nombre, LYNX SHARE vuelve a `System default` y conserva el tamaño del búfer.
- `Estado` también muestra la carga de CPU del audio y cuenta los cortes (buffer underruns).
- Puedes listar los drivers y dispositivos sin abrir la ventana. Mira `--devices` en [Línea de comandos](/wiki/command-line/).

## Relacionado

- [Configuración](/wiki/settings/)
- [Grabación](/wiki/recording/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Solución de problemas](/wiki/troubleshooting/)
