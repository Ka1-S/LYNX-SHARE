---
source: a73763934f
---
![O Piano Roll](/assets/img/pianoroll.webp)

O Piano Roll mostra as notas de um canal no pattern atual, com um teclado à esquerda e uma faixa de valores por nota embaixo. Aperte `F7` para mostrá-lo ou escondê-lo, ou escolha `Piano Roll` no menu de um canal no [Channel Rack](/wiki/channel-rack/).

A caixa de canal na barra de ferramentas escolhe qual canal você edita. Os canais que têm notas neste pattern aparecem marcados. A roda sobre a caixa passa pelos canais com notas, `Ctrl`+roda passa por todos.

## Escrever algumas notas

1. No Channel Rack, clique com o botão direito no nome de um canal e escolha `Piano Roll`.
2. Aperte `P` para o Lápis.
3. Clique na grade para adicionar uma nota. Mantenha o botão apertado e arraste para posicioná-la.
4. Arraste o fim da nota para deixá-la mais longa ou mais curta.
5. Clique com o botão direito em uma nota para apagá-la.
6. Aperte `Space` para ouvir o pattern.

## Ferramentas

![A barra de ferramentas do Piano Roll com a caixa de canal, as ferramentas e a caixa de grade](/assets/img/wiki/pianoroll-tools.webp)

Cada ferramenta tem uma tecla de letra. As letras funcionam enquanto o teclado do computador como piano (`Ctrl+T`) está desligado.

| Ferramenta | Tecla | O que faz |
|---|---|---|
| Lápis | `P` | Clicar adiciona uma nota, arrastar a posiciona |
| Pincel | `B` | Arrastar pinta uma sequência de notas. Com `Shift`, uma de cada vez |
| Pincel de bateria | `N` | Um golpe por célula da grade na tecla sob o mouse |
| Borracha | `D` | Clicar ou arrastar apaga |
| Mudo | `T` | Clicar ou arrastar silencia ou reativa notas |
| Faca | `C` | Arraste uma linha para cortar através das notas; clicar corta naquele ponto |
| Laço | `E` | Arrastar desenha uma caixa de seleção. `Shift` acrescenta |
| Zoom | `Z` | Clicar aproxima, arrastar um intervalo dá zoom nele, o clique direito afasta |
| Ouvir | `Y` | Clicar ou arrastar toca as notas sob o cursor |

## Trabalhar com notas

- Arraste uma nota para movê-la. `Shift`+arrastar faz uma cópia.
- Arraste o fim de uma nota para redimensioná-la. O início também pode ser arrastado, com o fim ficando no lugar.
- O clique direito apaga uma nota, em qualquer ferramenta.
- Dê um clique duplo em uma nota para abrir as propriedades dela.
- `Alt`+roda sobre uma nota muda a velocity dela.
- `Ctrl`+arrastar seleciona. `Ctrl`+clique adiciona uma nota à seleção ou a tira.
- Segure `Alt` ao arrastar para ignorar a grade. `Backspace` liga e desliga a grade.
- Com várias notas selecionadas, a alça de esticar estica ou comprime as notas no tempo.

## Teclado e régua

Clique em uma tecla do teclado para ouvi-la. Quanto mais à direita na tecla, mais forte. `Ctrl`+clique em uma tecla seleciona as notas dela. Arrastar com o botão do meio no teclado deixa as teclas mais altas ou mais baixas. A roda de zoom vertical faz o mesmo: é o quadradinho no fim da barra de rolagem do tempo, que você arrasta para cima ou para baixo, ou usa a roda do mouse sobre ele.

`Exibir › Estilo do teclado` no menu do Piano Roll escolhe entre `Teclas planas` e `Teclas de piano (linhas entre as teclas brancas)`, que desenha um teclado tradicional.

Clique na régua para definir a posição. `Ctrl`+arrastar, arrastar com o botão direito ou clique duplo e arrastar na régua seleciona as notas de um intervalo de tempo. `Ctrl`+roda dá zoom e `Shift`+roda rola.

## Barra de ferramentas

| Opção | O que faz |
|---|---|
| Grade | "Main" segue a grade da barra de ferramentas principal, ou escolha uma para esta janela. |
| Acorde | Clicar cria o acorde escolhido, com a nota clicada como fundamental. `Shift+N` volta às notas simples. A mesma lista é `Carimbo de acorde` no menu do Piano Roll. |
| Escala | Destaca as notas de uma escala. Um botão separado ajusta as notas novas e movidas, e o MIDI recebido, à escala escolhida. As notas movidas passam então a andar por passos da escala, de modo que um acorde fica na escala, e `Up` e `Down` movem um passo da escala. |
| Slide (`S`) | As notas novas são notas slide. A nota que está soando desliza até elas (glide 808). |
| Notas fantasma (`Alt+V`) | Mostra as notas dos outros canais atrás das suas. |
| Cor da nota | As notas novas recebem esta cor. São 16. |
| Follow | A vista rola com o cursor de reprodução enquanto o pattern toca. |
| Caixa de faixa | Escolhe o que a faixa de baixo edita: Velocity, Pan, Pitch fino, Release, Mod X, Mod Y, Chance ou Faixa de velocity. |

