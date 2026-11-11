---
source: 3801d5daff
---
![A página Áudio da janela Configurações, ainda sem dispositivo de saída escolhido](/assets/img/wiki/settings-audio.webp)

A página Áudio da janela Configurações define para onde vai o som e de onde vêm as gravações. Abra-a com `Opções › Configurações de áudio...`. O assistente de configuração (`Opções › Assistente de configuração (idioma, áudio, pastas de plugins)...`) também testa a saída de áudio.

## Colocar som para fora

1. Abra `Opções › Configurações de áudio...`.
2. Escolha um `Driver`. A nota embaixo da caixa diz para que serve esse driver.
3. Escolha um `Dispositivo de saída`.
4. Clique em `Tocar um som de teste`.
5. Leia a seção `Estado`. Ela mostra o driver em uso, a taxa de amostragem e a latência de saída, ou `Nenhum dispositivo de áudio aberto: escolha um dispositivo de saída.`

## Drivers

A caixa `Driver` lista os tipos de driver do seu sistema.

| Sistema | Driver | O que faz |
|---|---|---|
| Windows | Windows Audio (shared) | Os outros programas mantêm o som. O Windows mistura todos os programas em passos fixos, então o buffer não pode ficar abaixo desse passo |
| Windows | Windows Audio (shared, low-latency period) | Compartilhado, mas pede ao Windows um período de mixagem menor. Só alguns drivers aceitam |
| Windows | Windows Audio (exclusive, lowest latency) | O LYNX SHARE usa o dispositivo sozinho. Os outros programas ficam mudos enquanto ele toca |
| Windows | DirectSound (legacy) | Caminho de áudio antigo do Windows, com latência alta. Para quando os outros falham |
| Windows | ASIO | Drivers de baixa latência. O driver ASIO do fabricante funciona com o dispositivo dele |
| Windows | LYNX ASIO (built in) | O driver ASIO próprio do LYNX SHARE, para qualquer dispositivo, em reprodução e gravação. `Configurações do driver...` escolhe o modo compartilhado ou exclusivo e os dispositivos |
| macOS | Core Audio | O áudio do próprio sistema, compartilhado com os outros programas |
| Linux | PipeWire (native) | O LYNX SHARE fala direto com o PipeWire. `System default` segue a saída escolhida nas configurações de som do sistema |
| Linux | JACK / PipeWire | Para um servidor JACK em execução, ou PipeWire com `pipewire-jack` |
| Linux | ALSA | Fala com a placa de som. `default` ou `pipewire` compartilham o dispositivo; um dispositivo `hw:` toma a placa só para o LYNX SHARE |

No Linux, a primeira inicialização abre o primeiro entre PipeWire, JACK e ALSA que tiver dispositivos. A escolha salva é mantida.

## As opções da página

| Opção | O que faz |
|---|---|
| `Driver` | O tipo de driver, da tabela acima |
| `Dispositivo de saída` | Onde o Master toca |
| `Dispositivo de entrada` | A fonte da gravação de áudio e das entradas dos inserts do Mixer. `(nenhum)` por padrão |
| `Taxa` | As taxas que o dispositivo oferece |
| `Tamanho do buffer` | Os tamanhos que o dispositivo oferece, cada um com a latência em milissegundos. Buffers menores respondem mais rápido quando você toca. Os maiores dão mais folga à CPU, com menos estalos |
| `Configurações do driver...` | Abre a janela própria do driver (drivers ASIO) |
| `Tocar um som de teste` | Toca um som de teste pelo dispositivo de saída |
| `Gravar áudio da entrada` | Grava os dois primeiros canais de entrada em um novo insert quando nenhum insert está armado |
| `Formato do arquivo` | As gravações são WAV de 24 bits ou WAV de 32 bits float, na taxa de amostragem do dispositivo |
| `Medir a ida e volta` | Toca 8 cliques de `Saída do ping` para `Entrada do ping` por um cabo ou loopback e mede o atraso, para que os takes caiam onde você tocou |
| `Ajuste fino (amostras)` | Desloca todo take novo. Positivo é mais cedo na Playlist |
| `Faixa do metrônomo` | A faixa do Mixer por onde o metrônomo toca. `---` é o Master. O clique nunca entra numa exportação |
| `Tocar notas truncadas no transporte` | Toca as notas que cruzam a posição inicial |
| `Reiniciar plugins no transporte` | Limpa as caudas dos efeitos quando a reprodução começa ou salta |

## Quando não há som

- Olhe primeiro o `Estado`. Se nenhum dispositivo estiver aberto, escolha um dispositivo de saída.
- Teste outro driver. No Windows, `Windows Audio (compartilhado)` deixa os outros programas com som, e `DirectSound (antigo)` existe para quando os outros falham.
- Confira o fader do Master no [Mixer](/wiki/mixer/) e o knob `MASTER` na barra de ferramentas.
- No modo Pattern só toca o pattern atual. Se ele estiver vazio, você não ouve nada. Aperte `L` para o modo Song.
- No Linux, se o som falhar, o `Estado` mostra se a thread de áudio recebeu prioridade de tempo real. A página [Solução de problemas](/wiki/troubleshooting/) tem a correção.

## Bom saber

- O ASIO existe só no Windows.
- Com `Windows Audio (compartilhado)`, o menor buffer é o passo de mixagem do sistema. A nota embaixo da caixa `Driver` dá o valor para o seu dispositivo. Para menos latência, use o modo exclusivo ou um driver ASIO.
- Com o PipeWire, o buffer vai de 32 a 2048 amostras e a taxa de amostragem de 44,1 a 192 kHz. Se o PipeWire mantiver a taxa dele, ele reamostra o fluxo do LYNX SHARE.
- Com o PipeWire, quando um dispositivo nomeado é desconectado, o LYNX SHARE volta para `System default` e mantém o tamanho do buffer.
- O `Estado` também mostra a carga de CPU do áudio e conta as falhas (buffer underruns).
- Você pode listar os drivers e dispositivos sem abrir a janela. Veja `--devices` em [Linha de comando](/wiki/command-line/).

## Veja também

- [Configurações](/wiki/settings/)
- [Gravação](/wiki/recording/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Solução de problemas](/wiki/troubleshooting/)
