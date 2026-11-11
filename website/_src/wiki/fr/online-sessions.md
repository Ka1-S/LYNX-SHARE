---
source: 27354bbca9
---
Il y a deux façons de travailler à plusieurs sur un projet, et les deux sont sous `Fichier › Session en ligne`. Un paquet est un fichier que vous envoyez à un ami, qui vous renvoie une partie. Il ne demande ni réseau, ni compte, ni serveur. Une session en direct relie tout le monde par internet via le relais LYNX SHARE, et chaque modification parvient à tous. Jusqu'à 4 personnes participent à un projet, vous compris.

## Envoyer un projet à un ami

![Envoyer le projet](/assets/img/wiki/session-send.webp)

1. Ouvrez le projet et choisissez `Fichier › Session en ligne › Envoyer le projet...`.
2. Saisissez votre nom et celui de votre ami.
3. Choisissez ce que votre ami garde à la fin de la session : `Seulement sa propre partie (recommandé)` ou `Une copie complète du projet`.
4. Enregistrez le fichier `.lynxpack`.
5. Envoyez le fichier comme vous voulez : e-mail, dossier partagé, clé USB.

Le paquet contient le projet, tous les samples et enregistrements qu'il utilise, et des rendus audio (« remplaçants ») des parties qui utilisent un plugin tiers. Votre ami entend ces parties sans posséder vos plugins.

## Enregistrer une partie et la renvoyer

L'ami fait ceci :

1. Ouvrez le `.lynxpack` avec `Fichier › Ouvrir...`. Un résumé indique qui l'a envoyé et quels inserts du Mixer et quelles pistes de Playlist sont à vous. Cliquez sur `Ouvrir`.
2. Ajoutez vos propres canaux, patterns et audio sur vos inserts et vos pistes de Playlist. Le matériel de l'hôte reste tel quel.
3. Choisissez `Fichier › Session en ligne › Renvoyer ma partie...` et envoyez le nouveau paquet à l'hôte.

Chaque invité reçoit une couleur, 20 inserts du Mixer et 16 pistes de Playlist.

## Fusionner une partie

L'hôte fait ceci :

1. Ouvrez le projet qui a été envoyé.
2. Choisissez `Fichier › Session en ligne › Fusionner une partie...` et sélectionnez le paquet que votre ami a renvoyé.
3. Les canaux, patterns, notes, clips et inserts de l'ami sont ajoutés dans sa couleur. Rien de ce qui est à vous n'est modifié.

La fusion compte pour une seule étape d'annulation. Une partie plus récente du même ami remplace la précédente.

## Démarrer ou rejoindre une session en direct

![Démarrer une session en direct](/assets/img/wiki/session-start.webp)

L'hôte démarre la session :

1. Ouvrez le projet et choisissez `Fichier › Session en ligne › Démarrer une session en direct...`.
2. Saisissez votre nom.
3. Sous `Les personnes invitées peuvent modifier`, choisissez `Seulement ses propres canaux, patterns, inserts et lignes (recommandé)` ou `Tout`. Sous `À la fin de la session, les personnes invitées gardent`, choisissez leur propre partie ou une copie complète.
4. Cliquez sur `Début`, puis copiez le code d'invitation et envoyez-le à 3 personnes au plus.
5. Quand quelqu'un demande à rejoindre, cliquez sur `Faire entrer`.

Un invité rejoint la session :

1. Choisissez `Fichier › Session en ligne › Rejoindre une session en direct...`.
2. Saisissez votre nom, collez le code d'invitation et cliquez sur `Rejoindre`.
3. Attendez que l'hôte vous fasse entrer. Le projet de l'hôte s'ouvre.

## Pendant une session en direct

![Chat de la session](/assets/img/wiki/session-chat.webp)

Le menu `Fichier › Session en ligne` contient ces commandes tant qu'une session est en cours :

| Commande | Ce qu'elle fait |
|---|---|
| `Chat de la session...` | Ouvre le chat. L'entrée de menu affiche le nombre de messages non lus |
| `Suivre le transport de l'hôte` | Pour les invités : la lecture, l'arrêt et la position suivent l'hôte |
| `Personnes et autorisations...` | Pour l'hôte : ce que chaque personne peut modifier, et ce que chacune garde à la fin |
| `Afficher le code d'invitation...` | Pour l'hôte : affiche de nouveau le code, avec un bouton `Copie` |
| `Mettre fin à la session en direct` | Pour l'hôte. Tout le monde part, et le code cesse de fonctionner |
| `Quitter la session en direct` | Pour un invité. Votre partie du projet reste chez vous |

Dans `Personnes et autorisations...`, chaque personne reçoit l'un de quatre rôles : `Ses propres canaux, patterns, inserts et lignes`, `Ses propres parties + tempo et réglages du projet`, `Tout` ou `Écoute seule (aucune modification)`. Retirer une autorisation ne supprime jamais rien.

## Les plugins que les autres n'ont pas

Un canal ou un insert dont le plugin n'est pas installé sur votre ordinateur joue un remplaçant, et affiche un badge ambre `R` dans le Channel Rack et le Mixer.

- Dans un paquet, les remplaçants sont des rendus audio faits à l'enregistrement du paquet. Ils suivent les clips que vous déplacez, mais pas les modifications des notes d'un plugin que vous n'avez pas. Demandez un nouveau paquet à son propriétaire.
- Dans une session en direct, les parties qui utilisent un plugin que quelqu'un n'a pas sont rendues par la personne qui l'a et diffusées aux autres sous forme d'audio.

## Ce qui passe par le réseau

Les paquets sont des fichiers. Rien n'est envoyé nulle part.

Une session en direct ne se connecte que pendant que vous y êtes. Tout passe par le relais `relay.lynx-share.com`, chiffré de bout en bout avec une clé issue du code d'invitation. Le relais transmet les données sans pouvoir les lire. Il voit votre adresse IP, quand vous étiez connecté et combien d'octets sont passés. Les autres personnes de la session voient votre nom, vos modifications, les samples que votre partie utilise et si vous lisez ou enregistrez. Elles ne voient pas votre adresse IP. Les détails sont dans la [politique de confidentialité](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Bon à savoir

- `Seulement sa propre partie` est une courtoisie entre musiciens, pas une protection contre la copie. Le paquet est un zip normal.
- Les deux côtés devraient utiliser la même version de LYNX SHARE. Le résumé du paquet avertit quand elles diffèrent.
- Un invité dont la connexion tombe se reconnecte tout seul et garde ses modifications.
- Si LYNX SHARE se ferme chez l'hôte pendant une session, le démarrage suivant propose de la reprendre, et les invités se reconnectent tout seuls.
- Dans une session en direct, annuler et rétablir ne ramènent que vos propres modifications.
- Toute personne qui a le code d'invitation peut demander à rejoindre, mais seulement après que l'hôte l'a fait entrer.

## Voir aussi

- [Ce que seul LYNX SHARE fait : les sessions en ligne](/why/#online)
- [Formats de fichiers](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Annuler et historique](/wiki/undo-history/)
- [Dépannage](/wiki/troubleshooting/)
