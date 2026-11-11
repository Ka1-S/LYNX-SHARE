---
source: 7db9a73959
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
| `Compartir plugins con quien entre...` | Para el host: cuáles de tus plugins pueden usar los invitados. El elemento del menú muestra cuántos se comparten |
| `Mostrar código de invitación...` | Para el anfitrión: vuelve a mostrar el código, con un botón `Copiar` |
| `Terminar sesión en vivo` | Para el anfitrión. Todos salen y el código deja de funcionar |
| `Salir de la sesión en vivo` | Para un invitado. Tu parte del proyecto se queda contigo |

En `Personas y permisos...`, cada persona recibe uno de cuatro roles: `Sus propios canales, patrones, inserts y filas`, `Sus propias partes + tempo y ajustes del proyecto`, `Todo` o `Solo escuchar (sin ediciones)`. Quitar un permiso nunca borra nada.

## Plugins que los demás no tienen

Un canal o insert cuyo plugin no está instalado en tu ordenador reproduce un stand-in, y muestra una insignia ámbar `R` en el Channel Rack y el Mixer.

- En un paquete, los stand-ins son renders de audio hechos al guardar el paquete. Siguen a los clips que mueves, pero no las ediciones de las notas de un plugin que no tienes. Pide un paquete nuevo a su propietario.
- En una sesión en vivo, las partes que usan un plugin que alguien no tiene las renderiza la persona que lo tiene y se transmiten a los demás como audio.

## Compartir tus plugins con los invitados

En una sesión en vivo, el host puede dejar que los invitados usen plugins que solo tiene el host.

El host hace esto:

1. Elige `Archivo › Sesión online › Compartir plugins con quien entre...`.
2. Marca los plugins que quieras compartir. `Buscar en tus plugins` reduce la lista. No se comparte nada hasta que lo marcas.

Un invitado hace esto:

1. Abre el menú de plugins de uno de tus canales o ranuras de efecto. Los plugins compartidos aparecen primero, bajo `On <host>'s computer`.
2. Elige uno. Su ventana lista los parámetros del plugin como deslizadores, con `Buscar parámetros`.
3. Añade notas y mueve los deslizadores. La ventana muestra `Updating...` mientras el ordenador del host renderiza, y luego `Al día`.

- El plugin se queda en el ordenador del host y funciona con la licencia del host. LYNX SHARE nunca envía el plugin, sus archivos, sus presets ni su licencia.
- El invitado oye un render que sigue las notas y los valores al cabo de un momento. No se toca en directo, y el invitado no recibe la ventana propia del plugin.
- Mientras el host no está, suena el último render. La compartición se detiene cuando termina la sesión, y el último render se queda.
- Un invitado con el rol `Solo escuchar (sin ediciones)` no puede usar plugins compartidos.
- Algunas licencias de plugins no permiten que otras personas manejen el plugin, ni siquiera de forma remota. Revisa cada licencia antes de compartir.

## Qué pasa por la red

Los paquetes son archivos. No se sube nada a ningún sitio.

Una sesión en vivo se conecta solo mientras estás en una. Todo pasa por el relay `relay.lynx-share.com`, cifrado de extremo a extremo con una clave que sale del código de invitación. El relay transmite los datos sin poder leerlos. Ve tu dirección IP, cuándo estuviste conectado y cuántos bytes pasaron. Las demás personas de la sesión ven tu nombre, tus ediciones, los samples que usa tu parte y si estás reproduciendo o grabando. No ven tu dirección IP. Los detalles están en la [política de privacidad](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Conviene saber

- `Solo su propia parte` es una cortesía entre músicos, no una protección contra copias. El paquete es un zip normal.
- Ambas partes deberían usar la misma versión de LYNX SHARE. El resumen del paquete avisa cuando difieren.
- Un invitado cuya conexión se corta se reconecta solo y conserva sus ediciones.
- Si LYNX SHARE del anfitrión se cierra durante una sesión, el siguiente arranque ofrece reanudarla, y los invitados se reconectan solos.
- Si el LYNX SHARE de un invitado se cierra durante una sesión, el siguiente inicio pregunta si quiere volver a entrar. `Volver a entrar` te devuelve como la misma persona, con tu parte y todo lo que habías cambiado. Esto funciona durante 12 horas.
- La versión de Linux puede alojar una sesión en vivo desde la 0.1.26.
- En una sesión en vivo, deshacer y rehacer solo recuperan tus propias ediciones.
- Cualquiera con el código de invitación puede pedir unirse, pero solo después de que el anfitrión lo deje entrar.

## Relacionado

- [Lo que solo hace LYNX SHARE: sesiones online](/why/#online)
- [Formatos de archivo](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Deshacer e historial](/wiki/undo-history/)
- [Solución de problemas](/wiki/troubleshooting/)
