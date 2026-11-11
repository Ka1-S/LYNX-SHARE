---
source: 200df26182
---
![O Channel Rack com linhas de steps](/assets/img/rack.webp)

O Channel Rack lista todos os instrumentos (canais) do projeto, um por linha, e mostra os steps do pattern atual. Pressione `F6` para mostrar ou ocultar. Clicar com o botão direito no botão Pattern do transporte também o mostra.

## Fazer um pattern de bateria

1. Pressione `F6` para mostrar o Channel Rack.
2. Clique na caixa `+ adicionar canal` abaixo da última linha e escolha um instrumento no menu. Você também pode dar um clique duplo em um sample no [Browser](/wiki/browser/) para criar um novo canal de sampler.
3. Clique nos steps da linha do canal para ativá-los. Arraste para desenhar uma sequência de steps.
4. Clique com o botão direito em um step para apagá-lo.
5. Pressione `Space` para tocar. No modo Pattern, o pattern fica em loop.
6. Arraste o knob de volume da linha para ajustar o nível.

## Programar steps

- Clique em um step para ativá-lo. Arrastar desenha uma sequência de steps. Se o arrasto começar em um step aceso, ele apaga em vez de desenhar.
- Clique com o botão direito, ou arraste com o botão direito, para apagar.
- Clicar depois do fim do pattern adiciona um step e alonga o pattern.
- Uma linha cujas notas não são steps simples (outras teclas, notas mais longas, slides) mostra uma miniatura das notas. Clicar nela as abre no [Piano Roll](/wiki/piano-roll/).
- Você pode soltar arquivos de áudio no rack para criar canais.

O menu do canal (clique com o botão direito no nome do canal) tem `Preencher a cada 2 steps`, `Preencher a cada 4 steps`, `Preencher a cada 8 steps`, `Preenchimento avançado`, e `Girar para a esquerda` e `Girar para a direita`.

## A faixa de gráfico

![A faixa de gráfico aberta sob o canal Kick, com uma barra de velocity por step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` mostra a faixa de gráfico sob o canal selecionado. Os botões à esquerda escolhem o que ela edita: Velocity, Pan, Release, Fine pitch, Mod X ou Mod Y. Arrastar define um valor por step, arrastar com o botão direito desenha uma rampa e `Alt`+clique redefine um step.

## As partes de uma linha

| Parte | O que faz |
|---|---|
| Luz de mudo | Clique silencia. `Ctrl`+clique ou clique com o botão direito coloca em solo. |
| Knobs de pan e volume | Arraste ou use a roda. |
| Caixa de rota | Arrastar ou usar a roda escolhe o insert do Mixer. Clique duplo mostra esse insert no Mixer. Clique com o botão direito pega o próximo insert livre. |
| Botão de nome | Clique abre ou fecha o editor do canal. `Alt`+clique mantém abertos os outros editores. `Ctrl`+clique faz a pré-escuta. Arrastar para cima ou para baixo reordena. Clique com o botão direito abre o menu do canal. |
| Seletor | Clique seleciona um canal. `Ctrl`+clique ou clique com o botão direito adiciona ou remove. `Shift`+clique seleciona um intervalo. Clique duplo seleciona todos. |

## Patterns

O rack sempre edita o pattern atual. O seletor de pattern do rack mostra o nome dele: clique no nome para ver a lista, ou use as setas. `Shift`+roda sobre o título do rack também troca o pattern.

A caixa de duração do pattern no rack define a duração em compassos (arraste ou use a roda). Clique duplo nela para uma duração automática, tirada das notas.

O knob de swing na barra de ferramentas principal vale para todos os canais. `Mix do swing` no menu do canal define o quanto um canal o acompanha.

| Comando | Tecla | O que faz |
|---|---|---|
| Próximo pattern | `Numpad +` | Torna o próximo pattern o atual |
| Pattern anterior | `Numpad -` | Torna o pattern anterior o atual |
| Próximo pattern vazio | `F4` | Vai para o próximo pattern vazio e pede um nome |
| Renomear e colorir | `F2` | Renomeia e colore o pattern atual |

## Tocar notas com o teclado do PC

`Ctrl+T` transforma o teclado de digitação em um piano: `Z` a `M` e `Q` a `P` tocam os canais selecionados. Enquanto está ativado, os atalhos de uma letra dos editores ficam em pausa.

## Teclas

Funcionam enquanto o Channel Rack está com o foco.

| Comando | Tecla | O que faz |
|---|---|---|
| Selecionar canal | `Up` / `Down` | Seleciona o canal acima ou abaixo |
| Estender a seleção | `Shift+Up` / `Shift+Down` | Adiciona à seleção o canal acima ou abaixo |
| Mover canais | `Alt+Up` / `Alt+Down` | Move os canais selecionados para cima ou para baixo |
| Selecionar tudo | `Ctrl+A` | Seleciona todos os canais |
| Recortar, copiar, colar | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Recorta, copia ou cola os steps dos canais selecionados |
| Rotacionar steps | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotaciona os steps dos canais selecionados |
| Clonar | `Alt+C` | Clona os canais selecionados |
| Excluir | `Alt+Del` | Exclui os canais selecionados |
| Agrupar | `Alt+G` | Coloca os canais selecionados em um novo grupo de filtro |
| Rotear para o Mixer | `Ctrl+L` | Envia os canais selecionados para inserts livres do Mixer |
| Mudo | `1` a `0` | Silencia ou reativa os canais 1 a 10 |
| Solo | `Ctrl+1` a `Ctrl+0` | Coloca em solo os canais 1 a 10 |
| Faixa de gráfico | `Ctrl+K` | Mostra ou oculta a faixa de gráfico |

## Congelar um canal

Congelar renderiza um canal em áudio e toca esse áudio no lugar dele. O instrumento é descarregado, plugin de terceiros também, então não usa CPU nem memória.

1. Clique com o botão direito no canal e escolha `Congelar`. Com vários canais selecionados, o item diz `Congelar os canais selecionados`.
2. A linha ganha uma etiqueta de floco de neve. O volume, o pan do canal e os efeitos do insert dele continuam funcionando como antes.
3. Para mudar as notas ou o instrumento de novo, clique com o botão direito no canal e escolha `Descongelar`. O instrumento volta com as configurações que tinha.

Um canal congelado não pode ser editado: os steps ficam travados e a janela do instrumento não abre. Se as notas, o tempo ou o arranjo mudarem depois do congelamento, a etiqueta muda de cor e o áudio feito antes continua tocando até você escolher `Recongelar`. Se os arquivos de áudio renderizados estiverem faltando, o instrumento toca no lugar.

Congelar funciona só em canais. Um insert do Mixer não pode ser congelado.

## Outras opções do menu do canal

`Piano Roll`, `Renomear e colorir`, `Substituir` (trocar o instrumento), `Clonar`, `Delete`, `Arpejador`, `Ponto de loop`, `Gravar MIDI em`, `Ordenar canais`, `Selecionar canais sem uso` e `Configurações do canal`.

## Bom saber

- Só a caixa `+ adicionar canal` abre o menu de adicionar.
- O rack não tem pattern próprio. Mudar o pattern atual em qualquer lugar muda o que o rack mostra.
- Uma linha que mostra uma miniatura de notas não pode ser editada como steps. Use o Piano Roll para ela.
- As teclas numéricas alcançam só os dez primeiros canais.
- As teclas da tabela exigem que o rack esteja com o foco. Clique no rack primeiro.

## Veja também

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Atalhos](/wiki/shortcuts/)
