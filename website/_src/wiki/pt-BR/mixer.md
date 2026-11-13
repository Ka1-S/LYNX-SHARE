---
source: b6e9ef0dd0
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
- `Del`, ou `Delete` no menu do insert, exclui os inserts selecionados. O LYNX SHARE pergunta antes. Marque `Lembrar minha escolha` para pular a pergunta da próxima vez.

Mover, inserir ou apagar inserts mantém os envios, sidechains e a automação no insert deles.

A seção `Mixer` do menu do insert e `Opções › Mixer` têm mais dois comandos. `Organizar inserts como a Playlist` leva para a frente os inserts ligados a faixas da Playlist, na ordem da Playlist. Com `Manter ligações para inserts excluídos, em cinza (LYNX)` ligado, os envios que iam para um insert excluído continuam, em cinza, para você poder conectá-los de novo.

## Mudo e solo

Clique na luz de mudo para silenciar um insert. Clique com o botão direito ou `Ctrl`+clique nela para dar solo. `S` dá solo nos inserts selecionados, e `Alt+S` dá solo neles junto com os inserts que os alimentam.

Um insert mudo continua com o medidor se movendo. Em projetos novos e em projetos importados do FL Studio, ele também continua rodando seus efeitos, e só a saída para o Master e os envios são cortados. Para impedir que um insert mudo processe, ligue `Inserts mutados não processam (LYNX, economiza CPU; este projeto)` na seção `Mixer` do menu do insert ou em `Opções › Mixer`. O medidor dele então para, e os efeitos recomeçam do zero quando você tira o mudo.

## Efeitos

O painel de efeitos mostra os dez slots do insert selecionado.

- Arraste um slot, ou use a roda sobre ele, para movê-lo para cima ou para baixo.
- Clique com o botão direito em um slot para ver as opções dele (substituir, mover, apagar).
- Com o menu de um slot aberto, `Del` exclui o plugin desse slot.
- `Agrupar em rack de efeitos de áudio` no menu do slot coloca o efeito em um novo rack, e `Agrupar tudo em rack de efeitos de áudio` faz isso com a cadeia inteira. `Desagrupar` devolve aos slots os efeitos de um rack de uma só cadeia.
- O interruptor no cabeçalho do painel liga ou desliga todos os efeitos do insert.
- Você pode arrastar um efeito do banco de plugins do [Browser](/wiki/browser/) para um insert ou slot de efeito.
- `Voz limpa` no menu do insert põe no insert uma cadeia pronta de limpeza de vocal.

## Edison em um slot

O Edison, o editor e gravador de áudio, pode ficar em qualquer slot de efeito, vários por insert. A janela dele é o editor de áudio com uma linha de controles de gravação.

- `Abrir editor de áudio` (`Ctrl+E`) no menu do insert carrega o Edison no primeiro slot vazio do insert e o abre. `Abrir editor de áudio e gravar` (`Shift+E`) o carrega pronto para gravar quando a música toca.
- Os modos de gravação são `Agora`, `Na entrada` (o padrão: espera a entrada passar de um limiar), `Entrada` e `Ao tocar`. `Anexar` põe uma gravação nova depois do áudio que já está lá. O tempo máximo de gravação é `Sem limite` ou 1, 3, 5, 10, 30 ou 60 minutos: um slot novo começa em 30, `Abrir editor de áudio e gravar` define 5.
- `Sync` toca o áudio do Edison no tempo da música enquanto a música toca.
- O Edison continua gravando depois de você fechar a janela dele.
- `Ferramentas › Macros › Remover todas as instâncias do Edison` esvazia todos os slots do Edison do projeto, depois de uma pergunta.
- Slots do Edison em projetos do FL Studio (.flp) abrem com o áudio deles.

## EQ do insert

