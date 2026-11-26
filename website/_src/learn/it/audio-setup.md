---
source: 297095d385
---
## Driver e uscita

![La pagina Audio delle Impostazioni](/assets/img/learn/audio-setup-1.webp)

1. Apri `Opzioni › Impostazioni audio...`.
2. Scegli il driver. Su Windows, `LYNX ASIO (integrato)` funziona con qualsiasi dispositivo e non ha bisogno di un driver proprio; ci sono anche il driver ASIO della tua interfaccia, FL Studio ASIO e `Windows Audio (condiviso)` o `Windows Audio (esclusivo, latenza minima)`. Core Audio su Mac, PipeWire o ALSA su Linux.
3. `Dispositivo di uscita` è dove senti LYNX. `System default` segue l'uscita usata dal tuo computer.
4. `Dispositivo di ingresso` è ciò da cui registri (un microfono, un ingresso per chitarra). `Frequenza` di solito è 44100 o 48000 Hz.

## Dimensione del buffer

![Le dimensioni del buffer, con la loro latenza](/assets/img/learn/audio-setup-2.webp)

1. `Dimensione del buffer`: un buffer più piccolo risponde più in fretta quando suoni una tastiera; uno più grande dà più margine alla CPU.
2. Scricchiolii o interruzioni? Aumentalo. Le note arrivano in ritardo quando suoni? Diminuiscilo.

## Prova il suono

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Riproduci un suono di prova` riproduce un breve suono sull'uscita scelta.
2. Da qualsiasi punto, `Aiuto › Prova l'uscita audio...` fa lo stesso.

## Le registrazioni arrivano in ritardo?

1. La tua interfaccia ha bisogno di un attimo per far entrare il suono e farlo uscire di nuovo, quindi una ripresa può finire un po' in ritardo.
2. Nella stessa pagina Audio, sotto `Compensazione della latenza di registrazione`, collega un'uscita a un ingresso con un cavo (o usa il loopback della tua interfaccia) e clicca `Misura l'andata e ritorno`.
3. LYNX suona alcuni clic, misura quanto tornano in ritardo e da quel momento mette a tempo le riprese registrate.

Vedi anche [Configurazione audio](/wiki/audio-setup/) e [Risoluzione dei problemi](/wiki/troubleshooting/).
