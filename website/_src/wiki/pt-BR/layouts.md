---
source: e15b0e3ba5
---
![O fluxo por disparo de clipes: uma grade de clipes e cenas à esquerda, o Picker e a Playlist à direita](/assets/img/cliplaunch.webp)

`Exibir › Estilo de layout` tem três entradas: `Fluxo por padrões (padrão)`, `Fluxo por disparo de clipes` e `Fluxo simples`. A mesma escolha está em `Opções › Configurações gerais`, em Fluxo de trabalho. A escolha é mantida quando você fecha o programa.

O fluxo por padrões é o Channel Rack, os patterns e a Playlist. O fluxo por disparo de clipes acrescenta uma grade de clipes com cenas ao lado da Playlist. Os patterns e o Channel Rack continuam disponíveis.

## Disparar clipes na grade

1. Escolha `Exibir › Estilo de layout › Fluxo por disparo de clipes`. A grade de clipes aparece ao lado da Playlist.
2. Clique com o botão direito em um slot vazio, escolha `Colocar um padrão aqui` e selecione um pattern. Dar um clique duplo em um slot vazio coloca ali o pattern atual.
3. Clique no triângulo à esquerda do clipe para disparar. Se o transporte estiver parado, ele começa.
4. Clique no triângulo de uma cena, na coluna `Cenas`, para disparar todos os clipes daquela linha.
5. Clique em `Parar todos os clipes` para pará-los.
6. Clique em `Voltar à Playlist` para deixar as faixas tocarem de novo os clipes da Playlist.

## O que muda na tela

- A grade de clipes aparece ao lado da Playlist. As faixas da Playlist são as colunas e as cenas são as linhas.
- A barra de título da Playlist ganha um seletor `Ambos`, `Grade`, `Song`. `Grade` mostra só a grade, `Song` só a Playlist.
- Na primeira vez, o Channel Rack fica escondido. `F6` ou o botão dele na barra de ferramentas o mostra.
- As faixas de automação da Playlist são ligadas.
- O layout das janelas é guardado por estilo, então cada estilo volta como você o deixou.
- Sob a grade, o botão `Mixer` mostra ou esconde o mixer Session, como no Ableton Live: `Entrada/Saída` (`Áudio de`, `Monitorar`, `Áudio para`), `Envios`, `Atraso da faixa` e o volume de cada faixa com o ativador, o solo, o botão de armar e o medidor, além da faixa `Principal`.
- `Grooves`, sob a grade, mostra o Groove Pool. `Aplicar groove nas notas` grava um groove nas notas de um clipe MIDI.
- Os botões `MIDI` e `Tom` iniciam o MIDI Map Mode e o Key Map Mode. Veja [Controladores MIDI](/wiki/midi-controllers/).

## A grade

Um clipe disparado não começa na hora. Ele espera o próximo ponto de quantização de disparo, e o triângulo dele pisca até lá. A caixa embaixo da grade define a quantização de disparo global, usada por todo clipe configurado como `Global`. Uma parada espera do mesmo jeito.

Clique com o botão direito em um clipe para ver as configurações dele.

| Opção | O que faz |
|---|---|
| `Quantização de disparo` | Quando este clipe começa: `Global`, `Nenhum`, de `8 compassos` até `1/32` |
| `Modo de disparo` | `Gatilho`, `Gate`, `Alternar` ou `Repetições` |
| `Legato (manter a posição)` | O clipe assume a posição de reprodução do clipe que tocava antes naquela faixa |
| `Loop` | Ligado: o clipe se repete. Desligado: toca uma vez e depois a faixa para |
| `Ação seguinte` | O que acontece depois do clipe: duas ações com uma chance cada, e o tempo depois do qual elas agem |
| `Delete` | Esvazia o slot |

Clique com o botão direito em um slot vazio para ver `Botão de parar (uma cena para esta faixa aqui)`. Clicar no quadrado à esquerda de um slot vazio para aquela faixa. Clique com o botão direito em uma cena para ver `Inserir cena`, `Adicionar cena abaixo`, `Excluir cena`, `Renomear cena...` e o tempo da cena.

