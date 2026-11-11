---
source: b2de12e24d
---
![O Mixer](/assets/img/mixer.webp)

Todo canal toca por um insert do Mixer. Cada insert tem fader, pan, mudo e solo, um medidor de pico, dez slots de efeito, cada um com uma quantidade seco/molhado, e um EQ embutido. Todos os inserts terminam no Master, a menos que você os roteie para outro lugar. Aperte `F9` para mostrar ou esconder o Mixer.

Um projeto pode ter até 127 inserts. O Mixer tem uma faixa de adição para criar um novo. A caixa na barra de título do Mixer escolhe um de oito layouts de faixa, de `Compacta` a `Extra grande`.

## Pôr um efeito em um canal

1. No [Channel Rack](/wiki/channel-rack/), selecione o canal e aperte `Ctrl+L`. O canal vai para um insert livre, que recebe o nome e a cor dele.
2. Aperte `F9` para mostrar o Mixer e clique na faixa desse insert.
3. No painel de efeitos, clique em um slot vazio e escolha um efeito.
4. Clique no nome do slot para mostrar ou esconder a janela do efeito.
5. Clique na luz do slot para pôr o efeito em bypass e comparar.

## Enviar um canal para um insert

No Channel Rack, arraste a caixa de rota do canal (ou use a roda sobre ela) para escolher o número do insert. Um clique duplo na caixa mostra esse insert no Mixer.

`Ctrl+L` no Channel Rack funciona com vários canais selecionados de uma vez. Cada um vai para o seu próprio insert livre.

Com o foco no Mixer, `Ctrl+L` vincula os canais selecionados ao insert selecionado, e `Ctrl+Shift+L` os vincula a inserts consecutivos a partir dele.

## Selecionar e mover inserts

- Clique em uma faixa para selecioná-la.
- `Ctrl`+clique, ou arrastar por várias faixas, seleciona um intervalo. `Ctrl+Shift`+clique adiciona ou tira uma.
- Arraste uma faixa para o lado, ou aperte `Alt+Left` e `Alt+Right`, para mover os inserts selecionados.
- Clique com o botão direito em uma faixa para abrir o menu do insert. `F2` renomeia e colore o insert selecionado.

Mover, inserir ou apagar inserts mantém os envios, sidechains e a automação no insert deles.

## Mudo e solo

Clique na luz de mudo para silenciar um insert. Clique com o botão direito ou `Ctrl`+clique nela para dar solo. `S` dá solo nos inserts selecionados, e `Alt+S` dá solo neles junto com os inserts que os alimentam.

## Efeitos

O painel de efeitos mostra os dez slots do insert selecionado.

- Arraste um slot, ou use a roda sobre ele, para movê-lo para cima ou para baixo.
- Clique com o botão direito em um slot para ver as opções dele (substituir, mover, apagar).
- `Del` apaga o plugin do slot selecionado.
- O interruptor no cabeçalho do painel liga ou desliga todos os efeitos do insert.
- Você pode arrastar um efeito do banco de plugins do [Browser](/wiki/browser/) para um insert ou slot de efeito.

## EQ do insert

Cada insert tem o próprio EQ. Arraste uma banda para definir a frequência e o nível dela, use a roda para a largura e dê um clique duplo em uma banda para voltá-la a 0 dB. Clique com o botão direito no título do EQ para pô-lo em bypass. Se você quer um Mixer sem EQ embutido, os projetos novos podem ser configurados para começar com um mixer limpo.

## Roteamento, envios e sidechain

![O Mixer com o insert Kick selecionado, seus envios desenhados como cabos sob as faixas e o painel de efeitos à direita](/assets/img/wiki/mixer-sends.webp)

Selecione o insert que você quer rotear. Cada uma das outras faixas passa a ter um interruptor de rota:

- Clique no interruptor de outra faixa para enviar o insert selecionado para lá. Arrastar depois de ativar define o nível do envio.
- `Ctrl`+clique roteia só para aquele insert.
- `Shift`+clique cria um envio de sidechain. `Ctrl+Shift`+clique cria um só de sidechain.
- O clique com o botão direito abre as mesmas escolhas como menu.

Efeitos com entrada lateral, como um compressor, têm um seletor `Sidechain de` que lista os inserts a escutar. Escolher um cria a rota para você.

## Grupos e faders VCA

Os grupos do Mixer se recolhem com o chevron (ou `Alt`+clique para todos os grupos). Um fader VCA move o nível dos inserts membros enquanto eles mantêm os próprios faders. Crie um pelo menu do insert: em Group, escolha `VCA › Novo VCA dos selecionados`.

## Gravação

Cada insert tem um botão de armar gravação. Com um insert armado, Rec e Play gravam a entrada de áudio dele em um clipe da Playlist. `Shift+R` arma o insert selecionado e começa a gravar em um só passo. Veja [Gravação](/wiki/recording/).

## Teclas

`Ctrl+L` e `Ctrl+Shift+L` têm este significado só quando o Mixer está com o foco.

| Comando | Tecla | O que faz |
|---|---|---|
| Vincular canais | `Ctrl+L` | Vincula os canais selecionados ao insert selecionado |
| Vincular a inserts consecutivos | `Ctrl+Shift+L` | Vincula os canais selecionados a inserts a partir do selecionado |
| Mover inserts | `Alt+Left` / `Alt+Right` | Move os inserts selecionados |
| Renomear e colorir | `F2` | Renomeia e colore o insert selecionado |
| Solo | `S` | Dá solo nos inserts selecionados |
| Solo com as fontes | `Alt+S` | Dá solo nos inserts selecionados e nos inserts que os alimentam |
| Apagar efeito | `Del` | Apaga o plugin do slot selecionado |
| Armar e gravar | `Shift+R` | Arma o insert selecionado e começa a gravar |

## Bom saber

- O `Renderizar em áudio` (um insert convertido em clipe de áudio) toca cerca de 5 dB mais alto que o insert que ele substitui. É um problema conhecido nas notas da versão 0.1.25.
- `Ctrl+L` faz coisas diferentes no Channel Rack e no Mixer. No rack, ele escolhe inserts livres. No Mixer, usa o insert selecionado.
- Os interruptores de rota aparecem nas outras faixas, não na selecionada.
- O limite é de 127 inserts por projeto e dez slots de efeito por insert.

## Veja também

- [Channel Rack](/wiki/channel-rack/)
- [Gravação](/wiki/recording/)
- [Automação](/wiki/automation/)
- [Plugins de terceiros](/wiki/plugins/)
- [Instrumentos e efeitos](/instruments-and-effects/)
