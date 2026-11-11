---
source: 1c353ba51c
---
![O fluxo por disparo de clipes: uma grade de clipes e cenas à esquerda, o Picker e a Playlist à direita](/assets/img/cliplaunch.webp)

`Exibir › Estilo de layout` tem duas entradas na 0.1.25: `Fluxo por padrões (padrão)` e `Fluxo por disparo de clipes`. A mesma escolha está em `Opções › Configurações gerais`, em Fluxo de trabalho. A escolha é mantida quando você fecha o programa.

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

O menu do slot também tem duas opções para a grade inteira: `Devolver as faixas à Playlist quando o transporte parar` e `Ações seguintes em toda a grade`.

| Comando | Tecla | O que faz |
|---|---|---|
| Mover | Setas | Seleciona outro slot |
| Disparar clipe | `Enter` | Dispara o clipe selecionado |
| Disparar cena | `Ctrl+Enter` | Dispara a cena selecionada |
| Apagar clipe | `Del` | Esvazia o slot selecionado |

## Sets do Ableton Live

Quando você abre um set do Live, os clipes e cenas de Session dele vão para a grade de clipes, com as configurações de disparo. `Arquivo › Exportar › Set do Ableton Live (.als)` grava a grade de volta como clipes de Session. `Opções › Configurações gerais` tem `Clipes de sessão de sets do Live`, em que `Playlist, depois do arranjo` coloca esses clipes na Playlist.

## Voltar

Escolha `Exibir › Estilo de layout › Fluxo por padrões (padrão)`. O layout de janelas que você tinha no fluxo por padrões volta, e as faixas de automação retornam à configuração que tinham antes. Os clipes da grade ficam no projeto.

## Bom saber

- Uma faixa que disparou um clipe fica em silêncio na Playlist até você apertar `Voltar à Playlist`. A opção `Devolver as faixas à Playlist quando o transporte parar` faz isso por você a cada parada.
- Na 0.1.25, um slot é preenchido pelo menu dele ou por clique duplo. Não dá para arrastar um clipe ou arquivo para um slot.
- Existem ações seguintes para clipes, não para cenas.
- As faixas de automação ligadas à mão no fluxo por disparo de clipes não são mantidas quando você volta.
- Com o fluxo por padrões selecionado, nada da grade é carregado.

## Veja também

- [Playlist](/wiki/playlist/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)
- [Importar projetos](/wiki/importing-projects/)
- [O que só o LYNX faz: fluxos de trabalho](/why/#workflows)

Um layout simplificado para iniciantes não existe na 0.1.25. Veja [Layout simples](/wiki/simple-layout/).
