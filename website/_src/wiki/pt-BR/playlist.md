---
source: e767f4482b
---
![A Playlist com clipes nas faixas](/assets/img/playlist.webp)

A Playlist é onde os patterns viram uma música. Ela guarda três tipos de clipes nas faixas: clipes de pattern, clipes de áudio e clipes de automação. Pressione `F5` para mostrar ou ocultar, e `Enter` para maximizar ou restaurar.

O transporte tem dois modos. O modo Pattern toca o pattern atual em loop. O modo Song toca a Playlist. Pressione `L` para alternar.

## Posicionar um pattern na música

1. Pressione `F5` para mostrar a Playlist.
2. No painel Picker, clique em um pattern. O Picker lista os patterns, áudios e automações do projeto, e o que você clica vira a fonte de clipes.
3. Pressione `P` para o Lápis.
4. Clique em uma faixa para posicionar o clipe. Mantenha o botão pressionado e arraste para ajustar a posição.
5. Pressione `B` para o Pincel e arraste ao longo de uma faixa para pintar clipes um depois do outro.
6. Pressione `L` para o modo Song e depois `Space` para tocar.

Arquivos de áudio podem ser arrastados do [Browser](/wiki/browser/) para a Playlist.

## Ferramentas

![A barra de ferramentas da Playlist com a caixa de arranjo, as ferramentas e a caixa de grade](/assets/img/wiki/playlist-tools.webp)

As letras funcionam enquanto o piano do teclado de digitação (`Ctrl+T`) está desligado.

| Ferramenta | Tecla | O que faz |
|---|---|---|
| Lápis | `P` | O clique posiciona o clipe do Picker |
| Pincel | `B` | Arrastar pinta clipes um depois do outro |
| Borracha | `D` | Clicar ou arrastar exclui |
| Mudo | `T` | Clicar ou arrastar silencia ou reativa clipes |
| Faca | `C` | Arraste uma linha para cortar vários clipes; o clique corta naquele ponto |
| Laço | `E` | Arrastar desenha uma caixa de seleção. `Shift` acrescenta |
| Zoom | `Z` | O clique aproxima, arrastar um intervalo aproxima até ele, o clique com o botão direito afasta |
| Ouvir | `Y` | Clicar ou arrastar toca a partir daquele ponto |
| Deslizar | `S` | Arrastar desliza o conteúdo do clipe dentro do clipe |

## Trabalhar com clipes

- Arraste um clipe para movê-lo. `Shift`+arrastar faz uma cópia.
- Arraste a ponta de um clipe para redimensioná-lo. Arrastar o início o apara, com o conteúdo ficando no lugar.
- O clique com o botão direito exclui um clipe. `Ctrl`+arrastar seleciona.
- Dê um clique duplo em um clipe para abri-lo.
- O botão no canto superior esquerdo de um clipe abre o menu do clipe (tornar único, renomear e colorir, picotar e, para áudio: pitch, ajustar ao tempo, inverter).
- Mantenha `Alt` pressionado ao arrastar para ignorar a grade.
- Clipes de áudio têm alças de fade e um ganho de clipe que você arrasta para cima ou para baixo.
- `Shift+M` liga o Stretch: redimensionar um clipe de áudio passa a esticar o áudio em vez de cortá-lo.

## Faixas

No cabeçalho de uma faixa, o clique seleciona, `Ctrl`+clique adiciona ou remove, `Shift`+clique seleciona um intervalo e o clique com o botão direito abre o menu da faixa. `Shift`+roda move a faixa para cima ou para baixo. A luz da faixa a silencia (clique) ou a coloca em solo (`Ctrl`+clique ou clique com o botão direito). Arraste o divisor entre duas faixas para redimensionar uma, ou `Shift`+arrastar para redimensionar todas.

## Régua, loop e marcadores

- Clique na régua para definir a posição da música.
- Arrastar com o botão direito, `Ctrl`+arrastar ou clique duplo seguido de arrastar na régua cria uma seleção de tempo, que também é o loop. `Shift`+arrastar a move.
- `Ctrl+Ins` insere a duração da seleção de tempo em todas as faixas, `Ctrl+Del` a remove.
- `Alt+T` adiciona um marcador. Clique em um marcador para saltar até ele.
- `Ctrl`+roda dá zoom, `Alt`+roda muda a altura das faixas.

## Automação

Clique com o botão direito em um knob ou fader e escolha `Criar clipe de automação`. O clipe aparece na Playlist. Com o Lápis, arraste os pontos dele e clique na curva para adicionar um. As lanes de automação sob as faixas são opcionais e vêm desligadas por padrão. Veja [Automação](/wiki/automation/).

## Arranjos

A caixa de arranjo na barra de ferramentas alterna entre arranjos. Um projeto pode ter vários.

## Teclas

| Comando | Tecla | O que faz |
|---|---|---|
| Selecionar tudo | `Ctrl+A` | Seleciona todos os clipes |
| Copiar, recortar, colar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, recorta ou cola clipes |
| Duplicar | `Ctrl+B` | Duplica a seleção |
| Excluir | `Del` | Exclui os clipes selecionados |
| Excluir com ripple | `Shift+Del` | Exclui os clipes selecionados e fecha os vãos |
| Edição ripple | `Shift+S` | Liga ou desliga a edição ripple |
| Silenciar, reativar | `Alt+M` / `Shift+Alt+M` | Silencia ou reativa os clipes selecionados |
| Tornar único | `Ctrl+Shift+V` | Torna únicos os clipes selecionados |
| Mesclar | `Ctrl+G` | Mescla clipes de pattern |
| Stretch | `Shift+M` | Liga o Stretch ao redimensionar clipes de áudio |
| Inserir tempo | `Ctrl+Ins` | Insere a duração da seleção de tempo em todas as faixas |
| Excluir tempo | `Ctrl+Del` | Remove a seleção de tempo em todas as faixas |
| Adicionar marcador | `Alt+T` | Adiciona um marcador |
| Zoom | `PageUp` / `PageDown` | Aproxima ou afasta |
| Zoom para caber | `Shift+4` | Dá zoom para mostrar tudo |
| Maximizar | `Enter` | Maximiza ou restaura a Playlist |

## Bom saber

- A Playlist só é ouvida no modo Song. No modo Pattern, `Space` toca o pattern atual, seja o que for que estiver nas faixas.
- `Exibir › Estilo de layout › Fluxo por disparo de clipes` acrescenta uma grade de clipes e cenas ao lado da Playlist. É opcional. Veja [Layouts](/wiki/layouts/).
- Com vários clipes de automação sobrepostos em um mesmo controle, a escolha de qual deles toca pode diferir da do FL Studio (notas da versão 0.1.25).
- Um tempo alterado por um controlador MIDI vinculado não redimensiona os clipes de áudio.
- No Linux, vídeo precisa do FFmpeg.

## Veja também

- [Channel Rack](/wiki/channel-rack/)
- [Automação](/wiki/automation/)
- [Gravação](/wiki/recording/)
- [Exportação](/wiki/exporting/)
- [Layouts](/wiki/layouts/)
