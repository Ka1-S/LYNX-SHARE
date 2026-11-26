---
source: 297095d385
---
## Pilote et sortie

![La page Audio des Réglages](/assets/img/learn/audio-setup-1.webp)

1. Ouvrez `Options › Réglages audio...`.
2. Choisissez le pilote. Sous Windows, `LYNX ASIO (intégré)` fonctionne avec n'importe quel périphérique et n'a besoin d'aucun pilote propre ; le pilote ASIO de votre interface, FL Studio ASIO et `Windows Audio (partagé)` ou `Windows Audio (exclusif, latence minimale)` sont là aussi. Core Audio sur Mac, PipeWire ou ALSA sous Linux.
3. `Périphérique de sortie` est l'endroit où vous entendez LYNX. `System default` suit la sortie que votre ordinateur utilise.
4. `Périphérique d'entrée` est ce depuis quoi vous enregistrez (un microphone, une entrée guitare). `Fréq. d'échantillonnage` vaut en général 44100 ou 48000 Hz.

## Taille du tampon

![Les tailles de tampon, avec leur latence](/assets/img/learn/audio-setup-2.webp)

1. `Taille du tampon` : un tampon plus petit répond plus vite quand vous jouez sur un clavier ; un plus grand laisse plus de marge au processeur.
2. Des craquements ou des coupures ? Augmentez-le. Les notes arrivent en retard quand vous jouez ? Diminuez-le.

## Testez le son

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Jouer un son de test` joue un court son sur la sortie choisie.
2. De n'importe où, `Aide › Tester la sortie audio...` fait la même chose.

## Des prises enregistrées en retard ?

1. Votre interface a besoin d'un instant pour faire entrer puis ressortir le son, donc une prise peut arriver un peu en retard.
2. Sur la même page Audio, sous `Compensation de latence de l'enregistrement`, reliez une sortie à une entrée avec un câble (ou utilisez le loopback de votre interface) et cliquez sur `Mesurer l'aller-retour`.
3. LYNX joue quelques clics, mesure leur retard au retour et, à partir de là, cale les prises enregistrées dans le temps.

Voir aussi [Configuration audio](/wiki/audio-setup/) et [Dépannage](/wiki/troubleshooting/).
