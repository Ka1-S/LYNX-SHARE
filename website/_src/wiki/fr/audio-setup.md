---
source: 3801d5daff
---
![La page Audio de la fenêtre Réglages, sans périphérique de sortie choisi pour l'instant](/assets/img/wiki/settings-audio.webp)

La page Audio de la fenêtre Réglages sert à choisir où va le son et d'où viennent les enregistrements. Ouvrez-la avec `Options › Réglages audio...`. L'assistant de configuration (`Options › Assistant de configuration (langue, audio, dossiers de plugins)...`) teste aussi la sortie audio.

## Faire sortir le son

1. Ouvrez `Options › Réglages audio...`.
2. Choisissez un `Pilote`. La note sous la liste dit à quoi sert ce pilote.
3. Choisissez un `Périphérique de sortie`.
4. Cliquez sur `Jouer un son de test`.
5. Lisez la section `État`. Elle indique le pilote actif, la fréquence d'échantillonnage et la latence de sortie, ou `Aucun périphérique audio ouvert : choisissez un périphérique de sortie.`

## Pilotes

La liste `Pilote` montre les types de pilotes de votre système.

| Système | Pilote | Ce qu'il fait |
|---|---|---|
| Windows | Windows Audio (partagé) | Les autres programmes gardent leur son. Windows mixe tous les programmes par pas fixes, donc le tampon ne peut pas descendre sous ce pas |
| Windows | Windows Audio (partagé, période basse latence) | Partagé, mais demande à Windows une période de mixage plus courte. Seuls certains pilotes le gèrent |
| Windows | Windows Audio (exclusif, latence minimale) | LYNX SHARE utilise seul le périphérique. Les autres programmes sont muets pendant la lecture |
| Windows | DirectSound (ancien) | Ancien chemin audio de Windows, à forte latence. Pour quand les autres échouent |
| Windows | ASIO | Pilotes à faible latence. Le pilote ASIO d'un fabricant fonctionne avec son propre périphérique |
| Windows | LYNX ASIO (intégré) | Le pilote ASIO propre à LYNX SHARE, pour n'importe quel périphérique, en lecture comme en enregistrement. `Réglages du pilote...` choisit le mode partagé ou exclusif et les périphériques |
| macOS | Core Audio | L'audio du système, partagé avec les autres programmes |
| Linux | PipeWire (natif) | LYNX SHARE parle directement à PipeWire. `System default` suit la sortie choisie dans les réglages sonores du système |
| Linux | JACK / PipeWire | Pour un serveur JACK en cours d'exécution, ou PipeWire avec `pipewire-jack` |
| Linux | ALSA | Parle à la carte son. `default` ou `pipewire` partagent le périphérique, un périphérique `hw:` réserve la carte à LYNX SHARE seul |

Sous Linux, le premier démarrage ouvre le premier de PipeWire, JACK et ALSA qui a des périphériques. Un choix enregistré est conservé.

## Les réglages de la page

| Réglage | Ce qu'il fait |
|---|---|
| `Pilote` | Le type de pilote, parmi le tableau ci-dessus |
| `Périphérique de sortie` | Où joue le Master |
| `Périphérique d'entrée` | La source pour l'enregistrement audio et pour les entrées des inserts du Mixer. `(aucun)` par défaut |
| `Fréq. d'échantillonnage` | Les fréquences proposées par le périphérique |
| `Taille du tampon` | Les tailles proposées par le périphérique, chacune avec sa latence en millisecondes. Un petit tampon répond plus vite quand vous jouez. Un grand tampon laisse plus de marge au CPU, avec moins de craquements |
| `Réglages du pilote...` | Ouvre la fenêtre propre au pilote (pilotes ASIO) |
| `Jouer un son de test` | Joue un son de test sur le périphérique de sortie |
| `Enregistrer l'audio de l'entrée` | Enregistre les deux premiers canaux d'entrée dans un nouvel insert quand aucun insert n'est armé |
| `Format de fichier` | Les enregistrements sont en WAV 24 bits ou WAV 32 bits flottant, à la fréquence d'échantillonnage du périphérique |
| `Mesurer l'aller-retour` | Joue 8 clics de `Sortie du ping` vers `Entrée du ping` par un câble ou un loopback, et mesure le retard pour que les prises tombent là où vous avez joué |
| `Ajustement fin (échantillons)` | Décale chaque nouvelle prise. Une valeur positive place la prise plus tôt dans la Playlist |
| `Piste du métronome` | La piste du Mixer par laquelle passe le métronome. `---` est le Master. Le clic n'est jamais dans un export |
| `Jouer les notes tronquées au transport` | Joue les notes qui traversent la position de départ |
| `Réinitialiser les plugins au transport` | Vide la queue des effets quand la lecture démarre ou saute |

## Quand il n'y a pas de son

- Regardez d'abord `État`. Si aucun périphérique n'est ouvert, choisissez un périphérique de sortie.
- Essayez un autre pilote. Sous Windows, `Windows Audio (partagé)` laisse les autres programmes garder leur son, et `DirectSound (ancien)` est là pour quand les autres échouent.
- Vérifiez le fader Master dans le [Mixer](/wiki/mixer/) et le bouton `MASTER` de la barre d'outils.
- En mode Pattern, seul le pattern actuel est lu. S'il est vide, vous n'entendez rien. Appuyez sur `L` pour le mode Song.
- Sous Linux, si le son craque, `État` indique si le thread audio a obtenu la priorité temps réel. La page [Dépannage](/wiki/troubleshooting/) donne le correctif.

## Bon à savoir

- ASIO n'existe que sous Windows.
- Avec `Windows Audio (partagé)`, le plus petit tampon est le pas de mixage du système. La note sous la liste `Pilote` donne la valeur pour votre périphérique. Pour une latence plus basse, utilisez le mode exclusif ou un pilote ASIO.
- Avec PipeWire, le tampon va de 32 à 2048 échantillons et la fréquence d'échantillonnage de 44,1 à 192 kHz. Si PipeWire garde sa propre fréquence, il rééchantillonne le flux de LYNX SHARE.
- Avec PipeWire, quand un périphérique nommé est débranché, LYNX SHARE revient sur `System default` et garde la taille du tampon.
- `État` montre aussi la charge CPU audio et compte les coupures (underruns du tampon).
- Vous pouvez lister les pilotes et les périphériques sans ouvrir la fenêtre. Voyez `--devices` dans [Ligne de commande](/wiki/command-line/).

## Voir aussi

- [Réglages](/wiki/settings/)
- [Enregistrement](/wiki/recording/)
- [Contrôleurs MIDI](/wiki/midi-controllers/)
- [Dépannage](/wiki/troubleshooting/)