Cada insert tem o próprio EQ. Arraste uma banda para definir a frequência e o nível dela, use a roda para a largura e dê um clique duplo em uma banda para voltá-la a 0 dB. Clique com o botão direito no título do EQ para pô-lo em bypass. Se você quer um Mixer sem EQ embutido, os projetos novos podem ser configurados para começar com um mixer limpo.

## Roteamento, envios e sidechain

![O Mixer com o insert Kick selecionado, seus envios desenhados como cabos sob as faixas e o painel de efeitos à direita](/assets/img/wiki/mixer-sends.webp)

Selecione o insert que você quer rotear. Cada uma das outras faixas passa a ter um interruptor de rota:

- Clique no interruptor de outra faixa para enviar o insert selecionado para lá. Arrastar depois de ativar define o nível do envio.
- `Ctrl`+clique roteia só para aquele insert.
- `Shift`+clique cria um envio de sidechain. `Ctrl+Shift`+clique cria um só de sidechain.
- O clique com o botão direito abre as mesmas escolhas como menu.
- Para um envio pré-fader, clique com o botão direito na chave de rota ou no knob de nível do envio e marque `Envio pré-fader (o fader desta trilha não o altera)`. O envio fica então marcado como `PRE`.

Efeitos com entrada lateral, como um compressor, têm um seletor `Sidechain de` que lista os inserts a escutar. Escolher um cria a rota para você.

## Grupos e faders VCA

Os grupos do Mixer se recolhem com o chevron (ou `Alt`+clique para todos os grupos). Um fader VCA move o nível dos inserts membros enquanto eles mantêm os próprios faders. Crie um pelo menu do insert: em Group, escolha `VCA › Novo VCA dos selecionados`.

## Gravação

Cada insert tem um botão de armar gravação. Com um insert armado, Rec e Play gravam a entrada de áudio dele em um clipe da Playlist. `Shift+R` arma o insert selecionado e começa a gravar em um só passo. Veja [Gravação](/wiki/recording/).

Quando você escolhe uma entrada externa para um insert que também grava outro áudio, o LYNX SHARE pergunta se deve trocá-lo para `Só a entrada externa`, para que o take contenha só a entrada. `Mixer › Gravação em disco › Compensação de latência` no menu do insert vem ligado por padrão. Desligado, os takes gravados são colocados sem nenhuma compensação de latência.

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
| Excluir inserts | `Del` | Exclui os inserts selecionados, depois de uma pergunta |
| Armar e gravar | `Shift+R` | Arma o insert selecionado e começa a gravar |
| Open audio editor | `Ctrl+E` | Carrega o Edison no primeiro slot vazio do insert e o abre |
| Open audio editor and record | `Shift+E` | O mesmo, pronto para gravar quando a música toca |

## Bom saber

- `Medidor de loudness ao lado do Master (LYNX)`, em `Opções › Mixer`, mostra o loudness de curto prazo e integrado (LUFS) e o true peak da saída do Master, com um botão `Restaurar`.
- Um plugin de instrumento com várias saídas pode tocar cada saída em um insert próprio. Veja [Plugins de terceiros](/wiki/plugins/).
- `Ctrl+L` faz coisas diferentes no Channel Rack e no Mixer. No rack, ele escolhe inserts livres. No Mixer, usa o insert selecionado.
- Os interruptores de rota aparecem nas outras faixas, não na selecionada.
- O limite é de 127 inserts por projeto e dez slots de efeito por insert.
- A gravação do Edison é guardada como arquivo WAV na pasta do projeto, não dentro do arquivo do projeto.
- O mixer Session sob a grade de clipes pertence só ao fluxo por disparo de clipes. Ele mostra os mesmos inserts deste Mixer. Veja [Layouts](/wiki/layouts/).

## Veja também

- [Channel Rack](/wiki/channel-rack/)
- [Gravação](/wiki/recording/)
- [Automação](/wiki/automation/)
- [Plugins de terceiros](/wiki/plugins/)
- [Instrumentos e efeitos](/instruments-and-effects/)
