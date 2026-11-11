---
source: de4ead4a7a
---
Esta página percorre uma vez o caminho inteiro: um pattern de bateria, uma melodia, um arranjo, uma mixagem e um arquivo exportado. Cada parte tem um link para a página que cobre a janela correspondente por completo. Se você não ouvir nada em algum ponto, veja [Configuração de áudio](/wiki/audio-setup/).

## Fazer um pattern de bateria

1. Escolha `Arquivo › Novo a partir de modelo › Vazio`. O projeto começa com cinco canais, Kick, Clap, Hat, Snare e Synth, e nenhum step.
2. Pressione `F6` se o [Channel Rack](/wiki/channel-rack/) não estiver aparecendo.
3. Clique em alguns steps na linha do Kick. Arrastar desenha vários, e o clique com o botão direito apaga.
4. Pressione `Space`. O transporte está no modo Pattern, então o pattern toca em loop. Pressione `Space` de novo para parar.
5. Dê às linhas Clap, Hat e Snare os seus próprios steps.
6. Pressione `F2` para nomear e colorir o pattern.

Para adicionar um som que não está lá, clique na caixa `+ adicionar canal` sob a última linha e escolha um instrumento no menu. Para usar um sample seu, abra o [Browser](/wiki/browser/) com `Alt+F8`, clique em um arquivo de áudio para ouvi-lo e dê um clique duplo para adicioná-lo como novo canal.

## Escrever uma melodia

1. Pressione `F4`. O LYNX SHARE vai para o próximo pattern vazio e pede o nome dele.
2. Clique com o botão direito no nome do canal Synth e escolha `Piano Roll`.
3. Com o Lápis (`P`), clique na grade para adicionar uma nota.
4. Arraste uma nota para movê-la e arraste a ponta dela para mudar a duração. O clique com o botão direito exclui uma nota.
5. Pressione `Space` para ouvir o pattern enquanto edita.

`Ctrl+T` transforma o teclado de digitação em um piano, então você pode testar notas em `Z` a `M` e `Q` a `P` antes. Desligue de novo antes de usar atalhos de letra como `P`.

## Arranjar os patterns

1. Pressione `F5` para mostrar a [Playlist](/wiki/playlist/).
2. No painel Picker, à esquerda da Playlist, clique no pattern de bateria. Ele passa a ser a fonte de clipes.
3. Com o Lápis (`P`), clique em uma faixa para posicionar o pattern. Clique de novo mais à direita para posicioná-lo outra vez, ou use o Pincel (`B`) e arraste para pintar clipes um depois do outro.
4. Clique no pattern da melodia no Picker e posicione-o em outra faixa.
5. Pressione `L` para mudar para o modo Song e depois `Space`. A Playlist toca a partir da posição da música. Clique na régua para definir a posição.

## Mixar e adicionar um efeito

1. Pressione `F9` para mostrar o [Mixer](/wiki/mixer/). Os cinco canais iniciais já tocam pelos inserts 1 a 5, e um canal adicionado pelo menu `+ adicionar canal` usa um insert livre. O número na caixa de rota do canal, no Channel Rack, diz qual.
2. Para mover canais para inserts livres só deles, selecione-os no Channel Rack e pressione `Ctrl+L`. Cada insert recebe o nome e a cor do canal.
3. Arraste o fader de um insert para definir o nível.
4. Clique em um insert para selecioná-lo. O painel de efeitos à direita mostra os dez slots dele.
5. Clique em um slot vazio e escolha um efeito. Clique no nome do slot para mostrar ou ocultar a janela do efeito. A luz do slot faz o bypass do efeito.

## Exportar a música

![A janela de exportação: intervalo e cauda à esquerda, formato, profundidade de bits e taxa de amostragem à direita, opções de mixagem abaixo](/assets/img/wiki/export.webp)

1. Pressione `Ctrl+S` e salve o projeto primeiro.
2. Escolha `Arquivo › Exportar › Arquivo Wave...`. Para outros formatos, o mesmo menu tem `Arquivo MP3...`, `Arquivo OGG...` e `Arquivo FLAC...`.
3. Defina `Extensão` como `Song (Playlist)`. As outras opções são `Pattern atual` e `Seleção da Playlist`.
4. Deixe as outras opções como estão.
5. Clique em `Exportar...`, escolha o nome do arquivo e espere a renderização.

## O que cada tecla fez

| Comando | Tecla | O que faz |
|---|---|---|
| Channel Rack | `F6` | Mostra ou oculta o Channel Rack |
| Próximo pattern vazio | `F4` | Vai para o próximo pattern vazio e o nomeia |
| Piano Roll | `F7` | Mostra ou oculta o Piano Roll |
| Playlist | `F5` | Mostra ou oculta a Playlist |
| Modo Pattern ou Song | `L` | Alterna o que `Space` toca |
| Mixer | `F9` | Mostra ou oculta o Mixer |
| Enviar para inserts livres | `Ctrl+L` | Dá aos canais selecionados seus próprios inserts do Mixer |
| Salvar | `Ctrl+S` | Salva o projeto como `.lynx` |
| Exportar | `Ctrl+R` | Abre a janela de exportação com o último formato usado |

## Bom saber

- No modo Pattern você ouve só o pattern atual. Se a música estiver muda depois de arranjar, confira se o transporte está no modo Song.
- A janela de exportação começa em `Song (Playlist)` quando o transporte está no modo Song e a Playlist tem clipes. Caso contrário, começa em `Pattern atual`.
- O clique do metrônomo nunca faz parte de uma exportação.
- O rodapé da janela de exportação mostra a duração da música em compassos, o tempo total e o espaço em disco de que o arquivo precisa.
- Por padrão, um projeto novo tem um Limitador no insert Master. Ele faz parte do que você ouve e do que exporta.
- Desfazer é `Ctrl+Z`. Pressionado de novo, refaz aquele passo. `Ctrl+Alt+Z` volta mais atrás.

## Veja também

- [Um passeio pela janela](/wiki/tour/)
- [Exportação](/wiki/exporting/)
- [Automação](/wiki/automation/)
- [Gravação](/wiki/recording/)
- [Instrumentos e efeitos](/instruments-and-effects/)
