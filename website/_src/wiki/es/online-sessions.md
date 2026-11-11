---
source: 27354bbca9
---
Hay dos formas de trabajar en un proyecto con otras personas, y las dos están en `Archivo › Sesión online`. Un paquete es un archivo que envías a un amigo, y él te devuelve una parte. No necesita red, cuenta ni servidor. Una sesión en vivo conecta a todos por internet a través del relay de LYNX SHARE, y cada cambio llega a todos. En un proyecto participan hasta 4 personas, tú incluido.

## Enviar un proyecto a un amigo

![Enviar proyecto](/assets/img/wiki/session-send.webp)

1. Abre el proyecto y elige `Archivo › Sesión online › Enviar proyecto...`.
2. Escribe tu nombre y el de tu amigo.
3. Elige qué se queda tu amigo cuando termina la sesión: `Solo su propia parte (recomendado)` o `Una copia completa del proyecto`.
4. Guarda el archivo `.lynxpack`.
5. Envía el archivo como quieras: correo, una carpeta compartida, una memoria USB.

El paquete contiene el proyecto, todos los samples y grabaciones que usa, y renders de audio ("stand-ins") de las partes que usan un plugin de terceros. Tu amigo oye esas partes sin tener tus plugins.

## Grabar una parte y devolverla

Esto lo hace el amigo:

1. Abre el `.lynxpack` con `Archivo › Abrir...`. Un resumen muestra quién lo envió y qué inserts del mixer y pistas de la Playlist son tuyos. Haz clic en `Abrir`.
2. Añade tus propios canales, patrones y audio en tus inserts y pistas de la Playlist. El material del anfitrión se deja como está.
3. Elige `Archivo › Sesión online › Devolver mi parte...` y envía el nuevo paquete al anfitrión.

Cada invitado recibe un color, 20 inserts del mixer y 16 pistas de la Playlist.

## Fusionar una parte

Esto lo hace el anfitrión:

1. Abre el proyecto que se envió.
2. Elige `Archivo › Sesión online › Fusionar una parte...` y selecciona el paquete que tu amigo te devolvió.
3. Los canales, patrones, notas, clips e inserts del amigo se añaden con su color. No se cambia nada tuyo.

Fusionar es un solo paso de deshacer. Una parte más reciente del mismo amigo reemplaza a la anterior.

## Iniciar o unirse a una sesión en vivo

![Iniciar sesión en vivo](/assets/img/wiki/session-start.webp)

El anfitrión inicia la sesión:

1. Abre el proyecto y elige `Archivo › Sesión online › Iniciar sesión en vivo...`.
2. Escribe tu nombre.
3. En `Quien entra puede editar`, elige `Solo sus propios canales, patrones, inserts y filas (recomendado)` o `Todo`. En `Cuando termine la sesión, quien entró conserva`, elige su propia parte o una copia completa.
4. Haz clic en `Inicio`, copia el código de invitación y envíalo a hasta 3 personas.
5. Cuando alguien pida unirse, haz clic en `Dejar entrar`.

Un invitado se une:

1. Elige `Archivo › Sesión online › Unirse a una sesión en vivo...`.
2. Escribe tu nombre, pega el código de invitación y haz clic en `Unirse`.
3. Espera a que el anfitrión te deje entrar. Se abre el proyecto del anfitrión.

## Durante una sesión en vivo

![Chat de la sesión](/assets/img/wiki/session-chat.webp)

El menú `Archivo › Sesión online` tiene estos comandos mientras hay una sesión en marcha:

| Comando | Qué hace |
|---|---|
| `Chat de la sesión...` | Abre el chat. El elemento del menú muestra el número de mensajes sin leer |
| `Seguir el transporte de quien creó la sesión` | Para invitados: reproducir, detener y la posición siguen al anfitrión |
| `Personas y permisos...` | Para el anfitrión: qué puede cambiar cada persona y qué se queda al final |
| `Mostrar código de invitación...` | Para el anfitrión: vuelve a mostrar el código, con un botón `Copiar` |
| `Terminar sesión en vivo` | Para el anfitrión. Todos salen y el código deja de funcionar |
| `Salir de la sesión en vivo` | Para un invitado. Tu parte del proyecto se queda contigo |

En `Personas y permisos...`, cada persona recibe uno de cuatro roles: `Sus propios canales, patrones, inserts y filas`, `Sus propias partes + tempo y ajustes del proyecto`, `Todo` o `Solo escuchar (sin ediciones)`. Quitar un permiso nunca borra nada.

## Plugins que los demás no tienen

Un canal o insert cuyo plugin no está instalado en tu ordenador reproduce un stand-in, y muestra una insignia ámbar `R` en el Channel Rack y el Mixer.

- En un paquete, los stand-ins son renders de audio hechos al guardar el paquete. Siguen a los clips que mueves, pero no las ediciones de las notas de un plugin que no tienes. Pide un paquete nuevo a su propietario.
- En una sesión en vivo, las partes que usan un plugin que alguien no tiene las renderiza la persona que lo tiene y se transmiten a los demás como audio.

## Qué pasa por la red

Los paquetes son archivos. No se sube nada a ningún sitio.

Una sesión en vivo se conecta solo mientras estás en una. Todo pasa por el relay `relay.lynx-share.com`, cifrado de extremo a extremo con una clave que sale del código de invitación. El relay transmite los datos sin poder leerlos. Ve tu dirección IP, cuándo estuviste conectado y cuántos bytes pasaron. Las demás personas de la sesión ven tu nombre, tus ediciones, los samples que usa tu parte y si estás reproduciendo o grabando. No ven tu dirección IP. Los detalles están en la [política de privacidad](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Conviene saber

- `Solo su propia parte` es una cortesía entre músicos, no una protección contra copias. El paquete es un zip normal.
- Ambas partes deberían usar la misma versión de LYNX SHARE. El resumen del paquete avisa cuando difieren.
- Un invitado cuya conexión se corta se reconecta solo y conserva sus ediciones.
- Si LYNX SHARE del anfitrión se cierra durante una sesión, el siguiente arranque ofrece reanudarla, y los invitados se reconectan solos.
- En una sesión en vivo, deshacer y rehacer solo recuperan tus propias ediciones.
- Cualquiera con el código de invitación puede pedir unirse, pero solo después de que el anfitrión lo deje entrar.

## Relacionado

- [Lo que solo hace LYNX SHARE: sesiones online](/why/#online)
- [Formatos de archivo](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Deshacer e historial](/wiki/undo-history/)
- [Solución de problemas](/wiki/troubleshooting/)