Os clipes podem ser recortados, copiados, colados e duplicados (`Ctrl+D`) pelo menu deles, e arrastados entre slots e de ou para a Playlist. Um pattern ou um sample arrastado do Browser, ou arquivos de áudio soltos de fora, preenchem o slot em que caem. O menu de uma cena também tem `Duplicar` e `Capturar e inserir cena`, que põe os clipes que estão tocando agora em uma cena nova. `Exibir › Andamento e fórmula de compasso das cenas` mostra um andamento e uma fórmula de compasso para cada cena.

O menu do slot também tem duas opções para a grade inteira: `Devolver as faixas à Playlist quando o transporte parar` e `Ações seguintes em toda a grade`.

| Comando | Tecla | O que faz |
|---|---|---|
| Mover | Setas | Seleciona outro slot |
| Disparar clipe | `Enter` | Dispara o clipe selecionado |
| Disparar cena | `Ctrl+Enter` | Dispara a cena selecionada |
| Apagar clipe | `Del` | Esvazia o slot selecionado |
| Draw Mode | `B` | Um arrasto em uma faixa de automação desenha passos tão largos quanto a grade, ou à mão livre com a grade desligada ou `Alt` pressionado, como no Ableton Live. Também em `Opções` |

## Sets do Ableton Live

Quando você abre um set do Live, os clipes e cenas de Session dele vão para a grade de clipes, com as configurações de disparo. `Arquivo › Exportar › Set do Ableton Live (.als)` grava a grade de volta como clipes de Session. `Opções › Configurações gerais` tem `Clipes de sessão de sets do Live`, em que `Playlist, depois do arranjo` coloca esses clipes na Playlist.

## O fluxo Simples

`Exibir › Estilo de layout › Fluxo simples` é uma janela só com as faixas e a linha do tempo, para uma primeira música. `Exibir › Painel da faixa` mostra um painel sob a linha do tempo para a faixa selecionada, com as abas `Efeitos`, `Instrumento`, `Teclado` e `Editor`. Neste fluxo, o menu `Ajuda` tem `Tour da primeira música` e `Tela de boas-vindas...`, que oferece uma primeira música em sete passos, uma música de demonstração ou um projeto vazio. Veja [Layout simples](/wiki/simple-layout/).

`Letra e anotações` e `Igualar à referência...` no menu `Exibir`, e `Voz limpa` no menu de um insert do Mixer, existem em todos os layouts. No fluxo Simples, `Voz limpa` também está no menu de uma faixa.

## Voltar

Escolha `Exibir › Estilo de layout › Fluxo por padrões (padrão)`. O layout de janelas que você tinha no fluxo por padrões volta, e as faixas de automação retornam à configuração que tinham antes. Os clipes da grade ficam no projeto.

## Bom saber

- Uma faixa que disparou um clipe fica em silêncio na Playlist até você apertar `Voltar à Playlist`. A opção `Devolver as faixas à Playlist quando o transporte parar` faz isso por você a cada parada.
- As teclas novas da grade (`B`, `Q`, `Ctrl+M`, `Ctrl+K` e outras) agem assim só no fluxo por disparo de clipes. No fluxo por padrões elas mantêm o significado do FL Studio.
- Uma cena em 6/8 toca um compasso de três beats, porque o compasso conta semínimas.
- Existem ações seguintes para clipes, não para cenas.
- As faixas de automação ligadas à mão no fluxo por disparo de clipes não são mantidas quando você volta.
- Com o fluxo por padrões selecionado, nada da grade é carregado.

## Veja também

- [Playlist](/wiki/playlist/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)
- [Importar projetos](/wiki/importing-projects/)
- [O que só o LYNX faz: fluxos de trabalho](/why/#workflows)

A terceira entrada é uma janela para a primeira música. Veja [Layout simples](/wiki/simple-layout/).
