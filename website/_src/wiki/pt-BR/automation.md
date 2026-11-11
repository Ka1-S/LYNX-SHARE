---
source: b96330a448
---
![O editor de automação com uma curva que sobe ao longo de seis compassos](/assets/img/wiki/automation.webp)

Um clipe de automação é uma curva na [Playlist](/wiki/playlist/) que move um controle enquanto a música toca: o volume de um canal, o fader de um mixer, um parâmetro de plugin, o tempo. O clipe tem uma janela de edição própria, o editor de automação.

Há três outras formas de mover um controle. Eventos são valores guardados dentro de um pattern. Um vínculo faz um knob MIDI ou um controlador interno comandar o controle. Um modulador faz um knob oscilar em torno do próprio valor.

## Automatizar um knob

1. Leve o cursor de reprodução até onde a automação deve começar, ou selecione um intervalo de tempo na Playlist.
2. Clique com o botão direito no knob ou fader e escolha `Criar clipe de automação`.
3. O clipe cai na primeira faixa vazia da Playlist e o editor de automação abre. A curva começa como uma linha reta no valor atual do controle.
4. Clique no editor para adicionar um ponto e arraste-o para o lugar.
5. Segure `Alt` e arraste um segmento para cima ou para baixo para curvá-lo.
6. Toque a música no modo Song. O controle segue a curva.

Outras formas de obter o mesmo clipe:

- `Adicionar › Automação do último parâmetro mexido` cria um clipe para o controle que você mexeu por último.
- O indicador de último mexido e `Ferramentas › Último mexido` abrem o mesmo menu do clique direito no controle.
- Para um plugin de terceiros, abra o menu do canal no Channel Rack ou o menu do slot de efeito no Mixer e use `Automatizar parâmetro do plugin`. `Automatizar controlador MIDI (CC, aftertouch, pitch bend)` fica ao lado.

Para abrir o editor de novo, dê um clique duplo no clipe, ou clique nele com o botão direito e escolha `Editar pontos...`. `Editar clipe de automação...`, no menu do próprio controle, faz o mesmo.

## O editor de automação

| Comando | Tecla | O que faz |
|---|---|---|
| Novo ponto | Clique ou clique direito, depois arraste | Adiciona um ponto e o move enquanto você segura o botão |
| Mover um ponto | Arrastar | `Shift` mantém o tempo dele, `Ctrl` mantém o valor, `Alt` ignora a grade |
| Curvar um segmento | `Alt`+arrastar para cima ou para baixo | Muda a tensão do segmento sob o mouse |
| Alça de tensão | Arrastar para cima ou para baixo | Curva o segmento. `Ctrl` é mais fino, o clique direito deixa reto |
| Ponto na curva | `Shift`+clique direito | Adiciona um ponto na linha |
| Menu do ponto | Clique direito em um ponto | `Digitar valor...`, `Copiar valor`, `Colar valor`, `Apagar ponto` e `Curva inteira` para escalar, inverter ou endireitar tudo |
| Zoom | `Ctrl`+roda | Dá zoom na vista |
| Rolagem | Arrastar com o botão do meio | Move a vista |

## Modo LFO

![O editor de automação no modo LFO, com uma onda triangular e os knobs SPD, TNS, SKEW, PW e AMNT](/assets/img/wiki/automation-lfo.webp)

Marque `LFO` no alto do editor. Agora uma forma de onda molda o valor do clipe, e os pontos que você desenha definem a amplitude do LFO, não o valor em si.

| Opção | O que faz |
|---|---|
| `SPD` | A duração de um ciclo, em tempos |
| `TNS` | Tensão da onda |
| `SKEW` | Inclinação da onda |
| `LP` | Largura de pulso |
| `AMNT` | Quanto o LFO move o valor |
| `MUL` | Ligado: o LFO é multiplicado pelo nível do envelope. Desligado: é somado |

## Eventos em um pattern

![O Editor de eventos com eventos de volume desenhados como barras](/assets/img/wiki/eventeditor.webp)

Os eventos pertencem a um pattern e tocam onde quer que esse pattern toque. Clique com o botão direito em um controle e escolha `Editar eventos no pattern` para abrir o Editor de eventos dele.

- `Desenhar` define valores enquanto você arrasta, e arrastar com o botão direito apaga. `Linha` desenha uma rampa reta. `Selecionar` marca um intervalo de tempo para as ferramentas de baixo.
- `Grade` vai de 1/2 step a 1 compasso.
- `Escala`, `Deslocamento` e `Aleatorizar` alteram os eventos da seleção, ou do pattern inteiro.
- `LFO` escreve uma onda senoidal, triangular ou quadrada de 1 tempo a 4 compassos.
- `Limpar` remove os eventos.
- `Para automação...` transforma os eventos em um clipe de automação e os remove do pattern.
- `Arquivo` abre e salva os eventos como um arquivo `.fsc` e importa um arquivo MIDI.

`Iniciar a música com esta posição`, no mesmo menu do clique direito, guarda o valor para o qual o controle volta sempre que a música começa.

## Gravar seus movimentos

Ligue a gravação com `Automação` marcado no filtro de gravação, aperte play e mova o controle. No modo Song, os movimentos viram um clipe de automação. No modo Pattern, viram eventos no pattern atual. Veja [Gravação](/wiki/recording/).

## Vínculos e moduladores

- `Vincular ao controlador MIDI (mova um knob no controlador)` está em [Controladores MIDI](/wiki/midi-controllers/).
- `Vincular a controlador interno` deixa um Envelope, Formula ou Peak Controller do projeto comandar o controle. Adicione um deles antes.
- `Modular` anexa um modulador, um LFO ou uma fonte aleatória que move um knob do LYNX SHARE em torno do próprio valor. `Ferramentas › Moduladores...` lista os moduladores.

## Faixas de automação

As faixas vêm desligadas por padrão. Ligue `Faixas de automação` no menu da Playlist. Cada cabeçalho de faixa passa a ter um botão que adiciona uma faixa de automação embaixo, e o menu do controle ganha `Automatizar em faixa`. Numa faixa de automação, clique para adicionar ou arraste um ponto, clique com o botão direito para apagar um, e `Alt`+arrastar para curvar a linha. O menu da faixa tem `Para clipe de automação`.

## Bom saber

- Um clipe novo começa na seleção de tempo, se houver, senão no cursor de reprodução no modo Song. Desligue `Criar clipes de automação na posição de reprodução` em `Opções › Configurações gerais...` para começar no início da música.
- Vários clipes em um mesmo controle tocam cada um no próprio lugar. Antes do primeiro clipe, o controle mantém o próprio valor.
- No início de um clipe, o controle chega ao novo valor em cerca de 8 ms. O FL Studio chega deslizando, então um projeto importado pode diferir um pouco nesses pontos.
- Onde clipes do mesmo controle se sobrepõem, o clipe que o LYNX SHARE escolhe pode ser diferente do que o FL Studio escolhe.
- Os moduladores só funcionam nos knobs do próprio LYNX SHARE e ficam de fora quando você exporta para `.flp`.

## Veja também

- [Playlist](/wiki/playlist/)
- [Gravação](/wiki/recording/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Atalhos](/wiki/shortcuts/)
