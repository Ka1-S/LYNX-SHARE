---
source: 297095d385
---
## Driver e saída

![A página Audio das Configurações](/assets/img/learn/audio-setup-1.webp)

1. Abra `Opções › Configurações de áudio...`.
2. Escolha o driver. No Windows, `LYNX ASIO (embutido)` funciona com qualquer dispositivo e não precisa de driver próprio; o driver ASIO da sua interface, o FL Studio ASIO e `Windows Audio (compartilhado)` ou `Windows Audio (exclusivo, menor latência)` também estão lá. Core Audio no Mac, PipeWire ou ALSA no Linux.
3. `Dispositivo de saída` é onde você ouve o LYNX. `System default` segue a saída que seu computador usa.
4. `Dispositivo de entrada` é de onde você grava (um microfone, uma entrada de guitarra). `Taxa` (a taxa de amostragem) costuma ser 44100 ou 48000 Hz.

## Tamanho do buffer

![Os tamanhos de buffer, com a latência](/assets/img/learn/audio-setup-2.webp)

1. `Tamanho do buffer`: um buffer menor responde mais rápido quando você toca um teclado; um maior dá mais folga para a CPU.
2. Estalos ou falhas? Aumente. As notas saem atrasadas quando você toca? Diminua.

## Teste o som

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Tocar um som de teste` toca um som curto pela saída escolhida.
2. De qualquer lugar, `Ajuda › Testar saída de áudio...` faz o mesmo.

## Takes gravadas atrasadas?

1. Sua interface precisa de um instante para trazer o som e mandá-lo de volta, então uma take pode cair um pouco atrasada.
2. Na mesma página Audio, em `Compensação de latência da gravação`, ligue uma saída a uma entrada com um cabo (ou use o loopback da sua interface) e clique em `Medir a ida e volta`.
3. O LYNX toca alguns cliques, mede quanto atrasados eles voltam e, a partir daí, coloca as takes gravadas no tempo.

Veja também [Configuração de áudio](/wiki/audio-setup/) e [Solução de problemas](/wiki/troubleshooting/).