## Chance e faixa de velocity

Toda nota tem uma Chance e uma Faixa de velocity. A Chance é a probabilidade de a nota tocar cada vez que ela volta. A Faixa de velocity deixa a velocity variar entre a velocity da nota e essa velocity mais a faixa. Escolha `Chance` ou `Faixa de velocity (LYNX)` na caixa de faixa para editá-las. `Ferramentas › Chance (LYNX)` no menu do Piano Roll agrupa as notas selecionadas para que todas ou só uma delas toque, e tem `Redefinir chance e faixa de velocity`.

## Ferramentas de notas

O menu `Ferramentas` do Piano roll abre as ferramentas de notas do FL Studio como diálogos. Elas atuam nas notas selecionadas, ou em todas as notas quando nenhuma está selecionada. Algumas delas:

| Ferramenta | Tecla | O que faz |
|---|---|---|
| Limit | `Alt+K` | Move notas por oitavas para dentro de uma faixa de teclas e depois as limita. Em `Restrição de tom / escala` ele mantém as notas em uma tecla-base e um dos acordes ou escalas do FL Studio, com `Grade` em `Acima`, `Abaixo` ou `Alternar`. `Dobrar para o topo / a base` e `Transpor` definem para onde vão as notas fora da faixa |
| Flip | `Alt+Y` | `Inverter horizontalmente` espelha as notas no tempo. Com `Preservar os tempos de início`, ligado no início, os inícios são espelhados e as notas mantêm a duração. `Inverter verticalmente` espelha as teclas em torno do meio da faixa delas |
| Randomise | `Alt+R` | Mudanças aleatórias de velocity, pan, afinação fina, timing e duração, e de Mod X, Mod Y e release |
| Scale levels | `Alt+X` | Escala um nível de nota do início ao fim da seleção, com `Deslocamento`, `Centro` (o valor em torno do qual escala) e `Tensão` |
| LFO | `Alt+O` | Escreve uma forma de LFO em um nível de nota. Clique com o botão direito em um slider `Período` para ver uma lista de 1 passo a 512 passos |

## Ferramentas MIDI e scripts

`Ferramentas › Ferramentas MIDI...` no menu do Piano Roll abre um painel com Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp e Velocity Shaper.

A coluna `Scripts` do menu `Ferramentas` lista os scripts do Piano Roll: os do próprio LYNX SHARE e arquivos `.pyscript`, incluindo os das pastas de scripts do FL Studio, se o FL Studio estiver instalado. `Abrir a pasta de scripts...` abre a pasta para os seus próprios scripts, e `Executar o último script de novo` repete o último.

## Teclas

| Comando | Tecla | O que faz |
|---|---|---|
| Selecionar tudo | `Ctrl+A` | Seleciona todas as notas |
| Desmarcar | `Ctrl+D` ou `Esc` | Limpa a seleção |
| Copiar, recortar, colar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, recorta ou cola notas |
| Duplicar | `Ctrl+B` | Duplica a seleção |
| Apagar | `Del` | Apaga a seleção |
| Transpor | `Up` / `Down` | Move a seleção um semitom |
| Transpor oitava | `Ctrl+Up` / `Ctrl+Down` | Move a seleção uma oitava |
| Deslocar | `Left` / `Right` | Move a seleção um passo da grade |
| Quantização rápida | `Ctrl+Q` | Quantiza inícios e durações |
| Quantizar inícios | `Shift+Q` | Quantiza só os inícios |
| Picotar | `Ctrl+U` | Corta as notas na grade |
| Unir notas | `Ctrl+G` | Junta notas que se tocam |
| Legato | `Ctrl+L` | Faz cada nota durar até a seguinte |
| Mutar, reativar | `Alt+M` / `Shift+Alt+M` | Silencia ou reativa a seleção |
| Importar MIDI | `Ctrl+M` | Importa um arquivo MIDI para este canal |
| Exportar MIDI | `Ctrl+Shift+M` | Exporta o pattern como um arquivo MIDI |
| Zoom | `PageUp` / `PageDown` | Aproxima ou afasta |

As ferramentas de notas (Arpejar, Dedilhar, Espelhar, Aleatorizar, Quantizar, Picotar e outras) abrem caixas de diálogo. As teclas delas estão na página [Atalhos](/wiki/shortcuts/).

## Bom saber

- O Piano Roll mostra um canal em um pattern. Mude o pattern atual e ele mostra as notas desse pattern.
- As letras das ferramentas não fazem nada enquanto o teclado do computador como piano (`Ctrl+T`) está ligado.
- O clique direito apaga com qualquer ferramenta, então não é preciso trocar para a Borracha por causa de uma nota.
- `S` é o interruptor Slide aqui. Na Playlist, a mesma letra é a ferramenta Deslizar.
- Notas que não são steps simples (outras teclas, notas mais longas, slides) fazem a linha do canal no Channel Rack mostrar uma miniatura em vez de botões de step.

## Veja também

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Atalhos](/wiki/shortcuts/)
- [Formatos de arquivo](/wiki/file-formats/)
